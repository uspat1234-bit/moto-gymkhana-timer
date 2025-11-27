import tkinter as tk
from tkinter import ttk
import threading
import sys
import socket
import json
import select
import time

import timing_core
import gpio_sensor

GUI_UPDATE_INTERVAL_MS = 100
UDP_PORT = 5005

class App(tk.Tk):
    def __init__(self, system: timing_core.TimingSystem):
        super().__init__()
        self.system = system
        self.title("Moto Gymkhana - NORMAL MODE (Pursuit Ready)")
        self.geometry("1000x600")
        self.protocol("WM_DELETE_WINDOW", self.on_closing)
        self.configure(bg='#2c3e50')

        # ソケット作成
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", UDP_PORT))
        self.sock.setblocking(0)

        self.var_last_read = tk.StringVar(value="Ready")

        # スタイル設定
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure('TFrame', background='#2c3e50')
        style.configure('TLabel', background='#2c3e50', foreground='#ecf0f1', font=('Helvetica', 14))
        style.configure('Header.TLabel', background='#333', foreground='#eee', font=('Helvetica', 12))

        self._setup_ui()
        self.after(GUI_UPDATE_INTERVAL_MS, self.update_gui)

    def _setup_ui(self):
        main_frame = ttk.Frame(self, padding="20")
        main_frame.pack(expand=True, fill='both')

        # === 左側: 計測情報 ===
        left_frame = ttk.Frame(main_frame, style='TFrame')
        left_frame.pack(side='left', fill='both', expand=True, padx=(0, 10))

        # タイム表示
        ttk.Label(left_frame, text="CURRENT TIME", style='TLabel').pack(anchor='w')
        self.lbl_time = tk.Label(left_frame, text="0.000", font=('Courier', 100, 'bold'), 
                                 bg='#2c3e50', fg='#f1c40f', width=7)
        self.lbl_time.pack(pady=20)

        # 走者情報
        self.lbl_name = tk.Label(left_frame, text="---", font=('Helvetica', 40, 'bold'), bg='#2c3e50', fg='white')
        self.lbl_name.pack(pady=(10, 0))
        
        # 車両情報
        self.lbl_bike = tk.Label(left_frame, text="", font=('Helvetica', 20), bg='#2c3e50', fg='#3498db')
        self.lbl_bike.pack(pady=(0, 5))

        # ID
        self.lbl_id = tk.Label(left_frame, text="", font=('Helvetica', 16), bg='#2c3e50', fg='gray')
        self.lbl_id.pack(pady=(0, 20))

        # センサー状態
        sensor_frame = ttk.Frame(left_frame, style='TFrame')
        sensor_frame.pack(fill='x', pady=10)
        self.d1_status = ttk.Label(sensor_frame, text="START", style='TLabel')
        self.d1_status.pack(side='left', padx=10)
        self.d2_status = ttk.Label(sensor_frame, text="STOP", style='TLabel')
        self.d2_status.pack(side='right', padx=10)
        
        # デバッグボタン
        btn_frame = ttk.Frame(left_frame, style='TFrame')
        btn_frame.pack(fill='x', pady=20)
        ttk.Button(btn_frame, text="強制スタート", command=self.force_start).pack(side='left', expand=True, padx=5)
        ttk.Button(btn_frame, text="強制ゴール", command=self.force_stop).pack(side='left', expand=True, padx=5)

        # === 右側: エントリーリスト ===
        right_frame = ttk.Frame(main_frame, style='TFrame')
        right_frame.pack(side='right', fill='both', expand=True)

        ttk.Label(right_frame, text="ENTRY LIST", style='Header.TLabel').pack(anchor='w')
        
        # リスト表示 (Treeview)
        columns = ('No', 'Name', 'Bike')
        self.tree = ttk.Treeview(right_frame, columns=columns, show='headings', height=15)
        self.tree.heading('No', text='#')
        self.tree.column('No', width=40, anchor='center')
        self.tree.heading('Name', text='Rider')
        self.tree.column('Name', width=150)
        self.tree.heading('Bike', text='Vehicle')
        self.tree.column('Bike', width=100)
        self.tree.pack(fill='both', expand=True)

        # 次走者
        self.lbl_next = tk.Label(right_frame, text="Next: (No Entry)", font=('Helvetica', 14), bg='#2c3e50', fg='#2ecc71')
        self.lbl_next.pack(pady=10, anchor='w')

        # ステータスバー
        ttk.Label(right_frame, textvariable=self.var_last_read, font=('Helvetica', 10), background='#2c3e50', foreground='gray').pack(anchor='w')

    def update_gui(self):
        try:
            with self.system.data_lock:
                data = self.system.gui_data

            # タイム更新
            self.lbl_time.config(text=data["first_run_elapsed"])
            if data["is_goal"]: self.lbl_time.config(fg='#e74c3c')
            else: self.lbl_time.config(fg='#f1c40f')

            # 走者情報
            self.lbl_name.config(text=data["current_runner_name"])
            self.lbl_bike.config(text=data.get("current_runner_bike", ""))
            if data["current_runner_id"]: self.lbl_id.config(text=f"ID: {data['current_runner_id']}")
            else: self.lbl_id.config(text="")

            # センサー
            self.d1_status.config(text=f"START: {data['d1_status']}")
            self.d2_status.config(text=f"STOP: {data['d2_status']}")

            # リスト更新
            if len(self.tree.get_children()) != len(data['queue_list']):
                self.tree.delete(*self.tree.get_children())
                for i, rider in enumerate(data['queue_list']):
                    self.tree.insert('', 'end', values=(i+1, rider['name'], rider.get('bike', '')))

            if len(data["queue_list"]) > 0:
                self.lbl_next.config(text=f"Next: {data['queue_list'][0]['name']}")
            else:
                self.lbl_next.config(text="Next: (No Entry)")

            # NFC受信
            ready = select.select([self.sock], [], [], 0.01)
            if ready[0]:
                data_bytes, addr = self.sock.recvfrom(1024)
                try:
                    msg = json.loads(data_bytes.decode('utf-8'))
                    if msg.get("type") == "ENTRY":
                        name = msg.get("name")
                        rid = msg.get("id")
                        bike = msg.get("bike", "")
                        print(f"NFC受信: {name}")
                        self.system.register_new_rider(name, rid, bike)
                        self.var_last_read.set(f"Entry: {name}")
                except: pass

        except Exception as e:
            print(f"GUI Error: {e}")

        self.after(GUI_UPDATE_INTERVAL_MS, self.update_gui)

    def force_start(self):
        """デバッグ用"""
        with self.system.data_lock:
            if self.system.queue:
                runner = self.system.queue.pop(0)
                runner['status'] = 'RUNNING'
                runner['start_time'] = time.time()
                self.system.on_course_runners.append(runner)
                self.system.current_runner = runner
                self.system.last_start_trigger_time = time.time()

    def force_stop(self):
        """デバッグ用"""
        with self.system.data_lock:
            if self.system.on_course_runners:
                runner = self.system.on_course_runners.pop(0)
                runner['status'] = 'GOAL'
                runner['result_time'] = time.time() - runner['start_time']
                self.system.current_runner = runner
                self.system.elapsed_time = runner['result_time']
                self.system.goal_hold_expire_time = time.time() + 5.0
                self.system.save_record(runner)

    def on_closing(self):
        self.system.stop()
        self.sock.close()
        self.destroy()

if __name__ == "__main__":
    # センサー初期化
    gpio_sensor.setup_gpio()
    
    # 通常モードでシステム起動
    system = timing_core.TimingSystem()
    system.mode = "NORMAL"
    
    # 監視スレッド
    t_sensor = threading.Thread(target=system.run_sensing_loop, daemon=True)
    t_sensor.start()

    app = App(system)
    try:
        app.mainloop()
    except KeyboardInterrupt:
        pass
    finally:
        system.stop()
        t_sensor.join(timeout=1.0)
        print("System Shutdown.")
