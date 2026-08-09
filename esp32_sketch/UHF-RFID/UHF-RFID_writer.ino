/*
 * ====================================================================
 * MGTS - UHF RFID Writer (ATOMS3 Lite + M5Unit-UHF-RFID)
 * A001~E004 (20個) を順番に書き込む専用プログラム
 * ====================================================================
 */
#include <M5Unified.h>
#include "UNIT_UHF_RFID.h"

Unit_UHF_RFID uhf;

// AtomS3 LiteのGroveポート (4P): G2=RX, G1=TX
#define UHF_RX_PIN 1
#define UHF_TX_PIN 2

// 書き込み対象リスト: A001〜E004 (5クラス x 4個 = 20個)
String targets[20];
int targetIndex = 0;

void buildTargetList() {
    const char classes[] = {'A', 'B', 'C', 'D', 'E'};
    int idx = 0;
    for (int c = 0; c < 5; c++) {
        for (int n = 1; n <= 4; n++) {
            char buf[8];
            sprintf(buf, "%c%03d", classes[c], n);
            targets[idx++] = String(buf);
        }
    }
}

void setup() {
    M5.begin();
    Serial.begin(115200);
    delay(1000);

    Serial.println("[BOOT] setup開始");

    buildTargetList();

    uhf.begin(&Serial1, 115200, UHF_RX_PIN, UHF_TX_PIN, false);
    Serial.println("[BOOT] uhf.begin完了、バージョン取得試行中...");

    int retryCount = 0;
    while (1) {
        String info = uhf.getVersion();
        Serial.printf("[BOOT] getVersion試行 %d回目: [%s]\n", ++retryCount, info.c_str());
        if (info != "ERROR") {
            Serial.println("UHFモジュール接続: " + info);
            break;
        }
        delay(500);
    }
    uhf.setTxPower(2600);  // 最大出力(書き込みは近距離なので問題なし)

    Serial.println("=== MGTS UHFタグライター ===");
    Serial.println("スキップしたい場合は、メッセージ欄に SKIP と入力して送信してください");
    Serial.printf("[ %s ] をリーダーの上に置いてください...\n", targets[targetIndex].c_str());
}

void loop() {
    if (targetIndex >= 20) {
        static bool doneShown = false;
        if (!doneShown) {
            Serial.println("🎉 全20タグの書き込みが完了しました！");
            doneShown = true;
        }
        delay(1000);
        return;
    }

    // シリアルモニタから "SKIP" と送信されたら、現在の対象を飛ばす
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "SKIP") {
            Serial.printf("⏭️ [ %s ] をスキップしました\n", targets[targetIndex].c_str());
            targetIndex++;
            if (targetIndex < 20) {
                Serial.printf("\n[ %s ] をリーダーの上に置いてください...\n", targets[targetIndex].c_str());
            } else {
                Serial.println("🎉 全20タグの書き込みが完了しました！");
            }
            return;
        }
    }

    uint8_t result = uhf.pollingOnce();
    if (result > 0) {
        if (!uhf.select(uhf.cards[0].epc)) {
            Serial.println("⚠️ タグの選択に失敗しました。もう一度かざしてください");
            delay(500);
            return;
        }

        String targetId = targets[targetIndex];
        uint8_t writeBuf[4];
        for (int i = 0; i < 4; i++) writeBuf[i] = (uint8_t)targetId[i];

        bool writeOk = uhf.writeCard(writeBuf, sizeof(writeBuf), 0x04, 0, 0x00000000);
        delay(200);

        if (writeOk) {
            uint8_t readBuf[4] = {0};
            bool readOk = uhf.readCard(readBuf, sizeof(readBuf), 0x04, 0, 0x00000000);
            String readId = "";
            for (int i = 0; i < 4; i++) readId += (char)readBuf[i];

            if (readOk && readId == targetId) {
                Serial.printf("✅ 書き込み成功: %s\n", targetId.c_str());
                targetIndex++;
                if (targetIndex < 20) {
                    Serial.printf("\n[ %s ] をリーダーの上に置いてください...\n", targets[targetIndex].c_str());
                }
            } else {
                Serial.println("⚠️ 検証エラー。もう一度お試しください");
            }
        } else {
            Serial.println("❌ 書き込み失敗。もう一度お試しください");
        }

        delay(1500);
    }
    delay(50);
}
