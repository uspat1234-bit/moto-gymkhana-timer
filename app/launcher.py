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
        self.geometry("400x420")
        self.configure(bg='#2c3e50')
        
        self.lift()
        self.focus_force()
        self.attributes('-topmost', True)
        self.after_idle(self.attributes, '-topmost', False)

        # パス設定
        if getattr(sys, 'frozen', False):
            self.base_dir = os.path.dirname(sys.executable)
        else:
            self.base_dir = os.path.dirname(os.path.abspath(__file__))

        print(f"作業フォルダ: {self.base_dir}")

        self.proc_nfc = None
        self.proc_upload = None
        self.proc_gui = None
        self.proc_writer = None

        style = ttk.Style()
        style.theme_use('clam')
        style.configure('TButton', font=('Helvetica', 12), padding=10)
        style.configure('TLabel', background='#2c3e50', foreground='white', font=('Helvetica', 12))
        style.configure('Header.TLabel', background='#2c3e50', foreground='#f1c40f', font=('Helvetica', 16, 'bold'))

        # --- UI ---
        ttk.Label(self, text="モトジムカーナ計測システム", style='Header.TLabel').pack(pady=(20, 10))
        ttk.Label(self, text="バックグラウンド処理", style='TLabel').pack(pady=5)

        self.lbl_status = ttk.Label(self, text="準備中...", style='TLabel', foreground='gray')
        self.lbl_status.pack(pady=5)

        btn_frame = tk.Frame(self, bg='#2c3e50')
        btn_frame.pack(pady=10, fill='x', padx=50)

        self.btn_normal = ttk.Button(btn_frame, text="通常モード (Normal)", command=self.launch_normal)
        self.btn_normal.pack(fill='x', pady=5)

        self.btn_signal = ttk.Button(btn_frame, text="シグナルモード (Signal)", command=self.launch_signal)
        self.btn_signal.pack(fill='x', pady=5)

        self.btn_writer = ttk.Button(btn_frame, text="NFCタグ作成 (Writer)", command=self.launch_writer)
        self.btn_writer.pack(fill='x', pady=5)

        ttk.Button(self, text="全システム終了", command=self.on_closing).pack(side='bottom', pady=20)

        self.start_background_services()
        self.protocol("WM_DELETE_WINDOW", self.on_closing)

    def get_path(self, filename):
        """ファイル名から絶対パスを作成"""
        # 1. 自分と同じフォルダ (bin) を探す (最優先)
        # ランチャー自体が bin に入るので、隣を探せば見つかるはず
        same_dir_path = os.path.join(self.base_dir, filename)
        if os.path.exists(same_dir_path):
            return same_dir_path
        
        # 2. bin フォルダを探す (開発環境や、ランチャーだけルートにある場合用)
        bin_path = os.path.join(self.base_dir, "bin", filename)
        if os.path.exists(bin_path):
            return bin_path
            
        # 見つからなくてもとりあえず返す
        return same_dir_path

    def get_target_filename(self, name):
        if getattr(sys, 'frozen', False):
            return name.replace(".py", ".exe")
        else:
            return name

    def start_background_services(self):
        try:
            # 1. NFCリーダー
            target_nfc = self.get_target_filename("remote_entry.py")
            script_nfc = self.get_path(target_nfc)
            
            if os.path.exists(script_nfc):
                if target_nfc.endswith(".exe"):
                    self.proc_nfc = subprocess.Popen(
                        [script_nfc], 
                        creationflags=subprocess.CREATE_NEW_CONSOLE,
                        cwd=self.base_dir
                    )
                else:
                    self.proc_nfc = subprocess.Popen(
                        [sys.executable, script_nfc], 
                        creationflags=subprocess.CREATE_NEW_CONSOLE,
                        cwd=self.base_dir
                    )
            else:
                print(f"見つかりません: {script_nfc}")

            # 2. アップローダー
            target_upload = self.get_target_filename("drive_uploader.py")
            script_upload = self.get_path(target_upload)
            
            if os.path.exists(script_upload):
                if target_upload.endswith(".exe"):
                    self.proc_upload = subprocess.Popen(
                        [script_upload], 
                        creationflags=subprocess.CREATE_NEW_CONSOLE,
                        cwd=self.base_dir
                    )
                else:
                    self.proc_upload = subprocess.Popen(
                        [sys.executable, script_upload], 
                        creationflags=subprocess.CREATE_NEW_CONSOLE,
                        cwd=self.base_dir
                    )
            else:
                print(f"見つかりません: {script_upload}")
            
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
        target_gui = self.get_target_filename("gui_main.py")
        script = self.get_path(target_gui)
        
        if os.path.exists(script):
            if target_gui.endswith(".exe"):
                self.proc_gui = subprocess.Popen([script], cwd=self.base_dir)
            else:
                self.proc_gui = subprocess.Popen([sys.executable, script], cwd=self.base_dir)
        else:
            messagebox.showerror("エラー", f"ファイルなし:\n{script}")

    def launch_signal(self):
        self.close_current_gui()
        target_gui = self.get_target_filename("gui_signal.py")
        script = self.get_path(target_gui)
        
        if os.path.exists(script):
            if target_gui.endswith(".exe"):
                self.proc_gui = subprocess.Popen([script], cwd=self.base_dir)
            else:
                self.proc_gui = subprocess.Popen([sys.executable, script], cwd=self.base_dir)
        else:
            messagebox.showerror("エラー", f"ファイルなし:\n{script}")

    def launch_writer(self):
        if self.proc_writer and self.proc_writer.poll() is None:
            return

        target_writer = self.get_target_filename("writer.py")
        script = self.get_path(target_writer)
        
        if os.path.exists(script):
            if target_writer.endswith(".exe"):
                self.proc_writer = subprocess.Popen(
                    [script], 
                    creationflags=subprocess.CREATE_NEW_CONSOLE,
                    cwd=self.base_dir
                )
            else:
                self.proc_writer = subprocess.Popen(
                    [sys.executable, script], 
                    creationflags=subprocess.CREATE_NEW_CONSOLE,
                    cwd=self.base_dir
                )
        else:
            messagebox.showerror("エラー", f"ファイルなし:\n{script}")

    def on_closing(self):
        if messagebox.askokcancel("終了", "システムを終了しますか？"):
            if self.proc_gui: self.proc_gui.terminate()
            if self.proc_nfc: self.proc_nfc.terminate()
            if self.proc_upload: self.proc_upload.terminate()
            if self.proc_writer: self.proc_writer.terminate()
            self.destroy()

if __name__ == "__main__":
    app = LauncherApp()
    app.mainloop()
