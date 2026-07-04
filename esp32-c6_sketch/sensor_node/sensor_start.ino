/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Sensor Node Unit
 * * [概要]
 * 本プログラムは、モトジムカーナ計測システムにおける「センサー送信部」の
 * 制御基板（XIAO ESP32-C6）用ファームウェアです。
 * * [冗長化設計（Active-Active）]
 * 1. 無線ルート: ESP-NOW プロトコルによるピア・ツー・ピア超低遅延送信 (2宛先マルチキャスト)
 * 2. 有線ルート: W5500 チップを介した有線LAN（UDPブロードキャスト）送信
 * * [環境ノイズ・運用対策]
 * - ソフトウェア・デバウンス処理（20ms継続判定）による雨滴・落ち葉のサプレッション
 * - エッジトリガー（ワンショット）制御による、エリア内滞留時の通信ストーム防止
 * ====================================================================
 */

#include <SPI.h>
#include <Ethernet.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

/* ====================================================================
 * ハードウェア・ピン定義 (Seeed Studio XIAO ESP32-C6)
 * ==================================================================== */
// センサー入力ピン (回路図の d3 結線に対応。内部GPIO: 21)
#define SENSOR_PIN    21  

// W5500 有線LANモジュール接続用 (SPIバスおよび制御ピン)
#define ETH_CS_PIN    1   // チップセレクト (D1 / GPIO 1)
#define ETH_RST_PIN   2   // ハードウェア・リセット (D2 / GPIO 2)
#define SPI_SCK       19  // SPI クロック (D8 / GPIO 19)
#define SPI_MISO      20  // SPI MISO (D9 / GPIO 20)
#define SPI_MOSI      18  // SPI MOSI (D10 / GPIO 18)

/* ====================================================================
 * ネットワーク構成設定
 * ==================================================================== */
// 有線LAN (W5500) 静的IPアドレス定義 (.20のメインボードと重複不可)
byte mac_eth[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xE1 };
IPAddress ip_eth(192, 168, 1, 21);      
IPAddress dns(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

EthernetUDP ethUdp;
const int UDP_PORT = 5005;
IPAddress broadcastIp(255, 255, 255, 255); // レイヤ3 ブロードキャスト宛先

// 【変更箇所1】無線LAN (ESP-NOW) 受信側の物理アドレスを2つ定義
// 宛先1：メインLEDボード
uint8_t broadcastAddress1[] = {0x58, 0xE6, 0xC5, 0x12, 0xXX, 0xXX};
// 宛先2：シグナルボード
uint8_t broadcastAddress2[] = {0x58, 0xE6, 0xC5, 0x12, 0xYY, 0xYY}; 

/* ====================================================================
 * タイミング制御・フィルター用パラメータ
 * ==================================================================== */
const unsigned long DEBOUNCE_MS = 20;     // チャタリング排除用の継続遮断閾値 (ms)
const unsigned long SEND_GUARD_MS = 3000; // 不正連続トリガー防止用の再送信ガード時間 (ms)

unsigned long lastSendTime = 0;           // 最終パケット送信タイムスタンプ
unsigned long detectionStartTime = 0;      // センサー初期検知タイムスタンプ
bool isDetecting = false;                 // センサー入力継続フラグ
bool hasTriggered = false;                // 送信完了ロックフラグ (ワンショット制御用)

/**
 * ESP-NOW 送信完了コールバック関数
 * パケット送信成否のハンドシェイク結果をシリアルログに出力
 */
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("[TX_LOG] ESP-NOW status for MAC ");
  Serial.print(mac_addr[5], HEX); // 識別用にMACの末尾だけ表示
  Serial.print(" : ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "DELIVERED" : "FAILED");
}

void setup() {
  Serial.begin(115200);
  pinMode(SENSOR_PIN, INPUT_PULLUP); // センサー信号入力をプルアップで固定

  Serial.println("\n[INIT] Initialize MGTS Sensor Node...");

  // ------------------------------------------------------------------
  // 1. 有線LANモジュール (W5500) 初期化シーケンス
  // ------------------------------------------------------------------
  pinMode(ETH_RST_PIN, OUTPUT);
  digitalWrite(ETH_RST_PIN, LOW);  delay(100);
  digitalWrite(ETH_RST_PIN, HIGH); delay(500); // 物理リセット処理
  
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, ETH_CS_PIN);
  Ethernet.init(ETH_CS_PIN);
  Ethernet.begin(mac_eth, ip_eth, dns, gateway, subnet);
  
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("[ERROR] W5500 hardware not detected. Ethernet disabled.");
  } else {
    ethUdp.begin(UDP_PORT);
    Serial.print("[INFO] W5500 Ethernet initialized. IP: ");
    Serial.println(Ethernet.localIP());
  }

  // ------------------------------------------------------------------
  // 2. 無線LAN (ESP-NOW) 初期化シーケンス
  // ------------------------------------------------------------------
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // チャンネルを1chに固定（パケットの衝突・不一致対策）
  WiFi.disconnect();

  if (esp_now_init() == ESP_OK) {
    // ESP Core v3.x の厳格な型チェックを通過させるためキャスト処理を実行
    esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
    
    // 受信側ピア（宛先1）の登録処理
    esp_now_peer_info_t peerInfo1 = {};
    memcpy(peerInfo1.peer_addr, broadcastAddress1, 6);
    peerInfo1.channel = 1;
    peerInfo1.encrypt = false;
    if (esp_now_add_peer(&peerInfo1) == ESP_OK) {
      Serial.println("[INFO] ESP-NOW Peer 1 (MAIN) registered.");
    }

    // 受信側ピア（宛先2）の登録処理
    esp_now_peer_info_t peerInfo2 = {};
    memcpy(peerInfo2.peer_addr, broadcastAddress2, 6);
    peerInfo2.channel = 1;
    peerInfo2.encrypt = false;
    if (esp_now_add_peer(&peerInfo2) == ESP_OK) {
      Serial.println("[INFO] ESP-NOW Peer 2 (SIGNAL) registered.");
    }
    
  } else {
    Serial.println("[ERROR] Failed to initialize ESP-NOW.");
  }

  Serial.println("[READY] System status: ACTIVE. Monitoring SENSOR_PIN (GPIO21)...");
}

