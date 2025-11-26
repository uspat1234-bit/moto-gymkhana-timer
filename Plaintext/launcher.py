import tkinter as tk
from tkinter import ttk
from tkinter import messagebox
import subprocess
import sys
import os
import time

class LauncherApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Gymkhana System Launcher")
        self.geometry("400x350")
        self.configure(bg='#2c3e50')
        
        # ★追加: 起動時にウィンドウを強制的に最前面に出す処理
        self.lift()
        self.focus_force()
        self.attributes('-topmost', True)  # 一瞬だけ「常に手前」にする
        self.after_idle(self.attributes, '-topmost', False) # すぐに解除（操作できるように）

        # パス設定
        if getattr(sys, 'frozen', False):
            self.base_dir = os.path.dirname(sys.executable)
        else:
            self.base_dir = os.path.dirname(os.path.abspath(__file__))

        print(f"作業フォルダ: {self.base_dir}")

        # プロセス管理
        self.proc_nfc = None
        self.proc_upload = None
        self.proc_gui = None

        # スタイル
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('TButton', font=('Helvetica', 12), padding=10)
        style.configure('TLabel', background='#2c3e50', foreground='white', font=('Helvetica', 12))
        style.configure('Header.TLabel', background='#2c3e50', foreground='#f1c40f', font=('Helvetica', 16, 'bold'))

        # --- UI配置 ---
        ttk.Label(self, text="モトジムカーナ計測システム", style='Header.TLabel').pack(pady=(20, 10))
        ttk.Label(self, text="バックグラウンド処理", style='TLabel').pack(pady=5)

        self.lbl_status = ttk.Label(self, text="準備中...", style='TLabel', foreground='gray')
        self.lbl_status.pack(pady=5)

        # ボタン
        btn_frame = tk.Frame(self, bg='#2c3e50')
        btn_frame.pack(pady=20, fill='x', padx=50)

        self.btn_normal = ttk.Button(btn_frame, text="通常モード (Normal)", command=self.launch_normal)
        self.btn_normal.pack(fill='x', pady=5)

        self.btn_signal = ttk.Button(btn_frame, text="シグナルモード (Signal)", command=self.launch_signal)
        self.btn_signal.pack(fill='x', pady=5)

        ttk.Button(self, text="全システム終了", command=self.on_closing).pack(side='bottom', pady=20)

        # 自動起動
        self.start_background_services()
        self.protocol("WM_DELETE_WINDOW", self.on_closing)

    def get_path(self, filename):
        return os.path.join(self.base_dir, filename)

    def start_background_services(self):
        try:
            py_exe = sys.executable
            
            # NFCリーダー
            script_nfc = self.get_path("remote_entry.py")
            if os.path.exists(script_nfc):
                self.proc_nfc = subprocess.Popen(
                    [py_exe, script_nfc], 
                    creationflags=subprocess.CREATE_NEW_CONSOLE,
                    cwd=self.base_dir
                )
            else:
                print(f"見つかりません: {script_nfc}")

            # アップローダー
            script_upload = self.get_path("drive_uploader.py")
            if os.path.exists(script_upload):
                self.proc_upload = subprocess.Popen(
                    [py_exe, script_upload], 
                    creationflags=subprocess.CREATE_NEW_CONSOLE,
                    cwd=self.base_dir
                )
            else:
                print(f"見つかりません: {script_upload}")
            
            # 1行で記述
            self.lbl_status.config(text="● NFC & Upload 稼働中", foreground='#2ecc71')
            
        except Exception as e:
            self.lbl_status.config(text=f"起動エラー: {e}", foreground='red')
            messagebox.showerror("エラー", f"バックグラウンド処理の起動失敗\n{e}")

    def close_current_gui(self):
        if self.proc_gui and self.proc_gui.poll() is None:
            self.proc_gui.terminate()
            self.proc_gui = None

    def launch_normal(self):
        self.close_current_gui()
        py_exe = sys.executable
        script = self.get_path("gui_main.py")
        if os.path.exists(script):
            self.proc_gui = subprocess.Popen([py_exe, script], cwd=self.base_dir)
        else:
            messagebox.showerror("エラー", f"ファイルなし:\n{script}")

    def launch_signal(self):
        self.close_current_gui()
        py_exe = sys.executable
        script = self.get_path("gui_signal.py")
        if os.path.exists(script):
            self.proc_gui = subprocess.Popen([py_exe, script], cwd=self.base_dir)
        else:
            messagebox.showerror("エラー", f"ファイルなし:\n{script}")

    def on_closing(self):
        if messagebox.askokcancel("終了", "システムを終了しますか？"):
            if self.proc_gui: self.proc_gui.terminate()
            if self.proc_nfc: self.proc_nfc.terminate()
            if self.proc_upload: self.proc_upload.terminate()
            self.destroy()

if __name__ == "__main__":
    app = LauncherApp()
    app.mainloop()