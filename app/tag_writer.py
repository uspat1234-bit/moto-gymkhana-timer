import nfc
import ndef
import time

def main():
    print("=======================================")
    print(" 🏍️ MGTS - NFCタグ 事前プロビジョニングツール")
    print("=======================================")
    
    while True:
        target_id = input("\n📝 書き込むIDを入力してください (例: A001) / 終了は 'q': ").strip().upper()
        if target_id == 'Q':
            break
        if not target_id:
            continue

        print(f"📡 {target_id} を書き込みます。タグをリーダーにかざしてください...")

        def on_connect(tag):
            try:
                # NDEFテキストレコードの作成
                record = ndef.TextRecord(target_id)
                
                # タグに書き込み
                if tag.ndef is not None:
                    tag.ndef.records = [record]
                    print(f"✅ 書き込み成功: {tag.type} -> [{target_id}]")
                else:
                    print("❌ エラー: このタグはNDEFフォーマットに対応していません。")
            except Exception as e:
                print(f"❌ 書き込み失敗: {e}")
            return True

        # タグが離れるまで待機してから次へ
        with nfc.ContactlessFrontend('usb') as clf:
            clf.connect(rdwr={'on-connect': on_connect})
            time.sleep(1) 

if __name__ == '__main__':
    main()
