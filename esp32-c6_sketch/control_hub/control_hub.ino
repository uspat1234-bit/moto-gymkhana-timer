/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Control Hub Core Unit
 * 1. D4(GPIO22)の物理ボタンでシステム全体を制御
 * 2. ESP-NOWで全ボードからのステータスを受信し、USBシリアルへ転送
 * 3. コマンド(SEQ_START等)やPCからのデータ(JSON)をESP-NOWで対象基板へ発射
 * ====================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- 送信先 ESP-NOW MACアドレス ---
uint8_t signalBoardMac[] = { 0x58, 0xE6, 0xC5, 0x12, 0x95, 0x50 };
uint8_t mainBoardMac[]   = { 0x58, 0xE6, 0xC5, 0x12, 0xD5, 0x74 };

// --- ピン定義 ---
#define BTN_HUB 22 // D4 (GPIO 22)

// ====================================================================
// ESP-NOW 送信処理 (PCからのデータや、ボタン操作時のコマンドを一斉送信)
// ====================================================================
void sendCommand(String cmd) {
  esp_now_send(signalBoardMac, (uint8_t *)cmd.c_str(), cmd.length());
  esp_now_send(mainBoardMac, (uint8_t *)cmd.c_str(), cmd.length());
  
  // デバッグ用：PC側のシリアルモニタ（生ログ）にも何を送信したか表示
  Serial.println("[HUB_TX] " + cmd);
}

// ====================================================================
// ESP-NOW 受信コールバック (各基板からのデータをPCへシリアル転送)
// ====================================================================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  char msg[len + 1]; 
  memcpy(msg, data, len); 
  msg[len] = '\0';
  
  // PCアプリが認識できるように "[ESP_DATA] " というプレフィックスを付けて送る
  Serial.println("[ESP_DATA] " + String(msg)); 
}

// ====================================================================
// 初期設定 (Setup)
// ====================================================================
void setup() {
  Serial.begin(115200);
  pinMode(BTN_HUB, INPUT_PULLUP);
  
  // ESP-NOW 初期化
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); 
  WiFi.disconnect();
  
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);

    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 1;
    peerInfo.encrypt = false;

    // シグナルボードをピア登録
    memcpy(peerInfo.peer_addr, signalBoardMac, 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ERROR] Failed to add Signal Board peer");
    }

    // メインボードをピア登録
    memcpy(peerInfo.peer_addr, mainBoardMac, 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ERROR] Failed to add Main Board peer");
    }
  } else {
    Serial.println("[ERROR] ESP-NOW Init Failed");
  }

  Serial.println("[HUB_READY] Wireless Control Hub Active (USB Serial Mode)");
}

// ====================================================================
// メインループ (Loop)
// ====================================================================
void loop() {
  // --------------------------------------------------
  // 1. 物理ボタン操作の監視とコマンド配信
  // --------------------------------------------------
  static bool lastBtn = HIGH;
  static unsigned long btnDown = 0;
  bool currBtn = digitalRead(BTN_HUB);

  if (currBtn == LOW && lastBtn == HIGH) {
    btnDown = millis();
  } 
  else if (currBtn == HIGH && lastBtn == LOW) {
    unsigned long pressDuration = millis() - btnDown;
    
    // 誤操作防止：4000ms（4秒）以上の長押しで強制リセット
    if (pressDuration > 4000) {
      Serial.println("[HUB] System Reset Triggered (4sec hold)");
      sendCommand("FORCE_DNF");
      sendCommand("STOP");
    } 
    // 短押し：シグナルスタート
    else if (pressDuration > 50) {
      Serial.println("[HUB] Sequence Start Triggered");
      sendCommand("SEQ_START");
    }
  }
  lastBtn = currBtn;

  // --------------------------------------------------
  // 2. PC(総合アプリ)からのデータを受信してESP-NOWで中継
  // --------------------------------------------------
  if (Serial.available() > 0) {
    String pcData = Serial.readStringUntil('\n');
    pcData.trim(); // ゴミとなる改行コードや空白を除去
    
    // データが空でなければ、そのまま全基板へ転送
    if (pcData.length() >
