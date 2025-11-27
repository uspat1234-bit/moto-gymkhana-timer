import nfc
import sys
import time

class EraserWriter:
    def __init__(self, name, rider_id, bike):
        # 書き込むデータを作成
        self.text_data = f"Name:{name}|ID:{rider_id}|Bike:{bike}"

    def on_connect(self, tag):
        print(f"\n【 Touched 】 Tag: {tag.type}")
        print("⚠️  重要: 処理が終わるまで、絶対にタグを動かさないでください！")
        print("   (初期化と書き込みを行います...)")
        time.sleep(1.0)

        # NTAG2xx系以外は除外
        if tag.type != "Type2Tag":
            print("❌ エラー: NTAG215 (Type2Tag) ではありません。")
            return True

        # --- STEP 1: 完全消去 (ゼロ埋め) ---
        print("\n[1/2] メモリのお掃除中 (Zero Fill)...")
        try:
            # NTAG215のユーザー領域 (Page 4 〜 29 までをクリア)
            zero_data = b'\x00\x00\x00\x00'
            for i in range(4, 30):
                tag.write(i, zero_data)
                # 進捗が見えるようにドットを表示
                print(".", end="", flush=True)
            print(" 完了！")
        except Exception as e:
            print(f"\n❌ お掃除中にエラー: {e}")
            print("タグが途中で離れた可能性があります。再試行してください。")
            return True

        # --- STEP 2: 新規書き込み ---
        print(f"\n[2/2] データの書き込み: {self.text_data}")
        
        try:
            # データ構築 (手動NDEF作成)
            text_bytes = self.text_data.encode('utf-8')
            
            # Tレコードヘッダー (Type T, UTF-8, 'en')
            # D1: Record Header (SR=1, TNF=1)
            # 01: Type Length
            # Payload Length = Status(1) + Lang(2) + Text
            payload_len = len(text_bytes) + 3 
            
            # ヘッダー組み立て
            record_header = b'\xD1\x01' + payload_len.to_bytes(1, 'big') + b'\x54\x02en'
            full_record = record_header + text_bytes

            # TLVラッパー (03 + Len + Value + FE)
            tlv_data = b'\x03' + len(full_record).to_bytes(1, 'big') + full_record + b'\xfe'

            # 4バイト境界パディング
            while len(tlv_data) % 4 != 0:
                tlv_data += b'\x00'

            # 書き込み実行
            start_page = 4
            for i in range(0, len(tlv_data), 4):
                page_num = start_page + (i // 4)
                page_data = tlv_data[i : i+4]
                tag.write(page_num, page_data)
                print(".", end="", flush=True)

            print("\n\n✅ 全工程完了！きれいに書き込めました。")
            print(f"   内容: {self.text_data}")
            print("   タグを離してください...")
            
            time.sleep(2.0)
            
        except Exception as e:
            print(f"\n❌ 書き込みエラー: {e}")

        return True

if __name__ == '__main__':
    print("=" * 60)
    print("  【 完全初期化 & 書き込みツール 】")
    print("   ※ 入力文字数制限に注意してください ※")
    print("     合計データ量の上限は約 250バイト です。")
    print("     (目安: 日本語なら約80文字、英数字なら約240文字以内)")
    print("=" * 60)

    while True:
        try:
            print("\n--- 新しいライダーを登録します ---")
            
            # 入力ループ (制限チェック付き)
            while True:
                input_name = input("ライダー名 : ").strip()
                if not input_name: continue 
                
                input_id   = input("ID         : ").strip()
                input_bike = input("車両名     : ").strip()

                # バイト数チェック
                temp_data = f"Name:{input_name}|ID:{input_id}|Bike:{input_bike}"
                byte_len = len(temp_data.encode('utf-8'))
                
                # NDEFヘッダー分を考慮して余裕を持たせる (上限252バイト付近)
                if byte_len > 250:
                    print(f"\n⚠️ 文字数が多すぎます！ (現在: {byte_len}バイト / 上限: 250バイト)")
                    print("   名前や車両名を短くして、再入力してください。")
                    continue # 再入力へ
                
                # OKならブレイク
                break

            writer = EraserWriter(input_name, input_id, input_bike)

            print("-" * 30)
            print(f"書き込み待機中... (Ctrl+Cで終了)")
            
            # リーダー接続ループ
            while True:
                try:
                    with nfc.ContactlessFrontend('usb') as clf:
                        if clf.connect(rdwr={'on-connect': writer.on_connect}):
                            break
                except IOError:
                    print("リーダー接続待機中...", end="\r")
                    time.sleep(2.0)
                except Exception as e:
                    print(f"予期せぬエラー: {e}")
                    time.sleep(2.0)
            
            print("\n次の登録へ進みます...")

        except KeyboardInterrupt:
            print("\n\n終了します")
            sys.exit(0) 
