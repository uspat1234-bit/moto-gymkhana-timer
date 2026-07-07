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

# NFCライブラリの読み込み
try:
    import nfc
    import ndef
    NFC_AVAILABLE = True
except ImportError:
    NFC_AVAILABLE = False

# 有線UDP通信設定
UDP_IP = "0.0.0.0"
UDP_PORT = 5055

class MotoGymkhanaApp:
    # ====================================================================
    # 2. アプリケーション初期化・ステート定義
    # ====================================================================
    def __init__(self, page: ft.Page):
        self.page = page
        self.page.title = "MGTS - 総合データ管理窓口"
        self.page.theme_mode = ft.ThemeMode.DARK
        self.page.padding = 0
        
        # 内部ステート管理
        self.ser = None
        self.rider_database = {}    # 選手マスタ (辞書)
        self.active_runners = []    # 待機・出走中ランナーのタグIDリスト
        self.runner_notes = {}      # ペナルティ・備考の一時保管
        self.results_log = []       # 確定したリザルトログ
        self.is_nfc_locked = False  # NFC連続読み込み防止用ロックフラグ
        
        # ダイアログ初期化
        self.file_picker = ft.FilePicker(on_result=self.on_csv_selected)
        self.save_file_picker = ft.FilePicker(on_result=self.on_save_csv_result)
        self.page.overlay.extend([self.file_picker, self.save_file_picker])
        
        # UI構築とスレッド起動
        self.init_ui_components()
        self.build_layout()
        
        threading.Thread(target=self.udp_listener, daemon=True).start()
        if NFC_AVAILABLE:
            threading.Thread(target=self.nfc_listener, daemon=True).start()
        else:
            self.log_message("⚠️ nfcpy未検出: NFCリーダーが接続されていません", ft.Colors.YELLOW)

    # ====================================================================
    # 3. UIコンポーネント構築・レイアウト定義
    # ====================================================================
    def init_ui_components(self):
        # ダッシュボード系
        self.runner_count_text = ft.Text("0 台", size=30, weight=ft.FontWeight.BOLD, color=ft.Colors.CYAN_400)
        self.active_runners_row = ft.Row(wrap=True)
        self.result_table = ft.DataTable(
            columns=[
                ft.DataColumn(label=ft.Text("出走順")),
                ft.DataColumn(label=ft.Text("ゼッケン")),
                ft.DataColumn(label=ft.Text("名前")),
                ft.DataColumn(label=ft.Text("タイム")),
                ft.DataColumn(label=ft.Text("ペナルティ/備考")),
            ],
            rows=[]
        )
        self.btn_export_csv = ft.ElevatedButton("リザルトをCSV保存", icon=ft.Icons.DOWNLOAD, on_click=lambda _: self.save_file_picker.save_file(allowed_extensions=["csv"], file_name="mgts_results.csv"), color=ft.Colors.WHITE, bgcolor=ft.Colors.BLUE_700)
        
        # マスタ管理系 (CSVインポートのみに機能特化)
        self.btn_import_csv = ft.ElevatedButton("名簿CSVを一括読込", icon=ft.Icons.UPLOAD_FILE, on_click=lambda _: self.file_picker.pick_files(allowed_extensions=["csv"], allow_multiple=False), color=ft.Colors.WHITE, bgcolor=ft.Colors.GREEN_700)
        self.rider_table = ft.DataTable(
            columns=[
                ft.DataColumn(label=ft.Text("タグID")),
                ft.DataColumn(label=ft.Text("ゼッケン")),
                ft.DataColumn(label=ft.Text("選手名")),
                ft.DataColumn(label=ft.Text("クラス")),
            ],
            rows=[]
        )

        # システム設定・ログ系
        self.drop_com = ft.Dropdown(label="COMポート", width=200, options=[])
        self.btn_connect_ser = ft.ElevatedButton("接続", icon=ft.Icons.CABLE, on_click=self.connect_serial)
        self.log_box = ft.ListView(expand=True, spacing=5, auto_scroll=True)
        self.refresh_com_ports()

    def build_layout(self):
        # 画面1: 計測ダッシュボード
        self.timing_view = ft.Container(
            expand=True, padding=20,
            content=ft.Column([
                ft.Text("⏱️ 計測ダッシュボード", size=30, weight=ft.FontWeight.BOLD),
                ft.Card(content=ft.Container(padding=20, content=ft.Row([
                    ft.Column([ft.Text("現在コース上の台数", color=ft.Colors.GREY_400), self.runner_count_text], expand=1),
                    ft.Column([ft.Text("出走中 ➡ スターティング", color=ft.Colors.GREY_400), self.active_runners_row], expand=5),
                ]))),
                ft.Divider(),
                ft.Row([ft.Text("📊 リザルト一覧", size=20, weight=ft.FontWeight.BOLD), self.btn_export_csv], alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
                ft.Column([self.result_table], expand=True, scroll=ft.ScrollMode.AUTO)
            ])
        )

        # 画面2: 選手・タグマスタ
        self.nfc_view = ft.Container(
            expand=True, padding=20, visible=False,
            content=ft.Column([
                ft.Text("🏍️ 選手名簿マスタ", size=30, weight=ft.FontWeight.BOLD),
                ft.Row([self.btn_import_csv]),
                ft.Divider(),
                ft.Text("読み込み済みデータ", size=20, weight=ft.FontWeight.BOLD),
                ft.Column([self.rider_table], expand=True, scroll=ft.ScrollMode.AUTO)
            ])
        )

        # 画面3: システム・通信ログ
        self.system_view = ft.Container(
            expand=True, padding=20, visible=False,
            content=ft.Column([
                ft.Text("⚙️ システムログ", size=30, weight=ft.FontWeight.BOLD),
                ft.Row([self.drop_com, self.btn_connect_ser, ft.IconButton(icon=ft.Icons.REFRESH, on_click=lambda e: self.refresh_com_ports())]),
                ft.Divider(),
                ft.Container(bgcolor=ft.Colors.BLACK87, padding=10, border_radius=5, expand=True, content=self.log_box)
            ])
        )

        # ナビゲーションメニュー
        self.views = [self.timing_view, self.nfc_view, self.system_view]
        self.nav_rail = ft.NavigationRail(
            selected_index=0,
            label_type=ft.NavigationRailLabelType.ALL,
            min_width=100,
            group_alignment=-0.9,
            destinations=[
                ft.NavigationRailDestination(icon=ft.Icons.TIMER, selected_icon=ft.Icons.TIMER, label="計測"),
                ft.NavigationRailDestination(icon=ft.Icons.PEOPLE, selected_icon=ft.Icons.PEOPLE, label="名簿"),
                ft.NavigationRailDestination(icon=ft.Icons.SETTINGS, selected_icon=ft.Icons.SETTINGS, label="ログ"),
            ],
            on_change=self.handle_nav_change,
        )

        self.page.add(ft.Row(controls=[self.nav_rail, ft.VerticalDivider(width=1), ft.Stack(controls=self.views, expand=True)], expand=True))

    # ====================================================================
    # 4. ファイルI/O・CSVマスタ管理
    # ====================================================================
    def on_save_csv_result(self, e: ft.FilePickerResultEvent):
        if e.path:
            try:
                with open(e.path, "w", encoding="utf-8-sig", newline="") as f:
                    writer = csv.writer(f)
                    writer.writerow(["出走順", "ゼッケン", "名前", "タイム", "ペナルティ/備考"])
                    for idx, res in enumerate(self.results_log):
                        writer.writerow([idx + 1, res["bib"], res["name"], res["time"], res["note"]])
                self.log_message(f"💾 リザルト出力完了: {e.path}", ft.Colors.GREEN)
            except Exception as ex:
                self.log_message(f"❌ 出力エラー: {ex}", ft.Colors.RED)

    def on_csv_selected(self, e: ft.FilePickerResultEvent):
        if e.files and len(e.files) > 0:
            file_path = e.files[0].path
            try:
                try:
                    with open(file_path, mode='r', encoding='utf-8-sig') as f: lines = f.readlines()
                except UnicodeDecodeError:
                    with open(file_path, mode='r', encoding='shift_jis') as f: lines = f.readlines()
                self._parse_csv(lines)
            except Exception as ex:
                self.log_message(f"❌ 読み込みエラー: {ex}", ft.Colors.RED)

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
            
            errors = []
            if not re.match(r'^[A-Z0-9]+$', tag_id): errors.append("ID不正")
            if not bib.isdigit(): errors.append("ゼッケン非数字")
            if not name: errors.append("名前空欄")

            if errors:
                error_count += 1
                continue

            self.rider_database[tag_id] = {"bib": bib, "name": name, "class": r_class}
            count += 1
            
        self.update_rider_table()
        msg = f"📁 名簿読込完了: {count}名登録"
        if error_count > 0: msg += f" (エラー: {error_count}件)"
        self.log_message(msg, ft.Colors.GREEN if count > 0 else ft.Colors.RED)

    # ====================================================================
    # 5. コアロジック（パケット解析・状態遷移）
    # ====================================================================
    def process_incoming_packet(self, json_str, source):
        try:
            data = json.loads(json_str)
            msg_type = data.get("type")
            raw_id = data.get("id")
            
            # 生のJSON垂れ流しをやめ、意味のあるメッセージへ成型
            rider_id = raw_id if raw_id and raw_id != "X999" else (self.active_runners[0] if self.active_runners else "X999")
            info = self.rider_database.get(rider_id, {"bib": "?", "name": "不明", "class": "-"})
            rider_name = f"No.{info['bib']} {info['name']}"
            
            if msg_type in ["SEQ_START", "FORCE_DNF"]:
                self.is_nfc_locked = False
                if msg_type == "SEQ_START":
                    self.log_message(f"🚦 シグナル開始 (NFCロック解除)", ft.Colors.GREEN_400)
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
                run_time = data.get("time", 999.999)
                time_str = f"{run_time:.3f}"
                
                for res in self.results_log:
                    if res["bib"] == info["bib"] and res["time"] == time_str: return
                
                note_str = " / ".join(self.runner_notes.pop(rider_id, [])) or "-"
                self.results_log.append({"bib": info["bib"], "name": info["name"], "time": time_str, "note": note_str})
                
                # 成型したゴールログ
                self.log_message(f"🏁 ゴール確定: {rider_name} [タイム: {time_str}s] 備考: {note_str}", ft.Colors.CYAN_200)
                
                # リザルトの自動バックアップ処理
                csv_file = "mgts_results_auto.csv"
                write_header = not os.path.exists(csv_file)
                try:
                    with open(csv_file, "a", encoding="utf-8-sig", newline="") as f:
                        writer = csv.writer(f)
                        if write_header:
                            writer.writerow(["出走順", "ゼッケン", "名前", "タイム", "ペナルティ/備考"])
                        writer.writerow([len(self.results_log), info["bib"], info["name"], time_str, note_str])
                except: pass
                
                if rider_id in self.active_runners: self.active_runners.remove(rider_id)
                self.update_result_table()
                self.update_dashboard_counts()
        except json.JSONDecodeError:
            pass # JSONパースエラー時は無視 (生のシリアルログ等を弾く)
        except Exception:
            pass

    # ====================================================================
    # 6. NFCイベント制御
    # ====================================================================
    def on_nfc_connect(self, tag):
        try:
            if tag.ndef and tag.ndef.records and isinstance(tag.ndef.records[0], ndef.TextRecord):
                tag_id = tag.ndef.records[0].text
            else:
                return True
        except:
            return True

        if self.is_nfc_locked:
            self.log_message("🔒 ロック中: 前の選手がスタートするまでタッチ不可", ft.Colors.RED)
            self.page.update()
            return True

        if tag_id in self.rider_database:
            rider = self.rider_database[tag_id]
            self.log_message(f"📖 エントリー受付: No.{rider['bib']} {rider['name']} (ID:{tag_id})", ft.Colors.GREEN_200)

            packet = f"{json.dumps({'type':'ENTRY', 'id':tag_id})}\n".encode()
            
            # ハードウェアへの送信
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

    # ====================================================================
    # 7. UIレンダリング・画面更新
    # ====================================================================
    def log_message(self, msg, color=ft.Colors.WHITE70):
        timestamp = time.strftime("[%H:%M:%S] ")
        self.log_box.controls.append(ft.Text(timestamp + msg, color=color))
        self.page.update()

    def update_rider_table(self):
        self.rider_table.rows = [
            ft.DataRow(cells=[
                ft.DataCell(ft.Text(tid)),
                ft.DataCell(ft.Text(i.get("bib", ""))),
                ft.DataCell(ft.Text(i.get("name", ""))),
                ft.DataCell(ft.Text(i.get("class", "")))
            ]) for tid, i in self.rider_database.items()
        ]
        self.page.update()

    def update_result_table(self):
        self.result_table.rows = [ft.DataRow(cells=[ft.DataCell(ft.Text(str(i+1))), ft.DataCell(ft.Text(r["bib"])), ft.DataCell(ft.Text(r["name"])), ft.DataCell(ft.Text(r["time"])), ft.DataCell(ft.Text(r["note"]))]) for i, r in enumerate(self.results_log)]
        self.page.update()

    def update_dashboard_counts(self):
        self.runner_count_text.value = f"{len(self.active_runners)} 台"
        self.active_runners_row.controls.clear()
        
        if self.active_runners:
            for tid in self.active_runners:
                info = self.rider_database.get(tid, {"bib": "?", "name": "不明"})
                chip = ft.Chip(
                    label=ft.Text(f"No.{info.get('bib', '?')} {info.get('name', '不明')}", weight=ft.FontWeight.BOLD),
                    bgcolor=ft.Colors.ORANGE_800,
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

    # ====================================================================
    # 8. バックグラウンド通信リスナー (UDP/Serial/NFC)
    # ====================================================================
    def connect_serial(self, e):
        try:
            self.ser = serial.Serial(self.drop_com.value, 115200, timeout=0.5)
            self.log_message(f"✅ 接続成功: {self.drop_com.value}", ft.Colors.GREEN)
            threading.Thread(target=self.serial_listener, daemon=True).start()
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

    def serial_listener(self):
        while self.ser and self.ser.is_open:
            if self.ser.in_waiting > 0:
                line = self.ser.readline().decode('utf-8', 'ignore').strip()
                
                # シリアル経由での即時ロック解除イベント
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

# ====================================================================
# 9. メインエントリーポイント
# ====================================================================
def main(page: ft.Page): MotoGymkhanaApp(page)

if __name__ == "__main__": ft.app(target=main)