void loop() {
  // センサー入力状態のサンプリング (LOW: 遮断状態 / HIGH: 通過状態)
  bool currentSensorState = (digitalRead(SENSOR_PIN) == LOW);
  unsigned long now = millis();

  if (currentSensorState) {
    if (!isDetecting) {
      // センサーの初回遮断エッジを検出
      detectionStartTime = now;
      isDetecting = true;
      hasTriggered = false; // 新規ターゲット侵入のためロックを解除
    } 
    else {
      // チャタリング・ノイズ除去フィルターの判定
      if (now - detectionStartTime >= DEBOUNCE_MS) {
        
        // 未送信かつ送信ガード時間を超過している場合のみパケットを送出 (エッジトリガー制御)
        if (!hasTriggered && (now - lastSendTime > SEND_GUARD_MS)) {
          String msg = "START";
          
          Serial.println("\n-----------------------------------------");
          Serial.print("[TRIGGER] Target detected. Duration: ");
          Serial.print(now - detectionStartTime);
          Serial.println(" ms");

          // 【変更箇所3】ルート1: 無線パケット送出 (ESP-NOW) 宛先1と宛先2へ順番に送信
          esp_now_send(broadcastAddress1, (uint8_t *)msg.c_str(), msg.length());
          esp_now_send(broadcastAddress2, (uint8_t *)msg.c_str(), msg.length());
          
          // ルート2: 有線パケット送出 (UDP Layer3 Broadcast)
          if (Ethernet.hardwareStatus() != EthernetNoHardware) {
            ethUdp.beginPacket(broadcastIp, UDP_PORT);
            ethUdp.print(msg);
            ethUdp.endPacket();
            Serial.println("[TX_UDP] Broadcast packet sent successfully.");
          }

          Serial.println("-----------------------------------------");
          
          lastSendTime = now;
          hasTriggered = true; // 送信完了ロックを有効化（滞留時の連続誤送信を抑制）
        }
      }
    }
  } else {
    // センサー解放（光線復帰）時にすべてのステートをリセット
    isDetecting = false;
    hasTriggered = false; 
  }

  // 高速サンプリング周期の維持（1kHz）
  delay(1);
}
