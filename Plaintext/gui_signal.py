import tkinter as tk
from tkinter import ttk
import threading
import sys
import socket
import json
import select
import time
import os
import pygame # サウンド用

# 専用ロジックをインポート
import timing_signal 
import gpio_sensor

GUI_UPDATE_INTERVAL_MS = 50
UDP_PORT = 5005

class SignalApp(tk.Tk):
    def __init__(self, system: timing_signal.SignalTimingSystem):
        super().__init__()
        self.system = system
        self.title("Moto Gymkhana - SIGNAL START MODE")
        self.geometry("1000x750")
        self.protocol("WM_DELETE_WINDOW", self.on_closing)
        self.configure(bg='#222')

        # --- サウンド初期化 ---
        try:
            pygame.mixer.init()
            # 音声ファイルがない場合はエラーになるのでtryで囲む
            self.sounds = {
                "beep": pygame.mixer.Sound("signal_beap.wav"),  # 赤・黄
                "go":   pygame.mixer.Sound("signal_go.wav"),    # 緑
                "false": pygame.mixer.Sound("signal_false.wav") # フライング
            }
        except:
            self.sounds = {}
        
        self.last_signal_stage = "IDLE"

        # --- ソケット初期化 ---
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", UDP_PORT))
        self.sock.setblocking(0)

        self.light_ids = []
        self.var_last_read = tk.StringVar(value="Ready")

        # --- スタイル設定 ---
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure('TFrame', background='#222')
        style.configure('TLabel', background='#222', foreground='#eee', font=('Helvetica', 14))
        style.configure('Header.TLabel', background='#333', foreground='#eee', font=('Helvetica', 12))
        # アクション指示用
        style.configure('Action.TLabel', background='#2c3e50', foreground='#3498db', font=('Helvetica', 20, 'bold'))
        style.configure('Wait.TLabel', background='#2c3e50', foreground='#f39c12', font=('Helvetica', 20, 'bold'))

        self._setup_ui()
        self.after(GUI_UPDATE_INTERVAL_MS, self.update_gui)

    def _setup_ui(self):
        main_frame = ttk.Frame(self, padding="20")
        main_frame.pack(expand=True, fill='both')

        # === 1. シグナルライト (Canvas) ===
        self.canvas = tk.Canvas(main_frame, width=600, height=150, bg='#111', highlightthickness=0)
        self.canvas.pack(pady=(10, 20))
        
        self.light_ids = [
            self.canvas.create_oval(50, 25, 150, 125, fill='#300', outline='#555', width=3),  # 赤
            self.canvas.create_oval(250, 25, 350, 125, fill='#330', outline='#555', width=3), # 黄
            self.canvas.create_oval(450, 25, 550, 125, fill='#030', outline='#555', width=3)  # 緑
        ]

        # === 2. タイム & リアクション ===
        info_frame = ttk.Frame(main_frame, style='TFrame')
        info_frame.pack(fill='x', pady=10)
        
        # タイム (等幅フォント)
        self.lbl_time = tk.Label(info_frame, text="0.000", font=('Courier', 100, 'bold'), 
                                 bg='#222', fg='#f1c40f', width=7)
        self.lbl_time.pack()
        
        # リアクションタイム
        self.lbl_rt = tk.Label(info_frame, text="RT: ---", font=('Helvetica', 24), bg='#222', fg='#95a5a6')
        self.lbl_rt.pack(pady=5)

        # === 3. 指示メッセージ (アクション) ===
        action_frame = ttk.Frame(main_frame, style='TFrame')
        action_frame.pack(fill='x', pady=10)
        self.lbl_action = ttk.Label(action_frame, text="", style='Action.TLabel', anchor='center')
        self.lbl_action.pack()

        # === 4. 走者情報 ===
        runner_frame = ttk.Frame(main_frame, style='TFrame')
        runner_frame.pack(fill='x', pady=10)
        
        # 名前
        self.lbl_name = tk.Label(runner_frame, text="---", font=('Helvetica', 40, 'bold'), bg='#222', fg='white')
        self.lbl_name.pack()
        
        # 車両情報
        self.lbl_bike = tk.Label(runner_frame, text="", font=('Helvetica', 20), bg='#222', fg='#3498db')
        self.lbl_bike.pack(pady=(0, 5))

        # ID
        self.lbl_id = tk.Label(runner_frame, text="", font=('Helvetica', 16), bg='#222', fg='gray')
        self.lbl_id.pack()

        # 次の走者
        self.lbl_next = tk.Label(runner_frame, text="Next: (No Entry)", font=('Helvetica', 16), bg='#222', fg='#2ecc71')
        self.lbl_next.pack(pady=10)

        # === 5. 操作ボタン ===
        ctrl_frame = ttk.Frame(main_frame, style='TFrame')
        ctrl_frame.pack(side='bottom', fill='x', pady=10)
        
        self.btn_start = tk.Button(ctrl_frame, text="🚦 SIGNAL START (Space)", font=('Helvetica', 14, 'bold'),
                                   bg='#3498db', fg='white', command=self.start_signal)
        self.btn_start.pack(fill='x', ipady=5, pady=5)
        
        # デバッグボタン
        debug_frame = ttk.Frame(ctrl_frame, style='TFrame')
        debug_frame.pack(fill='x')
        tk.Button(debug_frame, text="[Debug] Force Start", command=self.force_start, bg='#555', fg='white').pack(side='left', expand=True, padx=5)
        tk.Button(debug_frame, text="[Debug] Force Goal", command=self.force_stop, bg='#555', fg='white').pack(side='left', expand=True, padx=5)
        
        self.bind('<space>', lambda e: self.start_signal())

    def start_signal(self, force=False):
        """シグナル開始 (force=Trueなら待機時間無視)"""
        threading.Thread(target=self.system.start_signal_sequence, args=(force,), daemon=True).start()

    def play_sound(self, stage):
        """サウンド再生"""
        if not self.sounds: return
        try:
            if stage in ["RED", "YELLOW"]: self.sounds["beep"].play()
            elif stage == "GREEN": self.sounds["go"].play()
            elif stage == "FALSE": self.sounds["false"].play()
        except: pass

    def update_gui(self):
        try:
            with self.system.data_lock:
                data = self.system.gui_data

            # --- 1. シグナル点灯 & 音 ---
            stage = data["signal_stage"]
            if stage != self.last_signal_stage:
                self.play_sound(stage)
                self.last_signal_stage = stage

            c_r, c_y, c_g = '#300', '#330', '#030'
            if stage == "RED": c_r = '#ff0000'
            elif stage == "YELLOW": c_r, c_y = '#300', '#ffff00'
            elif stage == "GREEN": c_y, c_g = '#330', '#00ff00'
            elif stage == "FALSE": c_r, c_y, c_g = '#ff0000', '#ff0000', '#ff0000'

            self.canvas.itemconfig(self.light_ids[0], fill=c_r)
            self.canvas.itemconfig(self.light_ids[1], fill=c_y)
            self.canvas.itemconfig(self.light_ids[2], fill=c_g)

            # --- 2. タイム表示 ---
            self.lbl_time.config(text=data["first_run_elapsed"])
            
            if data["is_goal"]:
                self.lbl_time.config(fg='#e74c3c') # ゴール時 (赤)
            elif data["is_false"]:
                self.lbl_time.config(fg='#e74c3c') # フライング時 (赤)
            else:
                self.lbl_time.config(fg='#f1c40f') # 計測中 (黄)

            # リアクション / 警告
            if data["is_false"]:
                self.lbl_rt.config(text="⚠️ FALSE START (+Penalty) ⚠️", fg='#e74c3c')
            elif data["reaction_time"] != "---":
                # 反応が良いと色を変える演出
                rt_val = float(data["reaction_time"])
                rt_color = '#00ff00' if rt_val < 0.4 else ('#ffffff' if rt_val < 0.6 else '#95a5a6')
                self.lbl_rt.config(text=f"RT: {data['reaction_time']} s", fg=rt_color)
            else:
                self.lbl_rt.config(text="RT: ---", fg='gray')

            # --- 3. 走者情報 ---
            self.lbl_name.config(text=data["current_runner_name"])
            self.lbl_bike.config(text=data.get("current_runner_bike", "")) # 車両情報
            if data["current_runner_id"]:
                self.lbl_id.config(text=f"ID: {data['current_runner_id']}")
            else:
                self.lbl_id.config(text="")
            
            # 次走者 & 指示
            if len(data["queue_list"]) > 0:
                next_rider = data["queue_list"][0]
                self.lbl_next.config(text=f"Next: {next_rider['name']} (Queue: {data['queue_size']})")
                
                # アクション指示 (10秒ルール vs 2回目タッチ)
                current_time = time.time()
                last_start = self.system.last_valid_start_time
                interval = self.system.NEXT_START_INTERVAL
                time_diff = current_time - last_start
                is_safe = (time_diff >= interval)
                
                if stage != "IDLE":
                    self.lbl_action.config(text="GO GO GO!", style='Action.TLabel')
                elif not is_safe:
                    rem = int(interval - time_diff)
                    # 待機中でも2回目タッチで強制スタートできることを示唆
                    self.lbl_action.config(text=f"WAIT... ({rem}s) or Touch to START", style='Wait.TLabel')
                else:
                    self.lbl_action.config(text="📢 TOUCH AGAIN TO START! 📢", style='Action.TLabel')
            else:
                self.lbl_next.config(text="Next: (No Entry)")
                self.lbl_action.config(text="Please Touch (Entry)", style='TLabel')

            # --- 4. NFC受信 (2回目タッチ判定) ---
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
                        
                        # 2回目タッチなら即スタート(force=True)
                        with self.system.data_lock:
                            q = self.system.queue
                        
                        if q and q[0]['id'] == rid:
                            # シグナル停止中なら開始
                            if stage == "IDLE":
                                print("★2回目タッチ: 強制スタート")
                                self.start_signal(force=True)
                        else:
                            # 新規エントリー
                            self.system.register_new_rider(name, rid, bike)
                except: pass

        except Exception as e:
            print(f"GUI Error: {e}")

        self.after(GUI_UPDATE_INTERVAL_MS, self.update_gui)

    def force_start(self):
        """デバッグ用: 強制スタート"""
        with self.system.data_lock:
            if self.system.queue and self.system.current_runner is None:
                runner = self.system.queue.pop(0)
                runner['status'] = 'RUNNING'
                self.system.current_runner = runner
                self.system.start_time = time.time()
                self.system.last_start_trigger_time = time.time()
                self.system.signal_stage = "GREEN"
                self.system.signal_start_time = time.time()

    def force_stop(self):
        """デバッグ用: 強制ゴール"""
        with self.system.data_lock:
            if self.system.current_runner:
                self.system.current_runner['status'] = 'GOAL'
                self.system.elapsed_time = time.time() - self.system.start_time
                self.system.goal_hold_expire_time = time.time() + 5.0
                self.system.last_stop_trigger_time = time.time()
                self.system.save_record(self.system.current_runner)

    def on_closing(self):
        self.system.stop()
        self.sock.close()
        self.destroy()

if __name__ == "__main__":
    gpio_sensor.setup_gpio()
    
    # ★シグナル版システムを使用
    system = timing_signal.SignalTimingSystem()
    system.mode = "SIGNAL" 
    
    t_sensor = threading.Thread(target=system.run_sensing_loop, daemon=True)
    t_sensor.start()

    app = SignalApp(system)
    try:
        app.mainloop()
    except KeyboardInterrupt:
        pass
    finally:
        system.stop()
        t_sensor.join(timeout=1.0)
        print("System Shutdown.")