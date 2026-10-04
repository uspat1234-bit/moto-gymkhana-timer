/*
 * ====================================================================
 * MGTS - NFC Reader & ESP-NOW Transmitter (JSON Mode)
 * タグを直接読み取り、JSON形式でハブとメイン基板へ一斉送信する
 * + WS2812Bを3個数珠つなぎにして、レディ(緑)・ID送信(赤)・SEQ_START(黄)を
 *   それぞれ専用のLEDで光らせる
 * + トグルスイッチON時は、タグ読み取りの1秒後にSEQ_STARTも自動送信（単一完結型）
 * + MGTS_NFC.h は使わず、readTagID()をこのファイル1本に統合
 * ====================================================================
 *
 * ★追加ライブラリ:
 *   Adafruit NeoPixel (Arduino IDE の「ライブラリを管理」から検索してインストール)
 *
 * ★配線（WS2812Bを3個数珠つなぎ）:
 *   1個目DIN → GPIO2 (LED_PIN、他の空きGPIOに変更可)
 *   1個目DO  → 2個目DI
 *   2個目DO  → 3個目DI
 *   VCCは3個とも並列に3V3(または5V)へ、GNDも3個とも並列にGNDへ
 *   トグルスイッチ片方 → GPIO21 (SW_AUTO_SEQ、D3)
 *   トグルスイッチもう片方 → GND
 *   （INPUT_PULLUPなので、スイッチON=GNDに接続でLOW、OFF=未接続でHIGH）
 *
 * ★LEDの役割分担（1個ずつ専用、レディだけ常時点灯、他は通常消灯で動作時だけ点灯）:
 *   1個目(PIX_READY) = レディ(待機中)：緑、常時DIM_LEVELで点灯し続ける
 *   2個目(PIX_ID)    = タグ読み取り(ID送信)：通常消灯、動作時のみBRIGHT_LEVELで点灯
 *   3個目(PIX_SEQ)   = SEQ_START自動送信：通常消灯、動作時のみBRIGHT_LEVELで点灯
 *
 * ★注意:
 *   signalBoardMac は実際のシグナル基板のMACアドレスに書き換えてください
 *   （下の 0xXX の部分はプレースホルダーのため、このままではコンパイルできません）。
 */
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
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

// --- LED (WS2812B / NeoPixel ×3 数珠つなぎ) 設定 ---
#define LED_PIN     2      // ★空いているGPIOに変更可（Wire.beginで22,23を使用中なので注意）
#define LED_COUNT   3      // 3個数珠つなぎ
// ★色がズレる(例:意図した色と違う色になる)場合は、お使いのLEDの配線順に合わせて
//   NEO_RGB / NEO_GRB / NEO_BRG などに変更してください（このボードはNEO_GRBが正しいことを確認済み）
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// 数珠つなぎの何番目が何の役割か（1個目DINから数えた順番）
#define PIX_READY 0  // 1個目：レディ(緑)
#define PIX_ID    1  // 2個目：ID送信(赤)
#define PIX_SEQ   2  // 3個目：SEQ_START(黄)

// 明るさレベル（お好みで調整、各色の成分値はそのまま0-255の明るさとして使う）
#define DIM_LEVEL    4    // レディ(緑)の常時点灯の明るさ：かなり薄く（WS2812Bは出力が非線形なので、小さい値でもそれなりに見えます）
#define BRIGHT_LEVEL 255  // 動作実行時(ID送信・SEQ_START)：屋外でも見えるようフルで

// レディ(緑)だけ常時薄く点灯、ID・SEQ用は消灯
void ledReadyOn() {
  pixel.setPixelColor(PIX_READY, pixel.Color(0, DIM_LEVEL, 0));
  pixel.setPixelColor(PIX_ID,    0);
  pixel.setPixelColor(PIX_SEQ,   0);
  pixel.show();
}

void ledIdBright() {
  pixel.setPixelColor(PIX_ID, pixel.Color(BRIGHT_LEVEL, 0, 0)); // 赤、明るく
  pixel.show();
}

void ledIdOff() {
  pixel.setPixelColor(PIX_ID, 0); // 消灯（基本状態へ戻す）
  pixel.show();
}

void ledSeqBright() {
  pixel.setPixelColor(PIX_SEQ, pixel.Color(BRIGHT_LEVEL, BRIGHT_LEVEL, 0)); // 黄(赤+緑)、明るく
  pixel.show();
}

void ledSeqOff() {
  pixel.setPixelColor(PIX_SEQ, 0); // 消灯（基本状態へ戻す）
  pixel.show();
}

// --- トグルスイッチ設定 ---
// ONの間、タグ読み取りの1秒後にSEQ_STARTを自動送信する（単一完結型）
#define SW_AUTO_SEQ 21  // D3 (GPIO21)

// --- ESP-NOW 送信先MACアドレス ---
// 1. M5StickC PLUS2 (ハブ)
uint8_t hubMac[] = { 0x58, 0xE6, 0xC5, 0x12, 0x97, 0xCC };
// 2. メイン基板 (LEDマトリクスが付いているESP32)
uint8_t mainBoardMac[] = { 0x58, 0xE6, 0xC5, 0x12, 0xD5, 0x74 };
// 3. シグナル基板 (★実際のMACアドレスに書き換えてください)
uint8_t signalBoardMac[] = { 0x58, 0xE6, 0xC5, 0x12, 0x95, 0x50 };

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
  pixel.setBrightness(255); // 全体スケールは最大のまま、明るさはDIM_LEVEL/BRIGHT_LEVELの値自体で調整
  pixel.clear();
  ledReadyOn(); // レディ(緑・薄め)だけ常時点灯

  // --- トグルスイッチ初期化 ---
  pinMode(SW_AUTO_SEQ, INPUT_PULLUP);

  // --- NFC初期化 ---
  Wire.begin(22, 23); // XIAO ESP32-C6 (SDA=22, SCL=23)
  nfc.begin();
  nfc.SAMConfig();

  // --- WiFi & ESP-NOW初期化 ---
  WiFi.mode(WIFI_STA);
  // ★ハブ・シグナル基板がチャンネル1固定で待ち受けているため、こちらも明示的に合わせる
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOWの初期化に失敗しました");
    return;
  }

  // コールバック関数の登録 (v3.xの厳格な型指定に対応)
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);

  // ピア(送信先)の登録設定
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  peerInfo.channel = 1;  // ★ハブ・シグナル基板と同じチャンネルに固定
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

    ledIdBright(); // ★読み取り成功時にID用LEDを赤(フル)で点灯

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
      ledIdOff(); // ID用LEDを消してからSEQ_START用LEDへ切り替え

      String seqCmd = "SEQ_START";
      Serial.println("🚦 SEQ_START 自動送信");
      esp_now_send(hubMac, (const uint8_t *)seqCmd.c_str(), seqCmd.length());
      esp_now_send(mainBoardMac, (const uint8_t *)seqCmd.c_str(), seqCmd.length());
      esp_now_send(signalBoardMac, (const uint8_t *)seqCmd.c_str(), seqCmd.length());

      ledSeqBright(); // SEQ_START送信の合図としてSEQ用LEDを黄(フル)で点灯
      delay(500); // ここまでで合計約1500ms
      ledSeqOff();
    } else {
      delay(1500); // 連続読み取り・連続送信防止のインターバル
      ledIdOff();
    }
  }
}
