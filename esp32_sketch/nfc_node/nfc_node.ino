/*
 * ====================================================================
 * MGTS - NFC Reader & ESP-NOW Transmitter (JSON Mode)
 * タグを直接読み取り、JSON形式でハブとメイン基板へ一斉送信する
 * + NFC読み取り時にLED（WS2812B / NeoPixel）の色を変化させる
 * + トグルスイッチON時は、タグ読み取りの1秒後にSEQ_STARTも自動送信（単一完結型）
 * + MGTS_NFC.h は使わず、readTagID()をこのファイル1本に統合
 * ====================================================================
 *
 * ★追加ライブラリ:
 *   Adafruit NeoPixel (Arduino IDE の「ライブラリを管理」から検索してインストール)
 *
 * ★配線:
 *   WS2812B の DIN  → GPIO2  (LED_PIN、他の空きGPIOに変更可)
 *   WS2812B の VCC  → 3V3 (または5V。単体1個なら3.3Vでも大抵動作します)
 *   WS2812B の GND  → GND
 *   トグルスイッチ片方 → GPIO21 (SW_AUTO_SEQ、D3)
 *   トグルスイッチもう片方 → GND
 *   （INPUT_PULLUPなので、スイッチON=GNDに接続でLOW、OFF=未接続でHIGH）
 *
 * ★注意:
 *   signalBoardMac は実際のシグナル基板のMACアドレスに書き換えてください
 *   （下の 0xXX の部分はプレースホルダーのため、このままではコンパイルできません）。
 */
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <Adafruit_NeoPixel.h>

// --- NFCオブジェクトの実体宣言 ---
PN532_I2C pn532_i2c(Wire);
PN532 nfc(pn532_i2c);

// --- NTAGまたはClassicからIDを直接読み取る関数 ---
// （元 MGTS_NFC.h の中身をこのファイルに統合）
String readTagID() {
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
  String readID = "";

  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500)) {
    if (uidLength == 7) { // NTAG
      uint8_t buffer[32];
      if (nfc.mifareultralight_ReadPage(4, buffer)) {
        for (int i = 0; i < 4; i++) readID += (char)buffer[i];
      }
    } else if (uidLength == 4) { // Classic
      uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, keya)) {
        uint8_t buffer[16];
        if (nfc.mifareclassic_ReadDataBlock(4, buffer)) {
          for (int i = 0; i < 4; i++) readID += (char)buffer[i];
        }
      }
    }
  }
  return readID;
}

// --- LED (WS2812B / NeoPixel) 設定 ---
#define LED_PIN     2      // ★空いているGPIOに変更可（Wire.beginで22,23を使用中なので注意）
#define LED_COUNT   1
// ★色がズレる(例:シアンのはずが紫になる)場合は、お使いのLEDの配線順に合わせて
//   NEO_RGB / NEO_GRB / NEO_BRG などに変更してください（このボードはNEO_RGBに変更済み）
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// 色の定義（お好みで変更可、値はR,G,B 各0-255）
#define COLOR_IDLE    pixel.Color(0, 0, 30)   // 待機中：うっすら青
#define COLOR_READ    pixel.Color(0, 60, 0)   // 読み取り成功：緑
#define COLOR_SEQ     pixel.Color(0, 60, 60)  // SEQ_START自動送信：シアン
#define COLOR_ERROR   pixel.Color(60, 0, 0)   // 予備（エラー等で使う場合）

void setLED(uint32_t color) {
  pixel.setPixelColor(0, color);
  pixel.show();
}

// --- トグルスイッチ設定 ---
// ONの間、タグ読み取りの1秒後にSEQ_STARTを自動送信する（単一完結型）
#define SW_AUTO_SEQ 21  // D3 (GPIO21)

// --- ESP-NOW 送信先MACアドレス ---
// 1. M5StickC PLUS2 (ハブ)
uint8_t hubMac[] = { 0x00, 0x4B, 0x12, 0xC4, 0x5D, 0x70 };
// 2. メイン基板 (LEDマトリクスが付いているESP32)
uint8_t mainBoardMac[] = { 0x58, 0xE6, 0xC5, 0x12, 0x9A, 0x80 };
// 3. シグナル基板 (★実際のMACアドレスに書き換えてください)
uint8_t signalBoardMac[] = { 0x58, 0xE6, 0xC5, 0x12, 0x95, 0xXX };

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

  // --- LED初期化 ---
  pixel.begin();
  pixel.setBrightness(50); // 明るさ 0-255（お好みで調整）
  setLED(COLOR_IDLE);

  // --- トグルスイッチ初期化 ---
  pinMode(SW_AUTO_SEQ, INPUT_PULLUP);

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

  // 3. シグナル基板を登録
  memcpy(peerInfo.peer_addr, signalBoardMac, 6);
  esp_now_add_peer(&peerInfo);

  Serial.println("\n--- MGTS NFC Reader (JSON Mode) Ready ---");
  Serial.println("タグをかざしてください...");
}

void loop() {
  // タグIDを読み取る
  String scannedID = readTagID();

  if (scannedID != "") {
    Serial.println("\n📡 タグ検出: [" + scannedID + "]");

    setLED(COLOR_READ); // ★読み取り成功時にLEDを緑へ変更

    // ★構造体ではなく、Pythonアプリやメイン基板がそのまま読めるJSON文字列を生成
    String jsonStr = "{\"type\":\"ENTRY\",\"id\":\"" + scannedID + "\"}";
    Serial.println("📦 送信データ: " + jsonStr);

    // ESP-NOWで2箇所へ一斉送信 (文字列の長さをそのまま送信サイズにする)
    esp_now_send(hubMac, (const uint8_t *)jsonStr.c_str(), jsonStr.length());
    esp_now_send(mainBoardMac, (const uint8_t *)jsonStr.c_str(), jsonStr.length());

    // ★トグルスイッチがONなら、1秒後にSEQ_STARTも自動送信（単一完結型）
    bool autoSeqEnabled = (digitalRead(SW_AUTO_SEQ) == LOW);
    if (autoSeqEnabled) {
      delay(1000);

      String seqCmd = "SEQ_START";
      Serial.println("🚦 SEQ_START 自動送信");
      esp_now_send(hubMac, (const uint8_t *)seqCmd.c_str(), seqCmd.length());
      esp_now_send(mainBoardMac, (const uint8_t *)seqCmd.c_str(), seqCmd.length());
      esp_now_send(signalBoardMac, (const uint8_t *)seqCmd.c_str(), seqCmd.length());

      setLED(COLOR_SEQ); // SEQ_START送信の合図としてLEDをシアンに
      delay(500);        // ここまでで合計約1500ms
    } else {
      delay(1500); // 連続読み取り・連続送信防止のインターバル
    }

    setLED(COLOR_IDLE); // ★待機色へ戻す
  }
}
