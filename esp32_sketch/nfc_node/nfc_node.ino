/*
 * ====================================================================
 * MGTS - NFC Reader & ESP-NOW Transmitter (JSON Mode)
 * タグを直接読み取り、JSON形式でハブとメイン基板へ一斉送信する
 * ====================================================================
 */
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <PN532_I2C.h>
#include "MGTS_NFC.h" // 別タブで作成したNFC共通モジュール

// --- NFCオブジェクトの実体宣言 ---
PN532_I2C pn532_i2c(Wire);
PN532 nfc(pn532_i2c);

// --- ESP-NOW 送信先MACアドレス ---
// 1. M5StickC PLUS2 (ハブ)
uint8_t hubMac[] = { 0x00, 0x4B, 0x12, 0xC4, 0x5D, 0x70 };

// 2. メイン基板 (LEDマトリクスが付いているESP32)
uint8_t mainBoardMac[] = { 0x58, 0xE6, 0xC5, 0x12, 0x9A, 0x80 };

// --- ESP-NOW 送信完了コールバック ---
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // ★最新ESP32コア(v3.x)の仕様変更によるポインタズレを補正して正しいMACを表示
  const uint8_t *real_mac = mac_addr;
  if (mac_addr[3] == 0x40 && mac_addr[2] >= 0x80) {
    real_mac = *((const uint8_t **)mac_addr);
  }

  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "✅ 送信成功 -> " : "❌ 送信失敗 -> ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", real_mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(2000); 

  // --- NFC初期化 ---
  Wire.begin(22, 23); // XIAO ESP32-C6 (SDA=22, SCL=23)
  nfc.begin();
  nfc.SAMConfig();

  // --- WiFi & ESP-NOW初期化 ---
  WiFi.mode(WIFI_STA); 
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOWの初期化に失敗しました");
    return;
  }
  
  // コールバック関数の登録 (v3.xの厳格な型指定に対応)
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);

  // ピア(送信先)の登録設定
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;

  // 1. ハブを登録
  memcpy(peerInfo.peer_addr, hubMac, 6);
  esp_now_add_peer(&peerInfo);

  // 2. メイン基板を登録
  memcpy(peerInfo.peer_addr, mainBoardMac, 6);
  esp_now_add_peer(&peerInfo);

  Serial.println("\n--- MGTS NFC Reader (JSON Mode) Ready ---");
  Serial.println("タグをかざしてください...");
}

void loop() {
  // モジュール化された関数を使ってタグIDを読み取る
  String scannedID = readTagID();
  
  if (scannedID != "") {
    Serial.println("\n📡 タグ検出: [" + scannedID + "]");
    
    // ★構造体ではなく、Pythonアプリやメイン基板がそのまま読めるJSON文字列を生成
    String jsonStr = "{\"type\":\"ENTRY\",\"id\":\"" + scannedID + "\"}";
    Serial.println("📦 送信データ: " + jsonStr);

    // ESP-NOWで2箇所へ一斉送信 (文字列の長さをそのまま送信サイズにする)
    esp_now_send(hubMac, (const uint8_t *)jsonStr.c_str(), jsonStr.length());
    esp_now_send(mainBoardMac, (const uint8_t *)jsonStr.c_str(), jsonStr.length());

    delay(1500); // 連続読み取り・連続送信防止のインターバル
  }
}
