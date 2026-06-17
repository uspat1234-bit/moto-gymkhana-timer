/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Universal Sensor Node Firmware
 * * [アーキテクチャ概要]
 * 本プログラムは、スタートノードおよびゴールノードで共通利用する
 * センサーエッジ検知・送信用のユニバーサルファームウェアです。
 * * [ビルドスイッチによるプロファイル切り替え]
 * 以下の「プロファイル選択マクロ」の有効化・無効化によって、
 * 静的IP、MAC、および送信コマンド等の環境変数を1ファイルで一元管理します。
 * ====================================================================
 */

#include <SPI.h>
#include <Ethernet.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

/* ====================================================================
 * プロファイル（アイデンティティ）設定マクロ
 * ==================================================================== */
// ★スタートノードとして書き込む場合は有効化（コメントアウト解除）
// ★ゴールノードとして書き込む場合は無効化（コメントアウト化）
#define IS_START_NODE

#ifdef IS_START_NODE
  // --- スタートノード用 ネットワーク環境変数 ---
  const String NODE_IDENTITY     = "START_NODE";
  const String TRANSMIT_COMMAND  = "START";
  byte mac_eth[]                 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x01 };
  IPAddress ip_eth(192, 168, 1, 21); // スタートノード固定IP: .21
#else
  // --- ゴールノード用 ネットワーク環境変数 ---
  const String NODE_IDENTITY     = "GOAL_NODE";
  const String TRANSMIT_COMMAND  = "STOP";
  byte mac_eth[]                 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x02, 0x02 };
  IPAddress ip_eth(192, 168, 1, 22); // ゴールノード固定IP: .22
#endif

// 共通ネットワークセグメント定義（閉域網）
IPAddress dns(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress target_broadcast(255, 255, 255, 255); // メイン表示基板およびPC向け
const int UDP_PORT = 5005;

/* ====================================================================
 * ハードウェア・ピン定義 (Seeed Studio XIAO ESP32-C6)
 * ==================================================================== */
#define SENSOR_PIN    21  // 光電センサー NPN入力ピン (D3 / GPIO 21)

// W5500 SPIバス接続ピン
#define ETH_CS_PIN    1   // チップセレクト (D1 / GPIO 1)
#define ETH_RST_PIN   2   // ハードウェア・リセット (D2 / GPIO 2)
#define SPI_SCK       19  // SPI クロック (D8 / GPIO 19)
#define SPI_MISO      20  // SPI MISO (D9 / GPIO 20)
#define SPI_MOSI      18  // SPI MOSI (D10 / GPIO 18)

/* ====================================================================
 * 通信および制御用・内部グローバル変数
 * ==================================================================== */
EthernetUDP ethUdp;
uint8_t target_mac_main[] = { XXXXXXXXXXXXXXXXXXXXXXXXXXX }; // メインLED基板のWiFi物理MAC

// チャタリングガード時間 (ms)
const unsigned long SENSOR_DEBOUNCE_MS = 20;
// 同一通過イベント内での連続送信ロックアウト期間 (ms)
const unsigned long TRANSMIT_LOCKOUT_MS = 3000;

unsigned long last_trigger_time = 0;

/* ====================================================================
 * パケット送出（データプレーン）処理
 * ==================================================================== */
/**
 * 有線LAN（UDP L3）および無線（ESP-NOW L2）のデュアルパスへパケットを同時送出
 */
void transmitTrigger() {
  Serial.print("[TX_INFRA] Dispatching command: ");
  Serial.println(TRANSMIT_COMMAND);

  // 1. 有線LANルート: UDPブロードキャスト
  ethUdp.beginPacket(target_broadcast, UDP_PORT);
  ethUdp.print(TRANSMIT_COMMAND);
  ethUdp.endPacket();

  // 2. 無線ルート: ESP-NOW ピアへのダイレクト送信
  esp_err_t result = esp_now_send(target_mac_main, (uint8_t *)TRANSMIT_COMMAND.c_str(), TRANSMIT_COMMAND.length());
  if (result == ESP_OK) {
    Serial.println("[TX_無線] ESP-NOW Packet sent successfully.");
  } else {
    Serial.println("[TX_無線] ESP-NOW Packet delivery failed.");
  }
}

/* ====================================================================
 * 初期化ルーチン (Boot Sequence)
 * ==================================================================== */
void setup() {
  Serial.begin(115200);
  
  // センサー入力ピン構成（オープンコレクタ対応の内蔵プルアップ）
  pinMode(SENSOR_PIN, INPUT_PULLUP);

  Serial.print("\n[BOOT] Booting MGTS Node Identity: ");
  Serial.println(NODE_IDENTITY);

  // ------------------------------------------------------------------
  // 1. W5500（有線LAN）初期化シーケンス
  // ------------------------------------------------------------------
  pinMode(ETH_RST_PIN, OUTPUT);
  digitalWrite(ETH_RST_PIN, LOW);  delay(100);
  digitalWrite(ETH_RST_PIN, HIGH); delay(500); // 物理リセット
  
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, ETH_CS_PIN);
  Ethernet.init(ETH_CS_PIN);
  Ethernet.begin(mac_eth, ip_eth, dns, gateway, subnet);
  
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("[CRITICAL] W5500 hardware not detected. Layer 3 path fallback to offline.");
  } else {
    Serial.print("[INFO] W5500 Static IP established: ");
    Serial.println(Ethernet.localIP());
  }

  // ------------------------------------------------------------------
  // 2. ESP-NOW（無線LAN）初期化シーケンス
  // ------------------------------------------------------------------
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // メイン基板と完全同期（1chロック）
  WiFi.disconnect();

  if (esp_now_init() == ESP_OK) {
    Serial.println("[INFO] ESP-NOW Protocol Layer initialized.");
    
    // 送信先となるメイン表示部コアのピア登録
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, target_mac_main, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ERROR] Failed to register Main Board Peer entry.");
    }
  } else {
    Serial.println("[CRITICAL] Failed to initialize ESP-NOW Layer.");
  }

  Serial.println("[READY] Edge node sensing engine is completely online.\n");
}

/* ====================================================================
 * メインループ（エッジトリガー監視タスク）
 * ==================================================================== */
void loop() {
  unsigned long now = millis();

  // センサー入力ピンの状態チェック（NPN出力型のため、遮断時はLOWを検知）
  bool is_sensor_triggered = (digitalRead(SENSOR_PIN) == LOW);

  if (is_sensor_triggered) {
    // ソフトウェア・デバウンス（チャタリングフィルタ）による過渡状態のサプレッション
    delay(SENSOR_DEBOUNCE_MS);
    if (digitalRead(SENSOR_PIN) == LOW) {
      
      // 送信ロックアウト期間を跨いでいるか判定（通過時の重複送出防止）
      if (now - last_trigger_time > TRANSMIT_LOCKOUT_MS) {
        last_trigger_time = now; // ステート更新（タイムスタンプロック）
        
        // パケット送出タスクのキック
        transmitTrigger();
      }
    }
  }
}
