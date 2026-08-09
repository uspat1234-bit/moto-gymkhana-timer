/*
 * ====================================================================
 * MGTS - C6 Wireless Bridge (for Elecrow Main Board)
 * 1. 物理ボタン制御なし（Elecrow本体からのコマンドに依存）
 * 2. ESP-NOWで受信したデータを、UART経由でElecrow本体へ転送
 * 3. Elecrow本体からUART経由で受け取ったコマンドをESP-NOWで発射
 * 4. 送信結果(ACK)を取得し、Elecrow本体に通信状況を通知する機能を追加
 *
 * ★変更点: Elecrow本体との接続を UART0_OUT(GPIO43/44) から
 *           IO2/IO8(SD・マイク切替スイッチの影響を受けない独立ピン)経由に変更。
 *           C6側は Serial1 を新規に割り当て、以下のピンに接続する。
 *             - C6 RXピン ← Elecrow側 IO8 (TX)
 *             - C6 TXピン → Elecrow側 IO2 (RX)
 * ====================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- 送信先 ESP-NOW MACアドレス ---
uint8_t targetAddress[] = {0x58, 0xE6, 0xC5, 0x12, 0x9A, 0x80};

// --- Elecrow本体との通信用UARTピン (C6モジュール側の実ピン番号に置き換えてください) ---
#define ELECROW_UART_RX_PIN 19   // ★仮の値。C6モジュールの実際の配線ピンに要修正
#define ELECROW_UART_TX_PIN 18   // ★仮の値。C6モジュールの実際の配線ピンに要修正

// Elecrow本体との通信専用UART (Serial0はUSBデバッグ用に温存し、Serial1を新規使用)
HardwareSerial ElecrowSerial(1);

// ====================================================================
// ESP-NOW 送信結果コールバック (ESP32 v3.x対応版)
// ====================================================================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    ElecrowSerial.println("[SYS] CONN_OK");
  } else {
    ElecrowSerial.println("[SYS] CONN_NG");
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

  ElecrowSerial.println("[ESP_DATA] " + String(msg));
}

// ====================================================================
// 初期設定 (Setup)
// ====================================================================
void setup() {
  // Elecrow本体との通信専用UART初期化 (IO2/IO8経由)
  ElecrowSerial.begin(115200, SERIAL_8N1, ELECROW_UART_RX_PIN, ELECROW_UART_TX_PIN);

  // ESP-NOW 初期化
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
      ElecrowSerial.println("[ERROR] Failed to add peer");
    }
  } else {
    ElecrowSerial.println("[ERROR] ESP-NOW Init Failed");
  }

  ElecrowSerial.println("[HUB_READY] C6 Wireless Bridge Active");
}

// ====================================================================
// メインループ (Loop)
// ====================================================================
void loop() {
  if (ElecrowSerial.available() > 0) {
    String s3Data = ElecrowSerial.readStringUntil('\n');
    s3Data.trim();

    if (s3Data.length() > 0) {
      sendCommand(s3Data);
    }
  }
}
