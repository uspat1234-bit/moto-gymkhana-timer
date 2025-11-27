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
        
        # 起動時にウィンドウを最前面に出す
        self.lift()
        self.focus_force()
        self.attributes('-topmost', True)
        self.after_idle(self.attributes, '-topmost', False)

        # パス設定 (exe化対応)
        if getattr(sys, 'frozen', False):
            self.base_dir = os.path.dirname(sys.executable)
        else:
            self.base_dir = os.path.dirname(os.path.abspath(__file__))

        print(f"作業フォルダ: {self.base_dir}")

        # プロセス管理リスト
        self.proc_nfc = None
        self.proc_upload = None
        self.proc_gui = None

        # スタイル設定
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('TButton', font=('Helvetica', 12), padding=10)
        style.configure('TLabel', background='#2c3e50', foreground='white', font=('Helvetica', 12))
        style.configure('Header.TLabel', background='#2c3e50', foreground='#f1c40f', font=('Helvetica', 16, 'bold'))

        # --- UI配置 ---
        ttk.Label(self, text="モトジムカーナ計測システム", style='Header.TLabel').pack(pady=(20, 10))
        ttk.Label(self, text="バックグラウンド処理", style='TLabel').pack(pady=5)

        # 状態表示
        self.lbl_status = ttk.Label(self, text="準備中...", style='TLabel', foreground='gray')
        self.lbl_status.pack(pady=5)

        # ボタン配置
        btn_frame = tk.Frame(self, bg='#2c3e50')
        btn_frame.pack(pady=20, fill='x', padx=50)

        self.btn_normal = ttk.Button(btn_frame, text="通常モード (Normal)", command=self.launch_normal)
        self.btn_normal.pack(fill='x', pady=5)

        self.btn_signal = ttk.Button(btn_frame, text="シグナルモード (Signal)", command=self.launch_signal)
        self.btn_signal.pack(fill='x', pady=5)

        # 終了ボタン
        ttk.Button(self, text="全システム終了", command=self.on_closing).pack(side='bottom', pady=20)

        # 自動起動
        self.start_background_services()
        self.protocol("WM_DELETE_WINDOW", self.on_closing)

    def get_path(self, filename):
        """ファイル名から絶対パスを作成 (binフォルダ優先探索)"""
        # 1. まず bin フォルダの中を探す (exe配布時の推奨構成)
        bin_path = os.path.join(self.base_dir, "bin", filename)
        if os.path.exists(bin_path):
            return bin_path
            
        # 2. なければルートを探す (開発環境など)
        root_path = os.path.join(self.base_dir, filename)
        if os.path.exists(root_path):
            return root_path
            
        # 見つからなくてもとりあえず返す(エラー表示用)
        return root_path

    def get_target_filename(self, name):
        """実行環境に合わせて .py か .exe かを判断する"""
        if getattr(sys, 'frozen', False):
            return name.replace(".py", ".exe")
        else:
            return name

    def start_background_services(self):
        """NFCリーダーとアップローダーを裏で起動"""
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
            messagebox.showerror("エラー", f"バックグラウンド処理の起動に失敗しました。\n{e}")

    def close_current_gui(self):
        """現在開いているGUIがあれば閉じる"""
        if self.proc_gui and self.proc_gui.poll() is None:
            self.proc_gui.terminate()
            self.proc_gui = None

    def launch_normal(self):
        """通常モード起動"""
        self.close_current_gui()
        
        target_gui = self.get_target_filename("gui_main.py")
        script = self.get_path(target_gui)
        
        if os.path.exists(script):
            if target_gui.endswith(".exe"):
                self.proc_gui = subprocess.Popen([script], cwd=self.base_dir)
            else:
                self.proc_gui = subprocess.Popen([sys.executable, script], cwd=self.base_dir)
        else:
            messagebox.showerror("エラー", f"ファイルが見つかりません:\n{script}")

    def launch_signal(self):
        """シグナルモード起動"""
        self.close_current_gui()
        
        target_gui = self.get_target_filename("gui_signal.py")
        script = self.get_path(target_gui)
        
        if os.path.exists(script):
            if target_gui.endswith(".exe"):
                self.proc_gui = subprocess.Popen([script], cwd=self.base_dir)
            else:
                self.proc_gui = subprocess.Popen([sys.executable, script], cwd=self.base_dir)
        else:
            messagebox.showerror("エラー", f"ファイルが見つかりません:\n{script}")

    def on_closing(self):
        """全プロセスを道連れにして終了"""
        if messagebox.askokcancel("終了", "システムを終了しますか？\n(NFCやアップロードも停止します)"):
            if self.proc_gui: self.proc_gui.terminate()
            if self.proc_nfc: self.proc_nfc.terminate()
            if self.proc_upload: self.proc_upload.terminate()
            self.destroy()

if __name__ == "__main__":
    app = LauncherApp()
    app.mainloop()