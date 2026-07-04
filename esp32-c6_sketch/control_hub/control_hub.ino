/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Control Hub Core Unit (Wireless Only)
 * 1. D4(GPIO22)の物理ボタンでシステム全体を制御
 * 2. ESP-NOWで全ボードからのステータスを受信し、USBシリアルへ転送
 * 3. コマンド(SEQ_START等)をESP-NOWで対象基板へ直接発射
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

// ESP-NOWでコマンドを直接送信する関数
void sendCommand(String cmd) {
  esp_now_send(signalBoardMac, (uint8_t *)cmd.c_str(), cmd.length());
  esp_now_send(mainBoardMac, (uint8_t *)cmd.c_str(), cmd.length());
  Serial.println("[HUB_TX] " + cmd);
}

// ESP-NOW受信コールバック (各基板からのデータをPCへシリアル転送)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  char msg[len + 1]; 
  memcpy(msg, data, len); 
  msg[len] = '\0';
  Serial.println("[ESP_DATA] " + String(msg)); 
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_HUB, INPUT_PULLUP);
  
  // ESP-NOW 初期化とピア(通信相手)の登録
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); 
  WiFi.disconnect();
  
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);

    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 1;
    peerInfo.encrypt = false;

    // シグナルボードを登録
    memcpy(peerInfo.peer_addr, signalBoardMac, 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ERROR] Failed to add Signal Board peer");
    }

    // メインボードを登録
    memcpy(peerInfo.peer_addr, mainBoardMac, 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ERROR] Failed to add Main Board peer");
    }
  } else {
    Serial.println("[ERROR] ESP-NOW Init Failed");
  }

  Serial.println("[HUB_READY] Wireless Control Hub Active (USB Serial Mode)");
}

void loop() {
  // 物理ボタン操作（コマンド配信）
  static bool lastBtn = HIGH;
  static unsigned long btnDown = 0;
  bool currBtn = digitalRead(BTN_HUB);

  if (currBtn == LOW && lastBtn == HIGH) {
    btnDown = millis();
  } 
  else if (currBtn == HIGH && lastBtn == LOW) {
    unsigned long pressDuration = millis() - btnDown;
    
    // 誤操作防止：4000ms（4秒）以上の長押しでリセット
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
  
  delay(10);
}
