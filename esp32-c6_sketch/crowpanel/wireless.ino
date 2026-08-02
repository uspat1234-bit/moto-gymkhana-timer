/*
 * ====================================================================
 * MGTS - C6 Wireless Bridge (for Elecrow Main Board & PC)
 * ====================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- 送信先 ESP-NOW MACアドレス ---
// ※スクリーンショットの環境に合わせて末尾0x80にしています
uint8_t targetAddress[] = {0x58, 0xE6, 0xC5, 0x12, 0x9A, 0xXX}; 

// ====================================================================
// ESP-NOW 送信結果コールバック
// ====================================================================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial0.println("[SYS] CONN_OK");
    Serial.println("[SYS] CONN_OK");
  } else {
    Serial0.println("[SYS] CONN_NG");
    Serial.println("[SYS] CONN_NG");
  }
}

// ====================================================================
// ESP-NOW 送信処理
// ====================================================================
void sendCommand(String cmd) {
  esp_now_send(targetAddress, (uint8_t *)cmd.c_str(), cmd.length());
}

// ====================================================================
// ESP-NOW 受信コールバック
// ====================================================================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  char msg[len + 1]; 
  memcpy(msg, data, len); 
  msg[len] = '\0';
  
  String outStr = "[ESP_DATA] " + String(msg);

  // S3（タイマー本体）へUARTで送信
  Serial0.println(outStr); 

  // PC（PCアプリ）へUSBケーブルで送信
  Serial.println(outStr); 
}

// ====================================================================
// 初期設定 (Setup)
// ====================================================================
void setup() {
  Serial0.begin(115200); // S3との通信用 (UART0)
  Serial.begin(115200);  // PCとの通信用 (USB CDC)
  
  // PCがUSBを認識してシリアルモニタを開くまでの猶予
  delay(3000); 
  
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); 
  WiFi.disconnect();
  
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);

    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, targetAddress, 6);
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial0.println("[ERROR] Failed to add peer");
      Serial.println("[ERROR] Failed to add peer");
    }
  } else {
    Serial0.println("[ERROR] ESP-NOW Init Failed");
    Serial.println("[ERROR] ESP-NOW Init Failed"); 
  }

  // 起動時の1回だけ生存をアピール
  Serial0.println("[HUB_READY] C6 Wireless Bridge Active");
  Serial.println("[HUB_READY] C6 Wireless Bridge Active");
}

// ====================================================================
// メインループ (Loop)
// ====================================================================
void loop() {
  // --- S3（本体）からのデータ受信処理 ---
  if (Serial0.available() > 0) {
    String s3Data = Serial0.readStringUntil('\n');
    s3Data.trim(); 
    
    if (s3Data.length() > 0) {
      sendCommand(s3Data);
    }
  }

  // --- PCアプリ（USB）からのデータ受信処理 ---
  if (Serial.available() > 0) {
    String pcData = Serial.readStringUntil('\n');
    pcData.trim();
    
    // ★ PCから「PING」と送られてきたら応答する（チェックボタン用）
    if (pcData == "PING") {
      Serial.println("[SYS] PONG (C6 Bridge is Alive)");
    } 
    // それ以外のコマンドなら、S3本体やセンサー等へESP-NOWで転送する
    else if (pcData.length() > 0) {
      sendCommand(pcData);
    }
  }
}
