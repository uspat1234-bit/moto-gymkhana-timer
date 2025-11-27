import os
import time
import datetime
import pickle
from google_auth_oauthlib.flow import InstalledAppFlow
from google.auth.transport.requests import Request
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload

# ==========================================
# ★設定エリア
# ==========================================

# 1. 監視するフォルダ (ラズパイの共有フォルダ)
WATCH_DIR = os.path.join(os.getcwd(), "gymkhana_data")

# 2. Google Driveの保存先フォルダID (書き換えてください)
DRIVE_FOLDER_ID = "1ou0BsBw88D4tzNmwaIfRu1twWLt-p8N8" 

# 3. 認証用ファイル名 (ダウンロードしたOAuth JSON)
CREDENTIALS_FILE = 'credentials.json'
TOKEN_FILE = 'token.pickle' # 自動生成されるのでこのままでOK

# 監視間隔 (秒)
CHECK_INTERVAL = 10

# ==========================================

# 権限スコープ
SCOPES = ['https://www.googleapis.com/auth/drive.file']

def authenticate():
    """ユーザー認証 (OAuth 2.0)"""
    creds = None
    # すでにログイン済み(token.pickleがある)なら読み込む
    if os.path.exists(TOKEN_FILE):
        with open(TOKEN_FILE, 'rb') as token:
            creds = pickle.load(token)
    
    # ログインしていない、または有効期限切れの場合
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            # ブラウザを立ち上げてログインを求める
            flow = InstalledAppFlow.from_client_secrets_file(
                CREDENTIALS_FILE, SCOPES)
            creds = flow.run_local_server(port=0)
        
        # 次回のためにトークンを保存
        with open(TOKEN_FILE, 'wb') as token:
            pickle.dump(creds, token)

    return build('drive', 'v3', credentials=creds)

def find_file_in_folder(service, filename, folder_id):
    """同名ファイルの検索"""
    query = f"name = '{filename}' and '{folder_id}' in parents and trashed = false"
    results = service.files().list(q=query, fields="files(id, name)").execute()
    files = results.get('files', [])
    if files:
        return files[0]['id']
    return None

def upload_file(service, local_path, filename):
    """アップロード実行"""
    try:
        existing_file_id = find_file_in_folder(service, filename, DRIVE_FOLDER_ID)

        file_metadata = {
            'name': filename,
            'parents': [DRIVE_FOLDER_ID]
        }
        media = MediaFileUpload(local_path, mimetype='text/csv', resumable=True)

        if existing_file_id:
            # 上書き更新
            del file_metadata['parents']
            updated_file = service.files().update(
                fileId=existing_file_id,
                body=file_metadata,
                media_body=media,
                fields='id'
            ).execute()
            print(f"🔄 更新完了 (Update): {filename}")
        else:
            # 新規作成
            file = service.files().create(
                body=file_metadata,
                media_body=media,
                fields='id'
            ).execute()
            print(f"✅ アップロード完了 (New): {filename}")

    except Exception as e:
        print(f"❌ エラー: {e}")

def main():
    print(f"--- 監視開始: {WATCH_DIR} ---")
    
    # 初回起動時にブラウザでログインを求められます
    try:
        service = authenticate()
        print("✅ 認証成功！監視ループに入ります...")
    except Exception as e:
        print(f"認証エラー: {e}")
        return

    file_timestamps = {}

    while True:
        try:
            if not os.path.exists(WATCH_DIR):
                print(f"⚠️ フォルダが見つかりません (再接続待機...)")
                time.sleep(CHECK_INTERVAL)
                continue

            files = [f for f in os.listdir(WATCH_DIR) if f.endswith('.csv')]

            for filename in files:
                local_path = os.path.join(WATCH_DIR, filename)
                current_mtime = os.path.getmtime(local_path)
                
                if filename not in file_timestamps or current_mtime > file_timestamps[filename]:
                    print(f"\n[{datetime.datetime.now().strftime('%H:%M:%S')}] 変更検知: {filename}")
                    upload_file(service, local_path, filename)
                    file_timestamps[filename] = current_mtime
            
            time.sleep(CHECK_INTERVAL)

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"エラー: {e}")
            time.sleep(CHECK_INTERVAL)

if __name__ == '__main__':
    main()