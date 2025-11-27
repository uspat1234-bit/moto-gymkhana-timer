import os
import time
import sys
import json
import webbrowser
import configparser  # ★追加: iniファイル読み込み用

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

CONFIG_FILE = os.path.join(BASE_DIR, "config.ini")
TOKEN_FILE = os.path.join(BASE_DIR, "token.json")
HISTORY_FILE = os.path.join(BASE_DIR, "uploaded_history.txt")

# 認証スコープ
SCOPES = ['https://www.googleapis.com/auth/drive.file']

# ★重要: CLIENT_CONFIG (ここにあなたのclient_secret.jsonの中身を貼る)
CLIENT_CONFIG = {
    "installed": {
        "client_id": "YOUR_CLIENT_ID...",
        "project_id": "your-project-id...",
        "auth_uri": "https://accounts.google.com/o/oauth2/auth",
        "token_uri": "https://oauth2.googleapis.com/token",
        "auth_provider_x509_cert_url": "https://www.googleapis.com/oauth2/v1/certs",
        "client_secret": "YOUR_CLIENT_SECRET...",
        "redirect_uris": ["http://localhost"]
    }
}

def load_settings():
    """config.ini から設定を読み込む"""
    config = configparser.ConfigParser()
    
    # デフォルト設定
    settings = {
        "folder_id": "",
        "watch_dir": os.path.join(BASE_DIR, "gymkhana_data")
    }

    if os.path.exists(CONFIG_FILE):
        try:
            # utf-8で読み込む (日本語コメント対応)
            config.read(CONFIG_FILE, encoding='utf-8')
            
            if "GoogleDrive" in config and "FolderID" in config["GoogleDrive"]:
                settings["folder_id"] = config["GoogleDrive"]["FolderID"].strip()
            
            if "System" in config and "DataDir" in config["System"]:
                dir_name = config["System"]["DataDir"].strip()
                settings["watch_dir"] = os.path.join(BASE_DIR, dir_name)
                
            print("✅ 設定ファイルを読み込みました")
        except Exception as e:
            print(f"⚠️ 設定ファイルの読み込みに失敗: {e}")
    else:
        print("⚠️ config.ini が見つかりません。デフォルト設定を使用します。")
        # ファイルがない場合は自動生成するのも親切
        create_default_config()

    return settings

def create_default_config():
    """config.ini の雛形を作成"""
    config = configparser.ConfigParser()
    config["GoogleDrive"] = {"FolderID": ""}
    config["System"] = {"DataDir": "gymkhana_data"}
    try:
        with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
            config.write(f)
        print("ℹ️ config.ini を作成しました。設定してください。")
    except:
        pass

# ... (get_drive_service, load_history, save_history, upload_file は変更なし) ...
def get_drive_service():
    """Google Drive API認証 (対話型)"""
    creds = None
    if os.path.exists(TOKEN_FILE):
        creds = Credentials.from_authorized_user_file(TOKEN_FILE, SCOPES)
    
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            print("初回認証を行います。ブラウザでログインしてください...")
            flow = InstalledAppFlow.from_client_config(CLIENT_CONFIG, SCOPES)
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
    file_metadata = {
        'name': filename,
        'parents': [folder_id]
    }
    media = MediaFileUpload(filepath, mimetype='text/csv', resumable=True)
    try:
        file = service.files().create(body=file_metadata, media_body=media, fields='id').execute()
        print(f"✅ 完了! File ID: {file.get('id')}")
        return True
    except Exception as e:
        print(f"❌ エラー: {e}")
        return False

def main():
    print(f"--- Drive Uploader (ini Config Ver) ---")
    
    # 1. 設定読み込み
    settings = load_settings()
    folder_id = settings["folder_id"]
    watch_dir = settings["watch_dir"]

    # 2. フォルダIDチェック
    if not folder_id:
        print("\n❌ エラー: アップロード先のフォルダIDが設定されていません。")
        print(f"1. {CONFIG_FILE} をメモ帳で開いてください。")
        print("2. [GoogleDrive] の FolderID にIDを入力して保存してください。")
        input("Enterキーを押して終了...")
        return

    print(f"ターゲットID: {folder_id}")
    print(f"監視フォルダ: {watch_dir}")
    
    # フォルダ待機
    if not os.path.exists(watch_dir):
        print(f"待機中: データフォルダを作成待ち...")
        while not os.path.exists(watch_dir):
            time.sleep(2)
            
    # 3. 認証
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