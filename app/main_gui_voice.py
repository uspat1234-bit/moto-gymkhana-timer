 # ====================================================================
# 1. ライブラリインポート・通信設定
# ====================================================================
import flet as ft
import threading
import socket
import json
import time
import serial
import re
import csv
import os
from serial.tools import list_ports
import pyttsx3

try:
    import nfc
    import ndef
    NFC_AVAILABLE = True
except ImportError:
    NFC_AVAILABLE = False

UDP_IP = "0.0.0.0"
UDP_PORT = 5005

class MotoGymkhanaApp:
    # ====================================================================
    # 2. アプリケーション初期化・ステート定義
    # ====================================================================
    def __init__(self, page: ft.Page):
        self.page = page
        self.page.title = "MGTS - 総合データ管理窓口"
        self.page.theme_mode = ft.ThemeMode.DARK
        self.page.padding = 0
        
        self.ser = None
        self.rider_database = {}    
        self.active_runners = []    
        self.runner_notes = {}      
        self.results_log = []       
        self.is_nfc_locked = False  
        
        self.file_picker = ft.FilePicker(on_result=self.on_csv_selected)
        self.save_file_picker = ft.FilePicker(on_result=self.on_save_csv_result)
        self.page.overlay.extend([self.file_picker, self.save_file_picker])
        
        self.init_ui_components()
        self.build_layout()
        
        threading.Thread(target=self.udp_listener, daemon=True).start()
        if NFC_AVAILABLE: threading.Thread(target=self.nfc_listener, daemon=True).start()
        else: self.log_message("⚠️ nfcpy未検出: NFCリーダーがPCに直接接続されていません", ft.Colors.YELLOW)

    # ====================================================================
    # 3. UIコンポーネント構築・レイアウト定義
    # ====================================================================
    def init_ui_components(self):
        self.runner_count_text = ft.Text("0 台", size=30, weight=ft.FontWeight.BOLD, color=ft.Colors.CYAN_400)
        self.active_runners_row = ft.Row(wrap=True)
        
        self.result_tabs = ft.Tabs(selected_index=0, animation_duration=300, tabs=[], expand=True)
        
        self.btn_export_csv = ft.ElevatedButton("リザルトをCSV保存", icon=ft.Icons.DOWNLOAD, on_click=lambda _: self.save_file_picker.save_file(allowed_extensions=["csv"], file_name="mgts_results.csv"), color=ft.Colors.WHITE, bgcolor=ft.Colors.BLUE_700)
        self.btn_import_csv = ft.ElevatedButton("名簿CSVを一括読込", icon=ft.Icons.UPLOAD_FILE, on_click=lambda _: self.file_picker.pick_files(allowed_extensions=["csv"], allow_multiple=False), color=ft.Colors.WHITE, bgcolor=ft.Colors.GREEN_700)
        self.rider_table = ft.DataTable(columns=[ft.DataColumn(label=ft.Text("タグID")), ft.DataColumn(label=ft.Text("ゼッケン")), ft.DataColumn(label=ft.Text("選手名")), ft.DataColumn(label=ft.Text("クラス"))], rows=[])

        self.drop_com = ft.Dropdown(label="COMポート", width=200, options=[])
        self.btn_connect_ser = ft.ElevatedButton("接続", icon=ft.Icons.CABLE, on_click=self.connect_serial)
        self.log_box = ft.ListView(expand=True, spacing=5, auto_scroll=True)
        
        self.current_edit_record = None
        self.penalty_dialog = ft.AlertDialog(
            title=ft.Text("ペナルティ操作"),
            content=ft.Column([
                ft.ElevatedButton("+1秒 (パイロンタッチ)", on_click=lambda e: self.apply_penalty(1, "PT"), bgcolor=ft.Colors.ORANGE_800, color=ft.Colors.WHITE, width=250),
                ft.ElevatedButton("+1秒 (足つき)", on_click=lambda e: self.apply_penalty(1, "足つき"), bgcolor=ft.Colors.ORANGE_800, color=ft.Colors.WHITE, width=250),
                ft.ElevatedButton("+3秒 (脱輪等)", on_click=lambda e: self.apply_penalty(3, "脱輪"), bgcolor=ft.Colors.RED_800, color=ft.Colors.WHITE, width=250),
                ft.Divider(),
                ft.ElevatedButton("MC (ミスコース) にする", on_click=lambda e: self.apply_penalty(999, "MC"), bgcolor=ft.Colors.PURPLE_800, color=ft.Colors.WHITE, width=250),
                ft.Divider(),
                ft.ElevatedButton("ペナルティ・MCをリセット", on_click=lambda e: self.apply_penalty(0, "RESET"), color=ft.Colors.RED_200, width=250),
            ], tight=True),
            actions=[ft.TextButton("閉じる", on_click=lambda e: self.close_penalty_dialog())],
        )
        self.page.overlay.append(self.penalty_dialog)
        self.refresh_com_ports()

    def build_layout(self):
        self.timing_view = ft.Container(expand=True, padding=20, content=ft.Column([
                ft.Text("⏱️ 計測ダッシュボード", size=30, weight=ft.FontWeight.BOLD),
                ft.Card(content=ft.Container(padding=20, content=ft.Row([
                    ft.Column([ft.Text("現在コース上の台数", color=ft.Colors.GREY_400), self.runner_count_text], expand=1),
                    ft.Column([ft.Text("出走中 ➡ スターティング", color=ft.Colors.GREY_400), self.active_runners_row], expand=5),
                ]))),
                ft.Divider(),
                ft.Row([ft.Text("📊 リザルト一覧", size=20, weight=ft.FontWeight.BOLD), self.btn_export_csv], alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
                ft.Column([self.result_tabs], expand=True)
            ]))

        self.nfc_view = ft.Container(expand=True, padding=20, visible=False, content=ft.Column([
                ft.Text("🏍️ 選手名簿マスタ", size=30, weight=ft.FontWeight.BOLD),
                ft.Row([self.btn_import_csv]), ft.Divider(),
                ft.Text("読み込み済みデータ", size=20, weight=ft.FontWeight.BOLD),
                ft.Column([self.rider_table], expand=True, scroll=ft.ScrollMode.AUTO)
            ]))

        self.system_view = ft.Container(expand=True, padding=20, visible=False, content=ft.Column([
                ft.Text("⚙️ システムログ", size=30, weight=ft.FontWeight.BOLD),
                ft.Row([self.drop_com, self.btn_connect_ser, ft.IconButton(icon=ft.Icons.REFRESH, on_click=lambda e: self.refresh_com_ports())]),
                ft.Divider(),
                ft.Container(bgcolor=ft.Colors.BLACK87, padding=10, border_radius=5, expand=True, content=self.log_box)
            ]))

        self.views = [self.timing_view, self.nfc_view, self.system_view]
        self.nav_rail = ft.NavigationRail(
            selected_index=0, label_type=ft.NavigationRailLabelType.ALL, min_width=100, group_alignment=-0.9,
            destinations=[
                ft.NavigationRailDestination(icon=ft.Icons.TIMER, selected_icon=ft.Icons.TIMER, label="計測"),
                ft.NavigationRailDestination(icon=ft.Icons.PEOPLE, selected_icon=ft.Icons.PEOPLE, label="名簿"),
                ft.NavigationRailDestination(icon=ft.Icons.SETTINGS, selected_icon=ft.Icons.SETTINGS, label="ログ"),
            ], on_change=self.handle_nav_change)
        self.page.add(ft.Row(controls=[self.nav_rail, ft.VerticalDivider(width=1), ft.Stack(controls=self.views, expand=True)], expand=True))

    # ====================================================================
    # 4. ペナルティ操作・MC・再計算ロジック
    # ====================================================================
    def open_penalty_dialog(self, record):
        self.current_edit_record = record
        self.penalty_dialog.title.value = f"操作: No.{record['bib']} {record['name']}"
        self.penalty_dialog.open = True
        self.page.update()

    def close_penalty_dialog(self):
        self.penalty_dialog.open = False
        self.page.update()

    def apply_penalty(self, seconds, note_text):
        if not self.current_edit_record: return
        rec = self.current_edit_record
        
        if note_text == "RESET":
            rec["penalty"] = 0
            rec["is_mc"] = False
            rec["note"] = ""
        elif note_text == "MC":
            rec["is_mc"] = True
            rec["note"] = "MC"
        else:
            rec["penalty"] += seconds
            rec["note"] = f"{rec['note']} [{note_text}]".strip()
            rec["is_mc"] = False
            
        if rec["is_mc"]:
            rec["time_float"] = float('inf')
            rec["time_str"] = "MC"
            self.log_message(f"⚠️ 修正: No.{rec['bib']} {rec['name']} -> ミスコース(MC)", ft.Colors.PURPLE_300)
        else:
            rec["time_float"] = round(rec["base_time"] + rec["penalty"], 3)
            rec["time_str"] = f"{rec['time_float']:.3f}"
            self.log_message(f"⚠️ 修正: No.{rec['bib']} {rec['name']} -> {note_text} (トータル: {rec['time_str']}s)", ft.Colors.RED_400)
            
        self.close_penalty_dialog()
        self.recalculate_results()

    def recalculate_results(self):
        if not self.results_log: return
        
        # 1. 全履歴からグローバルのトップタイムを取得
        valid_runs = [r for r in self.results_log if not r.get("is_mc")]
        global_top = min([r["time_float"] for r in valid_runs]) if valid_runs else None
        class_tops = {}
        for r in valid_runs:
            c = r["class"]
            if c not in class_tops or r["time_float"] < class_tops[c]:
                class_tops[c] = r["time_float"]

        # 2. 全レコードに対してタイム比率を計算し、順位をリセット
        for r in self.results_log:
            r["is_best"] = False
            r["overall_rank"] = "-"
            r["class_rank"] = "-"
            if r.get("is_mc"):
                r["top_ratio"] = "-"
                r["class_ratio"] = "-"
            else:
                if global_top: r["top_ratio"] = f"{(r['time_float'] / global_top) * 100:.2f}%"
                if class_tops.get(r["class"]): r["class_ratio"] = f"{(r['time_float'] / class_tops[r['class']]) * 100:.2f}%"

        # 3. 各選手の「ベストタイム」を抽出
        best_runs_map = {}
        for r in self.results_log:
            bib = r["bib"]
            if bib not in best_runs_map:
                best_runs_map[bib] = r
            else:
                curr_best = best_runs_map[bib]
                if curr_best["is_mc"] and not r["is_mc"]:
                    best_runs_map[bib] = r
                elif not curr_best["is_mc"] and not r["is_mc"]:
                    if r["time_float"] < curr_best["time_float"]:
                        best_runs_map[bib] = r

        # 4. ベストタイム同士で順位付けを行う
        best_valid_list = [r for r in best_runs_map.values() if not r.get("is_mc")]
        best_valid_list.sort(key=lambda x: x["time_float"])
        
        for i, r in enumerate(best_valid_list):
            r["overall_rank"] = i + 1
            
        for c in class_tops.keys():
            c_best = [r for r in best_valid_list if r["class"] == c]
            c_best.sort(key=lambda x: x["time_float"])
            for i, r in enumerate(c_best):
                r["class_rank"] = i + 1

        # ベストフラグを立てる
        for r in best_runs_map.values():
            r["is_best"] = True

        self.update_result_table()

    # ====================================================================
    # 5. コアロジック（パケット解析・状態遷移）
    # ====================================================================
    def process_incoming_packet(self, json_str, source):
        try:
            data = json.loads(json_str)
            msg_type = data.get("type")
            raw_id = data.get("id")
            
            rider_id = raw_id if raw_id and raw_id != "X999" else (self.active_runners[0] if self.active_runners else "X999")
            info = self.rider_database.get(rider_id, {"bib": "?", "name": "不明", "class": "-"})
            rider_name = f"No.{info['bib']} {info['name']}"
            current_time = time.time()
            
            if msg_type in ["SEQ_START", "FORCE_DNF"]:
                self.is_nfc_locked = False
                if msg_type == "SEQ_START": self.log_message(f"🚦 シグナル開始 (NFCロック解除)", ft.Colors.GREEN_400)
                else:
                    self.active_runners.clear()
                    self.update_dashboard_counts()
                    self.log_message(f"🛑 コースリセット (NFCロック解除 / 待機列クリア)", ft.Colors.ORANGE_400)
            
            elif msg_type == "REACTION":
                diff = data.get("diff", 0.0)
                if rider_id not in self.runner_notes: self.runner_notes[rider_id] = []
                self.runner_notes[rider_id].append(f"React:{diff}s")
                self.log_message(f"⏱️ リアクション: {rider_name} -> {diff}s", ft.Colors.BLUE_200)
                
            elif msg_type == "FLYING":
                diff = data.get("diff", 0.0)
                if rider_id not in self.runner_notes: self.runner_notes[rider_id] = []
                self.runner_notes[rider_id].append(f"FLYING({diff}s)")
                self.log_message(f"⚠️ フライング検知: {rider_name} -> {diff}s", ft.Colors.RED_400)
                
            elif msg_type == "RESULT":
                run_time = round(float(data.get("time", 999.999)), 3)
                time_str = f"{run_time:.3f}"
                r_class = info.get("class", "-")
                
                for res in self.results_log:
                    if res["bib"] == info["bib"] and res["base_time"] == run_time:
                        if current_time - res.get("recv_time", 0) < 3.0: return
                
                note_str = " / ".join(self.runner_notes.pop(rider_id, [])) or ""
                
                new_record = {
                    "bib": info["bib"], "name": info["name"], "class": r_class,
                    "base_time": run_time, "penalty": 0, "is_mc": False,
                    "time_float": run_time, "time_str": time_str, "note": note_str,
                    "overall_rank": "-", "class_rank": "-", "top_ratio": "-", "class_ratio": "-",
                    "is_best": False, "recv_time": current_time
                }
                self.results_log.append(new_record)
                
                # 計算メソッドを呼ぶことで全員のフラグと比率が更新される
                self.recalculate_results()
                
                sec = int(run_time)
                ms = int(round((run_time - sec) * 1000))
                # 音声読み上げには、今回の走行に対する比率を読ませる
                top_ratio_str = new_record['top_ratio'].replace('%','') if new_record['top_ratio'] != '-' else '測定不能'
                class_ratio_str = new_record['class_ratio'].replace('%','') if new_record['class_ratio'] != '-' else '測定不能'
                
                speech_text = f"ゼッケン{info['bib']}番。タイム、{sec}秒{ms}。総合タイム比、{top_ratio_str}パーセント。クラスタイム比、{class_ratio_str}パーセントです。"

                def speak_async(text):
                    try:
                        engine = pyttsx3.init()
                        engine.say(text)
                        engine.runAndWait()
                    except Exception as e: print(f"TTS Error: {e}")

                threading.Thread(target=speak_async, args=(speech_text,), daemon=True).start()
                self.log_message(f"🏁 ゴール: {rider_name} [{time_str}s] 総合比 {new_record['top_ratio']} / ｸﾗｽ比 {new_record['class_ratio']}", ft.Colors.CYAN_200)
                
                if rider_id in self.active_runners: self.active_runners.remove(rider_id)
                self.update_dashboard_counts()
                
        except Exception: pass

    # ====================================================================
    # 6. UIレンダリング・画面更新
    # ====================================================================
    def update_result_table(self):
        def create_table_frame():
            return ft.DataTable(columns=[
                    ft.DataColumn(label=ft.Text("順位")), ft.DataColumn(label=ft.Text("ｸﾗｽ")), ft.DataColumn(label=ft.Text("ゼッケン")),
                    ft.DataColumn(label=ft.Text("名前")), ft.DataColumn(label=ft.Text("最終タイム")), ft.DataColumn(label=ft.Text("トップ比")),
                    ft.DataColumn(label=ft.Text("ｸﾗｽ比")), ft.DataColumn(label=ft.Text("備考")), ft.DataColumn(label=ft.Text("編集")),
                ], rows=[])

        exist_classes = sorted(list(set([r["class"] for r in self.results_log])))
        # ★「全履歴」タブを追加
        tab_list = ["総合"] + exist_classes + ["全履歴"]
        new_tabs = []
        
        for tab_name in tab_list:
            table = create_table_frame()
            
            if tab_name == "全履歴":
                # 全履歴タブ: 新しい記録が上に来るように逆順で全表示
                records = list(reversed(self.results_log))
            else:
                # 順位表タブ: ベストタイムのみを抽出し、MCを下に回してタイム順ソート
                if tab_name == "総合": records = [r for r in self.results_log if r.get("is_best")]
                else: records = [r for r in self.results_log if r.get("is_best") and r["class"] == tab_name]
                records.sort(key=lambda x: (x.get("is_mc", False), x["time_float"]))

            for r in records:
                rank_str = str(r["overall_rank"]) if tab_name == "総合" else (str(r["class_rank"]) if tab_name != "全履歴" else "-")
                
                time_color = ft.Colors.PURPLE_400 if r.get("is_mc") else (ft.Colors.RED_400 if r["penalty"] > 0 else ft.Colors.WHITE)
                time_display = "MC" if r.get("is_mc") else r["time_str"]
                
                table.rows.append(ft.DataRow(cells=[
                    ft.DataCell(ft.Text(rank_str)), ft.DataCell(ft.Text(r["class"])), ft.DataCell(ft.Text(r["bib"])), ft.DataCell(ft.Text(r["name"])),
                    ft.DataCell(ft.Text(time_display, weight=ft.FontWeight.BOLD, color=time_color)),
                    ft.DataCell(ft.Text(r["top_ratio"])), ft.DataCell(ft.Text(r["class_ratio"])), ft.DataCell(ft.Text(r["note"])),
                    ft.DataCell(ft.IconButton(icon=ft.Icons.ADD_ALERT, icon_size=20, icon_color=ft.Colors.ORANGE_400, on_click=lambda e, rec=r: self.open_penalty_dialog(rec)))
                ]))
            new_tabs.append(ft.Tab(text=tab_name, content=ft.Column([table], scroll=ft.ScrollMode.AUTO)))

        self.result_tabs.tabs = new_tabs
        self.page.update()

    # --- 以下、省略不可の定型処理 ---
    def on_save_csv_result(self, e: ft.FilePickerResultEvent):
        if e.path:
            try:
                with open(e.path, "w", encoding="utf-8-sig", newline="") as f:
                    writer = csv.writer(f)
                    writer.writerow(["出走順", "ベストフラグ", "クラス", "ゼッケン", "名前", "ベースタイム", "ペナルティ", "最終タイム", "総合順位", "クラス順位", "トップ比", "クラス比", "ペナルティ/備考"])
                    for idx, r in enumerate(self.results_log):
                        time_display = "MC" if r.get("is_mc") else r["time_str"]
                        best_mark = "★" if r.get("is_best") else ""
                        writer.writerow([idx + 1, best_mark, r["class"], r["bib"], r["name"], r["base_time"], r["penalty"], time_display, r["overall_rank"], r["class_rank"], r["top_ratio"], r["class_ratio"], r["note"]])
                self.log_message(f"💾 リザルト出力完了: {e.path}", ft.Colors.GREEN)
            except Exception as ex: self.log_message(f"❌ 出力エラー: {ex}", ft.Colors.RED)

    def on_csv_selected(self, e: ft.FilePickerResultEvent):
        if e.files and len(e.files) > 0:
            file_path = e.files[0].path
            try:
                try:
                    with open(file_path, mode='r', encoding='utf-8-sig') as f: lines = f.readlines()
                except UnicodeDecodeError:
                    with open(file_path, mode='r', encoding='shift_jis') as f: lines = f.readlines()
                self._parse_csv(lines)
            except Exception as ex: self.log_message(f"❌ 読み込みエラー: {ex}", ft.Colors.RED)

    def _parse_csv(self, lines):
        if not lines: return
        count, error_count, header_skipped = 0, 0, False
        for idx, raw_line in enumerate(lines):
            line = raw_line.strip()
            if not line: continue
            if line.startswith('"') and line.endswith('"'): line = line[1:-1]
            if ',' in line: row = line.split(',')
            elif '\t' in line: row = line.split('\t')
            else: row = re.split(r'\s+', line)
            row = [item.strip() for item in row if item.strip()]

            if len(row) < 4:
                if idx > 0 and row: error_count += 1
                continue
            if not header_skipped and ("ID" in row[0].upper() or "タグ" in row[0]):
                header_skipped = True
                continue
                
            tag_id, bib, name = row[0].upper(), row[1], row[2]
            r_class = row[3] if len(row) > 3 else "-"
            
            if not bib.isdigit() or not name:
                error_count += 1
                continue

            self.rider_database[tag_id] = {"bib": bib, "name": name, "class": r_class}
            count += 1
            
        self.update_rider_table()
        msg = f"📁 名簿読込完了: {count}名登録"
        if error_count > 0: msg += f" (エラー: {error_count}件)"
        self.log_message(msg, ft.Colors.GREEN if count > 0 else ft.Colors.RED)

    def on_nfc_connect(self, tag):
        try:
            if tag.ndef and tag.ndef.records and isinstance(tag.ndef.records[0], ndef.TextRecord): tag_id = tag.ndef.records[0].text
            else: return True
        except: return True

        if self.is_nfc_locked:
            self.log_message("🔒 ロック中: 前の選手がスタートするまでタッチ不可", ft.Colors.RED)
            self.page.update()
            return True

        if tag_id in self.rider_database:
            rider = self.rider_database[tag_id]
            self.log_message(f"📖 エントリー受付: No.{rider['bib']} {rider['name']} (ID:{tag_id})", ft.Colors.GREEN_200)

            packet = f"{json.dumps({'type':'ENTRY', 'id':tag_id})}\n".encode()
            if self.ser and self.ser.is_open: self.ser.write(packet) 
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                s.sendto(packet, ("255.255.255.255", UDP_PORT))
                s.close()
            except: pass
            
            self.active_runners.append(tag_id)
            self.is_nfc_locked = True
            self.update_dashboard_counts()
        else:
            self.log_message(f"⚠️ 未登録タグ: {tag_id} (CSVに登録されていません)", ft.Colors.YELLOW)
        self.page.update()
        return True

    def log_message(self, msg, color=ft.Colors.WHITE70):
        timestamp = time.strftime("[%H:%M:%S] ")
        self.log_box.controls.append(ft.Text(timestamp + msg, color=color))
        self.page.update()

    def update_rider_table(self):
        self.rider_table.rows = [ft.DataRow(cells=[ft.DataCell(ft.Text(tid)), ft.DataCell(ft.Text(i.get("bib", ""))), ft.DataCell(ft.Text(i.get("name", ""))), ft.DataCell(ft.Text(i.get("class", "")))]) for tid, i in self.rider_database.items()]
        self.page.update()

    def update_dashboard_counts(self):
        self.runner_count_text.value = f"{len(self.active_runners)} 台"
        self.active_runners_row.controls.clear()
        if self.active_runners:
            for tid in self.active_runners:
                info = self.rider_database.get(tid, {"bib": "?", "name": "不明"})
                chip = ft.Chip(label=ft.Text(f"No.{info.get('bib', '?')} {info.get('name', '不明')}", weight=ft.FontWeight.BOLD), bgcolor=ft.Colors.ORANGE_800)
                self.active_runners_row.controls.append(chip)
        else: self.active_runners_row.controls.append(ft.Text("待機なし", size=24, weight=ft.FontWeight.BOLD, color=ft.Colors.ORANGE_400))
        self.page.update()

    def refresh_com_ports(self):
        ports = list_ports.comports()
        self.drop_com.options = [ft.dropdown.Option(p.device) for p in ports]
        if ports: self.drop_com.value = ports[0].device
        self.page.update()

    def handle_nav_change(self, e):
        for i, view in enumerate(self.views): view.visible = (i == e.control.selected_index)
        self.page.update()

    def connect_serial(self, e):
        try:
            self.ser = serial.Serial(self.drop_com.value, 115200, timeout=0.5)
            self.log_message(f"✅ 接続成功: {self.drop_com.value}", ft.Colors.GREEN)
            threading.Thread(target=self.serial_listener, daemon=True).start()
        except Exception as err: self.log_message(f"❌ シリアル接続エラー: {err}", ft.Colors.RED)

    def udp_listener(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind((UDP_IP, UDP_PORT))
            while True:
                data, _ = s.recvfrom(1024)
                self.process_incoming_packet(data.decode('utf-8', 'ignore').strip(), "UDP")
        except: pass

    def serial_listener(self):
        while self.ser and self.ser.is_open:
            if self.ser.in_waiting > 0:
                line = self.ser.readline().decode('utf-8', 'ignore').strip()
                if line == "SEQ_START" or "SEQ_START" in line:
                    self.is_nfc_locked = False
                    self.log_message("🚦 シグナル開始 (NFCロック解除)", ft.Colors.GREEN_400)
                elif line == "FORCE_DNF" or "FORCE_DNF" in line:
                    self.is_nfc_locked = False
                    self.active_runners.clear()
                    self.update_dashboard_counts()
                    self.log_message("🛑 コースリセット (NFCロック解除 / 待機列クリア)", ft.Colors.ORANGE_400)
                elif line.startswith("[ESP_DATA] "): 
                    self.process_incoming_packet(line.replace("[ESP_DATA] ", ""), "SERIAL")

    def nfc_listener(self):
        while True:
            try:
                with nfc.ContactlessFrontend('usb') as clf: clf.connect(rdwr={'on-connect': self.on_nfc_connect})
            except: time.sleep(2)

def main(page: ft.Page): MotoGymkhanaApp(page)
if __name__ == "__main__": ft.app(target=main)
