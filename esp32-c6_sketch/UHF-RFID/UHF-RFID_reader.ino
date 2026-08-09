/*
 * ====================================================================
 * MGTS - UHF RFID Reader & ESP-NOW Transmitter (ATOMS3 Lite)
 * 書き込み済みタグをUHFで検出し、JSON形式でハブ・メイン基板へ送信
 * ====================================================================
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include "UNIT_UHF_RFID.h"

Unit_UHF_RFID uhf;

// AtomS3 LiteのGroveポート (4P): G2=RX, G1=TX
#define UHF_RX_PIN 2
#define UHF_TX_PIN 1

// --- ESP-NOW 送信先MACアドレス ---
uint8_t hubMac[] = { 0x00, 0x4B, 0x12, 0xC4, 0x5D, 0x70 };
uint8_t mainBoardMac[] = { 0x58, 0xE6, 0xC5, 0x12, 0x9A, 0x80 };

// 同一タグの連続検知を抑制するための直近履歴
String lastSentId = "";
unsigned long lastSentMillis = 0;
const unsigned long RESEND_GUARD_MS = 10000;  // 同じタグの再送は10秒あける

void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ 送信成功" : "❌ 送信失敗");
}

void setup() {
    M5.begin();
    Serial.begin(115200);
    delay(1000);

    Serial.println("[BOOT] setup開始");

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
    uhf.setTxPower(2600);  // 最大出力(離れた距離での読み取りのため)

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOWの初期化に失敗しました");
        return;
    }
    esp_now_register_send_cb(OnDataSent);

    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    memcpy(peerInfo.peer_addr, hubMac, 6);
    esp_now_add_peer(&peerInfo);
    memcpy(peerInfo.peer_addr, mainBoardMac, 6);
    esp_now_add_peer(&peerInfo);

    Serial.println("=== MGTS UHFリーダー Ready ===");
}

void loop() {
    uint8_t result = uhf.pollingOnce();

    if (result > 0) {
        for (uint8_t i = 0; i < result; i++) {
            if (!uhf.select(uhf.cards[i].epc)) continue;

            uint8_t readBuf[4] = {0};
            if (!uhf.readCard(readBuf, sizeof(readBuf), 0x04, 0, 0x00000000)) continue;

            String scannedId = "";
            for (int j = 0; j < 4; j++) scannedId += (char)readBuf[j];

            // 制御文字などが混じった不正データは無視
            bool valid = true;
            for (int j = 0; j < 4; j++) {
                char c = scannedId[j];
                if (!isalnum(c)) { valid = false; break; }
            }
            if (!valid) continue;

            unsigned long now = millis();
            if (scannedId == lastSentId && (now - lastSentMillis) < RESEND_GUARD_MS) {
                continue;  // 同一タグの連続検知を抑制
            }
            lastSentId = scannedId;
            lastSentMillis = now;

            Serial.println("📡 タグ検出: [" + scannedId + "]");

            String jsonStr = "{\"type\":\"ENTRY\",\"id\":\"" + scannedId + "\"}";
            Serial.println("📦 送信データ: " + jsonStr);

            esp_now_send(hubMac, (const uint8_t *)jsonStr.c_str(), jsonStr.length());
            esp_now_send(mainBoardMac, (const uint8_t *)jsonStr.c_str(), jsonStr.length());
        }
    }
    delay(50);
}
