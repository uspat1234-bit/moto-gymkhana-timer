import nfc
import binascii
import socket
import json
import time
import re
import sys
import os

# --- 設定 ---
# ラズパイのIPアドレス (環境に合わせて変更してください)
RASPBERRY_PI_IP = "localhost" 
UDP_PORT = 5005
# ----------------

# ソケット作成
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def log(msg):
    """時刻付きログ出力 (即時表示)"""
    t = time.strftime("%H:%M:%S")
    print(f"[{t}] {msg}")
    sys.stdout.flush()

def on_connect(tag):
    print("-" * 40)
    log(f"⚡ タグ検知: {tag.type}")
    
    rider_name = "Unknown"
    rider_id = "---"
    rider_bike = ""
    success = False

    try:
        # 1. IDm取得
        rider_id = binascii.hexlify(tag._nfcid).decode('ascii').upper()
        log(f"   IDm: {rider_id}")

        # 2. データ読み取り
        full_text = ""
        
        # A. NDEF読み取り
        if tag.ndef:
            for record in tag.ndef.records:
                if hasattr(record, 'text'):
                    if "Name:" in record.text:
                        full_text = record.text
                        log(f"   NDEFデータ検出: {full_text}")
                        break
        
        # B. Raw読み取り (NDEFで取れなかった場合)
        if not full_text:
            # log("   Raw読み取り試行...")
            raw_data = b""
            # 読み取り範囲 (Page 4-12)
            for i in range(4, 13):
                try:
                    raw_data += tag.read(i)
                except:
                    break
            decoded = raw_data.decode('utf-8', errors='ignore')
            # 制御文字削除
            full_text = re.sub(r'[\x00-\x1f\x7f-\x9f]', '', decoded)

        # 3. 内容解析 ("Name:XXX|ID:YYY|Bike:ZZZ")
        if "Name:" in full_text:
            # "Name:" の位置から開始し、終端文字(fe)または末尾まで抽出
            start_index = full_text.find("Name:")
            extracted = full_text[start_index:].split('\xfe')[0]
            
            # パイプ(|)で分割
            parts = extracted.split("|")
            
            # 名前 (最初の要素から "Name:" を削除)
            rider_name = parts[0].replace("Name:", "")
            
            # IDとBikeを検索
            for p in parts[1:]:
                if p.startswith("ID:"):
                    rider_id = p.replace("ID:", "")
                elif p.startswith("Bike:"):
                    rider_bike = p.replace("Bike:", "")
            
            success = True
            log(f"   解析成功: {rider_name} / {rider_id} / {rider_bike}")
        else:
            log("   ⚠️ 有効なデータが見つかりませんでした")

    except Exception as e:
        log(f"❌ 読み取りエラー: {e}")

    # 4. UDP送信 (ラズパイへ)
    if success:
        data = {
            "type": "ENTRY",
            "name": rider_name,
            "id": rider_id,
            "bike": rider_bike
        }
        try:
            msg = json.dumps(data).encode('utf-8')
            sock.sendto(msg, (RASPBERRY_PI_IP, UDP_PORT))
            log(f"🚀 送信完了 -> {RASPBERRY_PI_IP}")
        except Exception as e:
            log(f"❌ 送信失敗: {e}")
        
        # 連続読み取り防止 (3秒待機)
        log("   (3秒待機...)")
        time.sleep(3.0)
    else:
        time.sleep(1.0)

    print("-" * 40)
    return True

def main():
    log("=== Remote Entry System (Sender) ===")
    log(f"Target Raspberry Pi: {RASPBERRY_PI_IP}:{UDP_PORT}")
    log("Ctrl+C で終了します")

    while True:
        try:
            # リーダー接続
            with nfc.ContactlessFrontend('usb') as clf:
                log("✅ リーダー接続成功。タグをタッチしてください...")
                
                while True:
                    # 0.5秒だけ待つ (Ctrl+Cを受け付けるため)
                    clf.connect(rdwr={'on-connect': on_connect}, time=0.5)
                    
        except IOError as e:
            # USBが抜けた場合などのエラーハンドリング
            # ★ここを修正しました
            if "No such device" in str(e) or "I/O error" in str(e):
                log("⚠️ リーダーが見つかりません。USBを確認してください。(3秒後再試行)")
            else:
                log(f"⚠️ IO Error: {e}")
            time.sleep(3.0)
            
        except KeyboardInterrupt:
            # Ctrl+C が押されたらループを抜ける
            raise
            
        except Exception as e:
            log(f"❌ 予期せぬエラー: {e}")
            time.sleep(3.0)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n[終了] プログラムを停止しました。")
        sys.exit(0)