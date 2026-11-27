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
import math
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
        self.pending_entry = None    # タグ読取済み・未出走の次走者
        self.delete_armed = False    # 削除ボタンの二度押し確認用
        self.restore_armed = False   # 復元ボタンの二度押し確認用
        
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
        
        self.btn_restore = ft.OutlinedButton("バックアップから復元", icon=ft.Icons.RESTORE, on_click=self.restore_backup)
        self.btn_export_csv = ft.ElevatedButton("リザルトをCSV保存", icon=ft.Icons.DOWNLOAD, on_click=lambda _: self.save_file_picker.save_file(allowed_extensions=["csv"], file_name="mgts_results.csv"), color=ft.Colors.WHITE, bgcolor=ft.Colors.BLUE_700)
        self.btn_import_csv = ft.ElevatedButton("名簿CSVを一括読込", icon=ft.Icons.UPLOAD_FILE, on_click=lambda _: self.file_picker.pick_files(allowed_extensions=["csv"], allow_multiple=False), color=ft.Colors.WHITE, bgcolor=ft.Colors.GREEN_700)
        self.rider_table = ft.DataTable(columns=[ft.DataColumn(label=ft.Text("タグID")), ft.DataColumn(label=ft.Text("ゼッケン")), ft.DataColumn(label=ft.Text("選手名")), ft.DataColumn(label=ft.Text("クラス"))], rows=[])

        self.drop_com = ft.Dropdown(label="COMポート", width=200, options=[])
        self.btn_connect_ser = ft.ElevatedButton("接続", icon=ft.Icons.CABLE, on_click=self.connect_serial)
        self.log_box = ft.ListView(expand=True, spacing=5, auto_scroll=True)
        
        self.current_edit_record = None
        self.penalty_dialog = ft.AlertDialog(
            title=ft.Text("リザルト編集"),
            content=ft.Column([], tight=True),
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
                ft.Row([ft.Text("📊 リザルト一覧", size=20, weight=ft.FontWeight.BOLD), ft.Row([self.btn_restore, self.btn_export_csv])], alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
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
    # 4. リザルト編集 (ペナルティ / MC / タイム修正 / 選手付け替え / 削除)
    # ====================================================================
    def open_penalty_dialog(self, record):
        self.current_edit_record = record
        self.delete_armed = False
        rec = record

        def btn(label, cb, bg=None, fg=ft.Colors.WHITE):
            return ft.ElevatedButton(label, on_click=cb, bgcolor=bg, color=fg, width=260)

        self.edit_time_field = ft.TextField(label="ベースタイム(秒)", value=f"{rec['base_time']:.3f}", width=150, dense=True)
        self.edit_rider_drop = ft.Dropdown(
            label="選手", width=260, dense=True, value=rec.get("tag_id"),
            options=[ft.dropdown.Option(key=tid, text=f"No.{i['bib']} {i['name']} ({i['class']})") for tid, i in self.rider_database.items()],
        )
        self.btn_delete = ft.TextButton("このリザルトを削除", icon=ft.Icons.DELETE, on_click=lambda e: self.delete_record(), style=ft.ButtonStyle(color=ft.Colors.RED_300))

        status = rec.get("status", "OK")
        self.penalty_dialog.title.value = f"編集: No.{rec['bib']} {rec['name']}" + (f"  [{status}]" if status != "OK" else "")
        self.penalty_dialog.content = ft.Column([
            ft.Text("ペナルティ", weight=ft.FontWeight.BOLD),
            btn("+1秒 (パイロンタッチ)", lambda e: self.apply_penalty(1, "PT"), ft.Colors.ORANGE_800),
            btn("+1秒 (足つき)", lambda e: self.apply_penalty(1, "足つき"), ft.Colors.ORANGE_800),
            btn("+1秒 (フライング)", lambda e: self.apply_penalty(1, "フライング"), ft.Colors.RED_600),
            btn("+3秒 (脱輪等)", lambda e: self.apply_penalty(3, "脱輪"), ft.Colors.RED_800),
            btn("直前のペナルティを1つ取り消す", lambda e: self.apply_penalty(0, "UNDO"), ft.Colors.GREY_700),
            btn("MC (ミスコース) にする", lambda e: self.apply_penalty(0, "MC"), ft.Colors.PURPLE_800),
            btn("ペナルティ・MCを全てリセット", lambda e: self.apply_penalty(0, "RESET"), None, ft.Colors.RED_200),
            ft.Divider(),
            ft.Text("タイム修正 (DNF/DNSも実タイムに変更できます)", weight=ft.FontWeight.BOLD),
            ft.Row([self.edit_time_field, ft.ElevatedButton("確定", on_click=lambda e: self.apply_time_edit())]),
            ft.Divider(),
            ft.Text("選手の付け替え", weight=ft.FontWeight.BOLD),
            ft.Row([self.edit_rider_drop, ft.ElevatedButton("変更", on_click=lambda e: self.apply_rider_change())], wrap=True),
            ft.Divider(),
            self.btn_delete,
        ], tight=True, scroll=ft.ScrollMode.AUTO, height=560)
        self.penalty_dialog.open = True
        self.page.update()

    def close_penalty_dialog(self):
        self.penalty_dialog.open = False
        self.page.update()

    def refresh_record(self, rec):
        """ペナルティ内訳・MC・DNF/DNS から表示用の値を作り直す"""
        items = rec.setdefault("penalty_items", [])
        rec["penalty"] = sum(sec for sec, _ in items)
        labels = " ".join(f"[{name}]" for _, name in items)
        status = rec.get("status", "OK")
        if status != "OK":
            rec["is_mc"] = True                       # 順位・タイム比から除外
            rec["time_float"] = float('inf')
            rec["time_str"] = status
            rec["penalty_text"] = f"{status} {labels}".strip()
        elif rec.get("is_mc_manual"):
            rec["is_mc"] = True
            rec["time_float"] = float('inf')
            rec["time_str"] = "MC"
            rec["penalty_text"] = f"MC {labels}".strip()
        else:
            rec["is_mc"] = False
            rec["time_float"] = round(rec["base_time"] + rec["penalty"], 3)
            rec["time_str"] = f"{rec['time_float']:.3f}"
            rec["penalty_text"] = labels

    def _after_edit(self, msg, color=ft.Colors.RED_400, close=True):
        self.log_message(msg, color)
        if close: self.close_penalty_dialog()
        self.recalculate_results()
        self.update_result_table()
        self.save_backup()

    def apply_penalty(self, seconds, note_text):
        rec = self.current_edit_record
        if not rec: return
        items = rec.setdefault("penalty_items", [])
        if note_text == "RESET":
            items.clear(); rec["is_mc_manual"] = False
        elif note_text == "MC":
            rec["is_mc_manual"] = True
        elif note_text == "UNDO":
            if rec.get("is_mc_manual"): rec["is_mc_manual"] = False
            elif items: items.pop()
            else: return
        else:
            items.append([seconds, note_text])
        self.refresh_record(rec)
        self._after_edit(f"✏️ 修正: No.{rec['bib']} {rec['name']} -> {note_text} (表示: {rec['time_str']} {rec['penalty_text']})")

    def apply_time_edit(self):
        rec = self.current_edit_record
        if not rec: return
        try:
            v = round(float(self.edit_time_field.value.strip()), 3)
            if not (0 < v < 3600): raise ValueError
        except Exception:
            self.edit_time_field.error_text = "0〜3600の数値で入力"
            self.page.update()
            return
        old = rec["time_str"]
        rec["base_time"] = v
        rec["status"] = "OK"
        self.refresh_record(rec)
        self._after_edit(f"✏️ タイム修正: No.{rec['bib']} {rec['name']} {old} -> {rec['time_str']}")

    def apply_rider_change(self):
        rec = self.current_edit_record
        tid = self.edit_rider_drop.value
        if not rec or not tid or tid not in self.rider_database: return
        old = f"No.{rec['bib']} {rec['name']}"
        info = self.rider_database[tid]
        rec.update({"tag_id": tid, "bib": info["bib"], "name": info["name"], "class": info["class"]})
        self._after_edit(f"✏️ 選手付け替え: {old} -> No.{rec['bib']} {rec['name']} ({rec['time_str']})")

    def delete_record(self):
        rec = self.current_edit_record
        if not rec: return
        if not self.delete_armed:                     # 1回目: 確認表示
            self.delete_armed = True
            self.btn_delete.text = "本当に削除する (もう一度押す)"
            self.page.update()
            return
        if rec in self.results_log: self.results_log.remove(rec)
        self.current_edit_record = None
        self._after_edit(f"🗑️ 削除: No.{rec['bib']} {rec['name']} ({rec['time_str']})", ft.Colors.GREY_400)

    # ---- 自動バックアップ / 復元 ----
    BACKUP_JSON = "mgts_results_backup.json"
    BACKUP_CSV = "mgts_results_auto.csv"

    def save_backup(self):
        try:
            with open(self.BACKUP_JSON, "w", encoding="utf-8") as f:
                json.dump(self.results_log, f, ensure_ascii=False)   # inf は Infinity として保存される
            self.write_results_csv(self.BACKUP_CSV)
        except Exception as ex:
            self.log_message(f"❌ 自動バックアップ失敗: {ex}", ft.Colors.RED)

    def restore_backup(self, e=None):
        if not os.path.exists(self.BACKUP_JSON):
            self.log_message("⚠️ バックアップがありません", ft.Colors.YELLOW); return
        if self.results_log and not self.restore_armed:
            self.restore_armed = True
            self.btn_restore.text = "現在の結果を上書きして復元 (もう一度押す)"
            self.page.update(); return
        self.restore_armed = False
        self.btn_restore.text = "バックアップから復元"
        try:
            with open(self.BACKUP_JSON, encoding="utf-8") as f:
                data = json.load(f)
            for r in data:
                r.setdefault("status", "OK"); r.setdefault("penalty_items", [])
                r.setdefault("tag_id", None); r.setdefault("is_mc_manual", False)
                self.refresh_record(r)
            self.results_log = data
            self.recalculate_results()
            self.update_result_table()
            self.log_message(f"♻️ バックアップから {len(data)} 件を復元しました", ft.Colors.GREEN)
        except Exception as ex:
            self.log_message(f"❌ 復元エラー: {ex}", ft.Colors.RED)

    def recalculate_results(self):
        if not self.results_log: return
        
        valid_runs = [r for r in self.results_log if not r.get("is_mc")]
        global_top = min([r["time_float"] for r in valid_runs]) if valid_runs else None
        class_tops = {}
        for r in valid_runs:
            c = r["class"]
            if c not in class_tops or r["time_float"] < class_tops[c]:
                class_tops[c] = r["time_float"]

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

        best_valid_list = [r for r in best_runs_map.values() if not r.get("is_mc")]
        best_valid_list.sort(key=lambda x: x["time_float"])
        
        for i, r in enumerate(best_valid_list):
            r["overall_rank"] = i + 1
            
        for c in class_tops.keys():
            c_best = [r for r in best_valid_list if r["class"] == c]
            c_best.sort(key=lambda x: x["time_float"])
            for i, r in enumerate(c_best):
                r["class_rank"] = i + 1

        for r in best_runs_map.values():
            r["is_best"] = True

        self.update_result_table()

    # ====================================================================
    # エントリー・出走状態の管理 (ATOMS3 RFID / PC NFC 共通)
    #   pending_entry : タグを読んだがまだ出走していない「次走者」
    #   active_runners: 次走者 + コース上の選手 (古い順)
    #   次走者は SEQ_START(シグナル使用時) か STARTED(メイン基板の出走通知) で確定する。
    #   確定前に別のタグを読んだ場合は差し替え (基板側の次走者IDも上書きされるため)
    # ====================================================================
    def register_entry(self, tag_id, via):
        if not tag_id: return
        tag_id = str(tag_id).strip().upper()
        if tag_id == self.pending_entry: return          # 同じタグの読み直し
        if tag_id in self.active_runners: return         # コース上の選手のタグを拾った

        rider = self.rider_database.get(tag_id)
        label = f"No.{rider['bib']} {rider['name']} (ID:{tag_id})" if rider else f"ID:{tag_id}"

        if via == "NFC":
            # PC側NFCはハブ経由で全基板へ送る (ATOMS3のRFIDはハブが自分で送信済み)
            packet = f"{json.dumps({'type':'ENTRY', 'id':tag_id})}\n".encode()
            if self.ser and self.ser.is_open: self.ser.write(packet)
        if not rider:
            self.log_message(f"⚠️ 未登録タグ: {tag_id} (CSVに登録されていません。計測は継続)", ft.Colors.YELLOW)

        if self.pending_entry and self.pending_entry in self.active_runners:
            old = self.pending_entry
            self.active_runners[self.active_runners.index(old)] = tag_id
            old_info = self.rider_database.get(old, {"bib": "?", "name": old})
            self.log_message(f"🔁 次走者を差し替え: No.{old_info['bib']} {old_info['name']} → {label}", ft.Colors.ORANGE_200)
        else:
            self.active_runners.append(tag_id)
            self.log_message(f"📖 エントリー受付({via}): {label}", ft.Colors.GREEN_200)
        self.pending_entry = tag_id
        self.update_dashboard_counts()

    def on_seq_start(self):
        # シグナル開始: 次走者はこのシーケンスに確定 (基板側でIDが消費される)
        name = ""
        if self.pending_entry:
            info = self.rider_database.get(self.pending_entry, {"bib": "?", "name": self.pending_entry})
            name = f": No.{info['bib']} {info['name']}"
        self.pending_entry = None
        self.log_message(f"🚦 シグナル開始{name}", ft.Colors.GREEN_400)
        self.update_dashboard_counts()

    def on_started(self, rider_id):
        # メイン基板からの出走通知 (MULT/SOLO: スタート通過 / SYNC: 出走確認)
        rider_id = rider_id or "X999"
        if rider_id == self.pending_entry:
            self.pending_entry = None
        elif rider_id not in self.active_runners:
            self.active_runners.append(rider_id)   # エントリー無し(X999)や取りこぼし
        info = self.rider_database.get(rider_id, {"bib": "?", "name": "不明"})
        self.log_message(f"🏍️ 出走: No.{info['bib']} {info['name']} (ID:{rider_id})", ft.Colors.CYAN_100)
        self.update_dashboard_counts()

    def on_force_dnf(self):
        # 実際に外れる選手はメイン基板から届く DNF リザルトで確定させる
        self.log_message("🛑 リセット指示 (先頭選手のDNFを待ちます)", ft.Colors.ORANGE_400)

    def _remove_finished(self, rider_id):
        if rider_id in self.active_runners: self.active_runners.remove(rider_id)
        if rider_id == self.pending_entry: self.pending_entry = None

    # ====================================================================
    # 5. コアロジック（パケット解析・状態遷移）
    # ====================================================================
    def process_incoming_packet(self, json_str, source):
        try:
            data = json.loads(json_str)
            msg_type = data.get("type")
            raw_id = data.get("id")
            
            rider_id = raw_id or "X999"
            info = self.rider_database.get(rider_id, {"bib": "?", "name": "不明", "class": "-"})
            rider_name = f"No.{info['bib']} {info['name']}"
            current_time = time.time()
            
            if msg_type == "ENTRY":
                self.register_entry(raw_id, "RFID")
                return
            if msg_type == "STARTED":
                self.on_started(raw_id)
                return

            if msg_type == "SEQ_START":
                self.on_seq_start()
            elif msg_type == "FORCE_DNF":
                self.on_force_dnf()
            
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
                status = data.get("status", "OK")          # OK / DNF / DNS (メイン基板が付与)
                time_str = f"{run_time:.3f}"
                r_class = info.get("class", "-")
                
                # 重複パケット除去 (未登録選手 "?" は別人の可能性があるので除去しない)
                for res in self.results_log:
                    if info["bib"] != "?" and res["bib"] == info["bib"] and res["base_time"] == run_time:
                        if current_time - res.get("recv_time", 0) < 3.0: return
                
                # ★修正：センサー由来の通知（React/FLYING等）は memo_text に格納する
                memo_str = " / ".join(self.runner_notes.pop(rider_id, [])) or ""
                
                new_record = {
                    "bib": info["bib"], "name": info["name"], "class": r_class,
                    "base_time": run_time, "penalty": 0, "is_mc": False,
                    "time_float": run_time, "time_str": time_str,
                    "penalty_text": "", "memo_text": memo_str, # ★2つのテキスト欄に分離
                    "overall_rank": "-", "class_rank": "-", "top_ratio": "-", "class_ratio": "-",
                    "is_best": False, "recv_time": current_time,
                    "status": status, "tag_id": rider_id if rider_id in self.rider_database else None,
                    "penalty_items": [], "is_mc_manual": False,
                }
                self.refresh_record(new_record)
                # DNF / DNS は refresh_record で順位・タイム比の計算から除外済み
                if status != "OK":
                    self.results_log.append(new_record)
                    self.recalculate_results()
                    self.save_backup()
                    spoken = "未出走" if status == "DNS" else "ディーエヌエフ"
                    threading.Thread(target=self._speak, args=(f"ゼッケン{info['bib']}番、{spoken}です。",), daemon=True).start()
                    self.log_message(f"🏁 {status}: {rider_name}", ft.Colors.ORANGE_300)
                    self._remove_finished(rider_id)
                    self.update_dashboard_counts()
                    return

                self.results_log.append(new_record)
                self.recalculate_results()
                self.save_backup()
                
                sec = int(run_time)
                ms = int(round((run_time - sec) * 1000))
                top_ratio_str = new_record['top_ratio'].replace('%','') if new_record['top_ratio'] != '-' else '測定不能'
                class_ratio_str = new_record['class_ratio'].replace('%','') if new_record['class_ratio'] != '-' else '測定不能'
                
                speech_text = f"ゼッケン{info['bib']}番。タイム、{sec}秒{ms}。総合タイム比、{top_ratio_str}パーセント。クラスタイム比、{class_ratio_str}パーセントです。"

                threading.Thread(target=self._speak, args=(speech_text,), daemon=True).start()
                self.log_message(f"🏁 ゴール: {rider_name} [{time_str}s] 総合比 {new_record['top_ratio']} / ｸﾗｽ比 {new_record['class_ratio']}", ft.Colors.CYAN_200)
                
                self._remove_finished(rider_id)
                self.update_dashboard_counts()
                
        except Exception: pass

    # ====================================================================
    # 6. UIレンダリング・画面更新
    # ====================================================================
    def update_result_table(self):
        def create_table_frame():
            # ★カラムに「ペナルティ」を追加し、「備考」と分離
            return ft.DataTable(columns=[
                    ft.DataColumn(label=ft.Text("順位")), ft.DataColumn(label=ft.Text("ｸﾗｽ")), ft.DataColumn(label=ft.Text("ゼッケン")),
                    ft.DataColumn(label=ft.Text("名前")), ft.DataColumn(label=ft.Text("最終タイム")), ft.DataColumn(label=ft.Text("トップ比")),
                    ft.DataColumn(label=ft.Text("ｸﾗｽ比")), ft.DataColumn(label=ft.Text("ペナルティ")), ft.DataColumn(label=ft.Text("備考")), ft.DataColumn(label=ft.Text("編集")),
                ], rows=[])

        exist_classes = sorted(list(set([r["class"] for r in self.results_log])))
        tab_list = ["総合"] + exist_classes + ["全履歴"]
        new_tabs = []
        
        for tab_name in tab_list:
            table = create_table_frame()
            
            if tab_name == "全履歴":
                records = list(reversed(self.results_log))
            else:
                if tab_name == "総合": records = [r for r in self.results_log if r.get("is_best")]
                else: records = [r for r in self.results_log if r.get("is_best") and r["class"] == tab_name]
                records.sort(key=lambda x: (x.get("is_mc", False), x["time_float"]))

            for r in records:
                rank_str = str(r["overall_rank"]) if tab_name == "総合" else (str(r["class_rank"]) if tab_name != "全履歴" else "-")
                
                time_color = ft.Colors.PURPLE_400 if r.get("is_mc") else (ft.Colors.RED_400 if r["penalty"] > 0 else ft.Colors.WHITE)
                time_display = r["time_str"]   # MC / DNF / DNS / 数値
                
                # 自動検知されたFLYINGは備考欄で赤字に
                memo_color = ft.Colors.RED_400 if "FLYING" in r["memo_text"] else ft.Colors.WHITE
                memo_weight = ft.FontWeight.BOLD if "FLYING" in r["memo_text"] else ft.FontWeight.NORMAL
                
                table.rows.append(ft.DataRow(cells=[
                    ft.DataCell(ft.Text(rank_str)), ft.DataCell(ft.Text(r["class"])), ft.DataCell(ft.Text(r["bib"])), ft.DataCell(ft.Text(r["name"])),
                    ft.DataCell(ft.Text(time_display, weight=ft.FontWeight.BOLD, color=time_color)),
                    ft.DataCell(ft.Text(r["top_ratio"])), ft.DataCell(ft.Text(r["class_ratio"])),
                    # ペナルティと備考をそれぞれ独立したセルとして表示
                    ft.DataCell(ft.Text(r["penalty_text"], color=ft.Colors.RED_400)), 
                    ft.DataCell(ft.Text(r["memo_text"], color=memo_color, weight=memo_weight)),
                    ft.DataCell(ft.IconButton(icon=ft.Icons.ADD_ALERT, icon_size=20, icon_color=ft.Colors.ORANGE_400, on_click=lambda e, rec=r: self.open_penalty_dialog(rec)))
                ]))
            new_tabs.append(ft.Tab(text=tab_name, content=ft.Column([table], scroll=ft.ScrollMode.AUTO)))

        self.result_tabs.tabs = new_tabs
        self.page.update()

    # --- 以下、省略不可の定型処理 ---
    def write_results_csv(self, path):
        with open(path, "w", encoding="utf-8-sig", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["出走順", "ベストフラグ", "クラス", "ゼッケン", "名前", "ベースタイム", "ペナルティ加算秒", "最終タイム", "総合順位", "クラス順位", "トップ比", "クラス比", "ペナルティ内容", "備考"])
            for idx, r in enumerate(self.results_log):
                best_mark = "★" if r.get("is_best") else ""
                writer.writerow([idx + 1, best_mark, r["class"], r["bib"], r["name"], r["base_time"], r["penalty"], r["time_str"], r["overall_rank"], r["class_rank"], r["top_ratio"], r["class_ratio"], r["penalty_text"], r["memo_text"]])

    def on_save_csv_result(self, e: ft.FilePickerResultEvent):
        if e.path:
            try:
                self.write_results_csv(e.path)
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
            if tag.ndef and tag.ndef.records and isinstance(tag.ndef.records[0], ndef.TextRecord):
                tag_id = tag.ndef.records[0].text
            else:
                return True
        except:
            return True

        if tag_id.strip().upper() in self.rider_database:
            self.register_entry(tag_id, "NFC")
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
        on_course = [t for t in self.active_runners if t != self.pending_entry]
        self.runner_count_text.value = f"{len(on_course)} 台"
        self.active_runners_row.controls.clear()
        
        if self.active_runners:
            for tid in self.active_runners:
                info = self.rider_database.get(tid, {"bib": "?", "name": "不明"})
                is_next = (tid == self.pending_entry)
                chip = ft.Chip(
                    label=ft.Text(("次走 " if is_next else "") + f"No.{info.get('bib', '?')} {info.get('name', '不明')}", weight=ft.FontWeight.BOLD),
                    bgcolor=ft.Colors.BLUE_800 if is_next else ft.Colors.ORANGE_800,
                )
                self.active_runners_row.controls.append(chip)
        else:
            self.active_runners_row.controls.append(ft.Text("待機なし", size=24, weight=ft.FontWeight.BOLD, color=ft.Colors.ORANGE_400))
            
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
            if self.ser and self.ser.is_open:
                self.ser.close()   # 二重接続防止 (古い受信スレッドはこれで終了する)
            ser = serial.Serial()
            ser.port = self.drop_com.value
            ser.baudrate = 115200
            ser.timeout = 0.5
            ser.dtr = False        # 接続時にATOMS3がリセットされるのを防ぐ
            ser.rts = False
            ser.open()
            self.ser = ser
            self.log_message(f"✅ 接続成功: {self.drop_com.value}", ft.Colors.GREEN)
            threading.Thread(target=self.serial_listener, args=(ser,), daemon=True).start()
        except Exception as err:
            self.log_message(f"❌ シリアル接続エラー: {err}", ft.Colors.RED)

    def udp_listener(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind((UDP_IP, UDP_PORT))
            while True:
                data, _ = s.recvfrom(1024)
                self.process_incoming_packet(data.decode('utf-8', 'ignore').strip(), "UDP")
        except: pass

    def serial_listener(self, ser):
        while ser.is_open:
            try:
                if ser.in_waiting == 0:
                    time.sleep(0.01)   # 待機中にCPUを使い切らない
                    continue
                line = ser.readline().decode('utf-8', 'ignore').strip()
            except Exception as err:
                if ser is self.ser:
                    self.log_message(f"❌ シリアル切断: {err}", ft.Colors.RED)
                break
            if not line: continue

            # 基板からのデータを最優先で判定 (中身の文字列で誤反応しないように)
            if line.startswith("[ESP_DATA] "):
                self.process_incoming_packet(line[len("[ESP_DATA] "):], "SERIAL")
            elif line == "SEQ_START" or line == "[HUB_TX] SEQ_START":
                self.on_seq_start()
            elif line == "FORCE_DNF" or line == "[HUB_TX] FORCE_DNF":
                self.on_force_dnf()
            elif line.startswith("[ERROR]") or line.startswith("[HUB_READY]"):
                self.log_message(f"🔌 ハブ: {line}", ft.Colors.GREY_400)

    def nfc_listener(self):
        while True:
            try:
                with nfc.ContactlessFrontend('usb') as clf: clf.connect(rdwr={'on-connect': self.on_nfc_connect})
            except: time.sleep(2)

def main(page: ft.Page): MotoGymkhanaApp(page)
if __name__ == "__main__": ft.app(target=main)
