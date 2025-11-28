import os
import time
import sys
import json
import webbrowser
import configparser
import tkinter as tk
from tkinter import simpledialog, messagebox

from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload
from google_auth_oauthlib.flow import InstalledAppFlow
from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials

# --- パス設定 ---
if getattr(sys, 'frozen', False):
    BASE_DIR = os.path.dirname(sys.executable)
else:
    BASE_DIR = os.path.dirname(os.path.abspath(__file__))

# config.ini のパス探索 (ルート優先)
CONFIG_FILE_BIN = os.path.join(BASE_DIR, "config.ini")
CONFIG_FILE_ROOT = os.path.join(os.path.dirname(BASE_DIR), "config.ini")

if os.path.exists(CONFIG_FILE_ROOT):
    CONFIG_FILE = CONFIG_FILE_ROOT
else:
    CONFIG_FILE = CONFIG_FILE_BIN 

TOKEN_FILE = os.path.join(BASE_DIR, "token.json")
HISTORY_FILE = os.path.join(BASE_DIR, "uploaded_history.txt")

# ★変更: client_secret.json を外部ファイルとして探す
# binフォルダ内、またはルートフォルダ内を探す
CLIENT_SECRET_BIN = os.path.join(BASE_DIR, "client_secret.json")
CLIENT_SECRET_ROOT = os.path.join(os.path.dirname(BASE_DIR), "client_secret.json")

if os.path.exists(CLIENT_SECRET_ROOT):
    CLIENT_SECRET_FILE = CLIENT_SECRET_ROOT
else:
    CLIENT_SECRET_FILE = CLIENT_SECRET_BIN

SCOPES = ['https://www.googleapis.com/auth/drive.file']

def load_settings():
    config = configparser.ConfigParser()
    
    # デフォルト設定
    default_watch_dir = os.path.join(os.path.dirname(CONFIG_FILE), "gymkhana_data")
    
    settings = {
        "folder_id": "",
        "watch_dir": default_watch_dir
    }

    if os.path.exists(CONFIG_FILE):
        try:
            config.read(CONFIG_FILE, encoding='utf-8')
            if "GoogleDrive" in config and "FolderID" in config["GoogleDrive"]:
                settings["folder_id"] = config["GoogleDrive"]["FolderID"].strip()
            if "System" in config and "DataDir" in config["System"]:
                dir_name = config["System"]["DataDir"].strip()
                settings["watch_dir"] = os.path.join(os.path.dirname(CONFIG_FILE), dir_name)
        except:
            pass
    
    return settings, config

def save_settings(config, folder_id):
    if not config.has_section("GoogleDrive"):
        config.add_section("GoogleDrive")
    config["GoogleDrive"]["FolderID"] = folder_id
    
    if not config.has_section("System"):
        config.add_section("System")
        config["System"]["DataDir"] = "gymkhana_data"

    try:
        with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
            config.write(f)
        return True
    except Exception as e:
        print(f"設定保存エラー: {e}")
        return False

def get_folder_id_gui():
    root = tk.Tk()
    root.withdraw()
    root.attributes('-topmost', True)

    while True:
        folder_id = simpledialog.askstring(
            "初期設定 (Google Drive)", 
            "アップロード先のGoogle DriveフォルダIDを入力してください。\n\n"
            "※ブラウザのURL末尾 folders/ の後ろの文字列です。",
            parent=root
        )
        
        if folder_id and folder_id.strip():
            root.destroy()
            return folder_id.strip()
        
        if messagebox.askyesno("終了確認", "IDが入力されていません。\nシステムを終了しますか？", parent=root):
            root.destroy()
            sys.exit(0)

def get_drive_service():
    """Google Drive API認証"""
    
    # ★追加: client_secret.json の存在チェック
    if not os.path.exists(CLIENT_SECRET_FILE):
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror(
            "設定エラー",
            "認証ファイル (client_secret.json) が見つかりません。\n\n"
            "Google Cloud Consoleからダウンロードしたjsonファイルを\n"
            "このアプリと同じフォルダ(またはbinフォルダ)に配置してください。"
        )
        sys.exit(1)

    creds = None
    if os.path.exists(TOKEN_FILE):
        creds = Credentials.from_authorized_user_file(TOKEN_FILE, SCOPES)
    
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            print("初回認証を行います。ブラウザでログインしてください...")
            # ファイルからFlowを作成
            flow = InstalledAppFlow.from_client_secrets_file(CLIENT_SECRET_FILE, SCOPES)
            creds = flow.run_local_server(port=0)
        
        with open(TOKEN_FILE, 'w') as token:
            token.write(creds.to_json())

    return build('drive', 'v3', credentials=creds)

def load_history():
    if not os.path.exists(HISTORY_FILE):
        return set()
    with open(HISTORY_FILE, 'r', encoding='utf-8') as f:
        return set(line.strip() for line in f)

def save_history(filename):
    with open(HISTORY_FILE, 'a', encoding='utf-8') as f:
        f.write(filename + "\n")

def upload_file(service, filepath, filename, folder_id):
    print(f"アップロード開始: {filename} ...")
    file_metadata = {'name': filename, 'parents': [folder_id]}
    media = MediaFileUpload(filepath, mimetype='text/csv', resumable=True)
    try:
        file = service.files().create(body=file_metadata, media_body=media, fields='id').execute()
        print(f"✅ 完了! File ID: {file.get('id')}")
        return True
    except Exception as e:
        print(f"❌ エラー: {e}")
        return False

def main():
    print(f"--- Drive Uploader (External JSON) ---")
    
    settings, config_obj = load_settings()
    folder_id = settings["folder_id"]
    watch_dir = settings["watch_dir"]

    if not folder_id:
        print("初期設定が必要です...")
        folder_id = get_folder_id_gui()
        save_settings(config_obj, folder_id)

    print(f"ターゲットID: {folder_id}")
    print(f"監視フォルダ: {watch_dir}")
    
    if not os.path.exists(watch_dir):
        print(f"待機中: データフォルダを作成待ち...")
        while not os.path.exists(watch_dir):
            time.sleep(2)
            
    try:
        service = get_drive_service()
    except Exception as e:
        print(f"認証エラー: {e}")
        input("Enterキーを押して終了...")
        return

    uploaded_files = load_history()
    print("監視中... (Ctrl+Cで終了)")

    while True:
        try:
            if os.path.exists(watch_dir):
                files = os.listdir(watch_dir)
                for file in files:
                    if file.endswith(".csv") and file not in uploaded_files:
                        filepath = os.path.join(watch_dir, file)
                        
                        initial_size = os.path.getsize(filepath)
                        time.sleep(1.0)
                        if os.path.getsize(filepath) != initial_size:
                            continue 
                        
                        if upload_file(service, filepath, file, folder_id):
                            uploaded_files.add(file)
                            save_history(file)
            time.sleep(10.0)

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"\nエラー: {e}")
            time.sleep(10.0)

if __name__ == "__main__":
    main()
