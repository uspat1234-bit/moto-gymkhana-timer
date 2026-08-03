/*
 * ====================================================================
 * MGTS - C6 Wireless Bridge (for Elecrow Main Board)
 * 1. 物理ボタン制御なし（Elecrow本体からのコマンドに依存）
 * 2. ESP-NOWで受信したデータを、Serial0(UART)経由でElecrow本体へ転送
 * 3. Elecrow本体からSerial0経由で受け取ったコマンドをESP-NOWで発射
 * 4. 送信結果(ACK)を取得し、Elecrow本体に通信状況を通知する機能を追加
 * ====================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- 送信先 ESP-NOW MACアドレス ---
// ※ここをタイマー実機（またはハブ）の実際のMACアドレスに変更してください。
// ブロードキャスト(FF:FF...)のままだと送信結果の受領確認(ACK)が返ってきません。
uint8_t targetAddress[] = {0x58, 0xE6, 0xC5, 0x12, 0x9A, 0xXX}; 

// ====================================================================
// 【修正済み】ESP-NOW 送信結果コールバック (ESP32 v3.x対応版)
// ====================================================================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // 相手から受領確認(ACK)が返ってきたらOK、無応答ならNG
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial0.println("[SYS] CONN_OK");
  } else {
    Serial0.println("[SYS] CONN_NG");
  }
}

// ====================================================================
// ESP-NOW 送信処理 (Elecrow本体からのコマンドを他基板へ送信)
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

  // プレフィックスを付けてElecrow本体(Serial0)へ送る
  Serial0.println("[ESP_DATA] " + String(msg)); 
}

// ====================================================================
// 初期設定 (Setup)
// ====================================================================
void setup() {
  // C6モジュールの物理ピン（専用ソケット側）の初期化
  Serial0.begin(115200);

  // ESP-NOW 初期化
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // ハブ側とチャンネル(1)を合わせる
  WiFi.disconnect();

  if (esp_now_init() == ESP_OK) {
    // 受信コールバックの登録
    esp_now_register_recv_cb(OnDataRecv);
    
    // 送信結果コールバックの登録
    esp_now_register_send_cb(OnDataSent);

    // ピア(宛先)の登録
    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, targetAddress, 6);

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial0.println("[ERROR] Failed to add peer");
    }
  } else {
    Serial0.println("[ERROR] ESP-NOW Init Failed");
  }

  // Elecrow本体に準備完了を通知
  Serial0.println("[HUB_READY] C6 Wireless Bridge Active");
}

// ====================================================================
// メインループ (Loop)
// ====================================================================
void loop() {
  // --------------------------------------------------
  // Elecrow本体(S3)からのデータを受信してESP-NOWで中継
  // --------------------------------------------------
  if (Serial0.available() > 0) {
    String s3Data = Serial0.readStringUntil('\n');
    s3Data.trim(); // ゴミとなる改行コードや空白を除去

    // データが空でなければ、そのままESP-NOWで送信
    if (s3Data.length() > 0) {
      sendCommand(s3Data);
    }
  }
}
