/*
 * ====================================================================
 * MGTS - NFC Reader Edge Node (ESP32-C6 + PN532)
 * 1. PN532(I2C)で NFCタグのUIDを読み取り
 * 2. {"type":"ENTRY", "id":"[UID]"} のJSONを構築
 * 3. メイン基板 と ハブ(M5StickC) の両方へESP-NOWで一斉送信
 * ====================================================================
 */

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ====================================================================
// --- 送信先MACアドレスの設定 ---
// ====================================================================
// 1. M5StickC PLUS2 (ハブ) のMACアドレス
uint8_t hubMac[] = { 0x58, 0xE6, 0xC5, 0x12, 0x9A, 0x80 };

// 2. ★メイン基板 (LEDマトリクスが付いているESP32) のMACアドレス
// ※実際のメイン基板のMACアドレスに必ず書き換えてください！
uint8_t mainBoardMac[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };


// ====================================================================
// --- PN532 I2C設定 ---
// ====================================================================
#define PN532_IRQ   (2)
#define PN532_RESET (3)  // デフォルト配線では未接続でOK

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// --- 制御用変数 ---
unsigned long lastReadTime = 0;
const unsigned long COOLDOWN_MS = 2000; // 連続読み取り防止 (2秒)

// ====================================================================
// ESP-NOW 送信完了コールバック (ACK確認用)
// ====================================================================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("[TX_STATUS] to ");
  // どのMACアドレスへの送信結果か簡易表示
  Serial.print(mac_addr[5], HEX); 
  Serial.print(" : ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success (ACK)" : "Fail");
}

// ====================================================================
// 初期設定 (Setup)
// ====================================================================
void setup(void) {
  Serial.begin(115200);
  delay(1000); // シリアル初期化待ち
  Serial.println("--- MGTS NFC Reader Node Starting ---");

  // --------------------------------------------------
  // 1. ESP-NOW 初期化とピア(送信先)登録
  // --------------------------------------------------
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW Init Failed");
    while (1); // エラー停止
  }
  
  // ★ここでコールバック関数の型を強制変換してエラーを回避（ESP32 Core v3.x対応）
  esp_now_register_send_cb(reinterpret_cast<esp_now_send_cb_t>(OnDataSent));
  
  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  // ハブ(M5)を登録
  memcpy(peerInfo.peer_addr, hubMac, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ERROR] Failed to add Hub peer");
  }

  // メイン基板を登録
  memcpy(peerInfo.peer_addr, mainBoardMac, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ERROR] Failed to add Main Board peer");
  }

  // --------------------------------------------------
  // 2. PN532 初期化
  // --------------------------------------------------
  Wire.begin();
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("[ERROR] Didn't find PN53x board.");
    Serial.println("Check wiring and DIP switch (I0=1, I1=0)");
    while (1); // エラー停止
  }
  
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // NTAGやスマホ等の読み取りに対応するための設定
  nfc.SAMConfig();
  
  Serial.println("[READY] Waiting for an NFC card/tag...");
}

// ====================================================================
// メインループ (Loop)
// ====================================================================
void loop(void) {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 }; // UID格納用バッファ
  uint8_t uidLength;                       // 4 or 7 bytes
  
  // クールダウン(2秒間)中は読み取り処理をスキップ
  if (millis() - lastReadTime < COOLDOWN_MS) {
    delay(50);
    return;
  }

  // NFCタグの接近を待機 (タイムアウトを短く設定してループを回す)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100);
  
  if (success) {
    // --------------------------------------------------
    // タグ検知時の処理
    // --------------------------------------------------
    Serial.println("====================");
    Serial.println("NFC Tag Detected!");
    
    // UIDを16進数の文字列に変換 (例: "04A1B2C3")
    String uidStr = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) uidStr += "0"; // 1桁の場合は0埋め
      uidStr += String(uid[i], HEX);
    }
    uidStr.toUpperCase(); // 大文字に統一
    Serial.println("UID: " + uidStr);
    
    // JSON文字列の構築
    String jsonPayload = "{\"type\":\"ENTRY\",\"id\":\"" + uidStr + "\"}";
    Serial.println("[TX] Sending: " + jsonPayload);
    
    // --------------------------------------------------
    // メイン基板とハブへ同時発射
    // --------------------------------------------------
    esp_now_send(mainBoardMac, (uint8_t *)jsonPayload.c_str(), jsonPayload.length());
    esp_now_send(hubMac,       (uint8_t *)jsonPayload.c_str(), jsonPayload.length());
    
    // クールダウンタイマーをリセット
    lastReadTime = millis();
  }
  
  delay(50);
}
