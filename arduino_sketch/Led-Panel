/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Main Display & Timing Core Unit
 * * [アーキテクチャ概要]
 * 本プログラムは、モトジムカーナ計測システムにおける「メイン計測・表示部」の
 * 制御基板（XIAO ESP32-C6）用ファームウェアです。
 * * [冗長化ネットワーク設計]
 * 1. L2 無線ルート: ESP-NOW プロトコルによる非同期パケット受信
 * 2. L3 有線ルート: W5500 経由の有線LAN（UDPブロードキャスト）常時監視
 * * [コア機能]
 * - 複数出走者のキューイング管理（std::vector）とミリ秒精度の並行計時
 * - Adafruit_NeoMatrix を用いた 64x8 マトリクスLEDへのリアルタイム描画
 * - PCアプリケーションとの JSON ベースの双方向連携（ENTRY/RESULT）
 * ====================================================================
 */

#include <SPI.h>
#include <Ethernet.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> // チャンネル固定化用
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <vector>
#include <ArduinoJson.h>

/* ====================================================================
 * L3 ネットワーク構成 (有線LAN / W5500)
 * ==================================================================== */
IPAddress ip_eth(192, 168, 1, 20);      // 本機固定IPアドレス
IPAddress dns(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // 物理MACアドレス
const int UDP_PORT   = 5005;            // リスニング・送信ポート

/* ====================================================================
 * システム制御用・安全パラメータ
 * ==================================================================== */
// チャタリングおよび不正連続入力防止用ガードタイム (ms)
const unsigned long TIMING_GUARD_MS = 3000; 

/* ====================================================================
 * ハードウェア・ピン定義 (Seeed Studio XIAO ESP32-C6)
 * ==================================================================== */
#define LED_PIN       21  // マトリクスLED DIN接続ピン (GPIO 21)
#define BTN_RESET     22  // 物理操作ボタン接続ピン (GPIO 22)

// W5500 SPIバス接続ピン
#define ETH_CS_PIN    1   // チップセレクト (GPIO 1)
#define ETH_RST_PIN   2   // ハードウェア・リセット (GPIO 2)
#define SPI_SCK       19  // SPI クロック (GPIO 19)
#define SPI_MISO      20  // SPI MISO (GPIO 20)
#define SPI_MOSI      18  // SPI MOSI (GPIO 18)

/* ====================================================================
 * ハードウェア抽象化・オブジェクト定義
 * ==================================================================== */
// マトリクスLEDフロントエンドの構成定義 (64x8, ZigZag配線)
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(64, 8, LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

EthernetUDP ethUdp;
char packetBuffer[512]; // UDP受信バッファ

/* ====================================================================
 * 計測データ構造およびステート管理
 * ==================================================================== */
// 出走者（ランナー）のセッション情報を保持する構造体
struct Runner { 
  String id;                   // ゼッケン/識別ID
  unsigned long startMillis;   // 物理計測開始絶対時刻 (ms)
  float result;                // 確定タイム (秒)
  bool isDnf;                  // リタイア(DNF)フラグ
};

std::vector<Runner> runners;         // 計測中ランナーの動的キュー
String nextRiderID = "X999";         // 次期出走予定者のIDキャッシュ
unsigned long resultStartTime = 0;   // リザルト表示の開始タイムスタンプ
Runner lastFinishedRunner;           // 直近の確定ランナー情報（表示用）

unsigned long lastStartActionTime = 0; // 最終START受信時刻（全体ガード用）
unsigned long lastStopActionTime = 0;  // 最終STOP受信時刻（全体ガード用）

bool isSingleSensorMode = false;     // シングルセンサー（8の字等）動作モードフラグ

/* ====================================================================
 * ユーティリティ関数
 * ==================================================================== */
/**
 * 浮動小数点タイムをフォーマット文字列（M.SS.mmm または SS.mmm）に変換
 */
String formatTime(float t) {
  if (t < 60.0) return String(t, 3);
  int m = (int)t / 60; 
  int s = (int)t % 60; 
  int ms = (int)((t - (int)t) * 1000);
  char buf[16]; 
  sprintf(buf, "%d.%02d.%03d", m, s, ms); 
  return String(buf);
}

/**
 * 確定タイムをネットワーク内（有線LAN）の上位PCへJSONでブロードキャスト送信
 */
void sendResultToPC(String id, float timeValue) {
  String json = "{\"type\":\"RESULT\",\"id\":\"" + id + "\",\"time\":" + String(timeValue, 3) + "}";
  IPAddress bc(255, 255, 255, 255);
  ethUdp.beginPacket(bc, UDP_PORT); 
  ethUdp.print(json); 
  ethUdp.endPacket();
}

/**
 * ランナーの計測完了処理および上位PCへのリザルト送信のバインディング
 */
void completeRunner(Runner r, float timeResult, bool dnf) {
    r.result = timeResult; 
    r.isDnf = dnf; 
    lastFinishedRunner = r; 
    resultStartTime = millis();
    sendResultToPC(r.id, r.result);
}

/**
 * フォースDNF（強制リタイア）ハンドラ
 */
void handleDNF() {
  if (!runners.empty()) {
    completeRunner(runners[0], 999.999, true);
    runners.erase(runners.begin());
  }
}

/**
 * マトリクスLEDへの文字列センタリング描画ラッパー
 */
void showStatus(String txt, uint16_t color) {
  matrix.setTextColor(color);
  int x = (64 - (txt.length() * 6)) / 2; // 6px幅フォントによるセンタリング計算
  matrix.setCursor(x >= 0 ? x : 0, 1); 
  matrix.print(txt);
}

/* ====================================================================
 * 上位プロトコル・コマンドハンドラ
 * ==================================================================== */
/**
 * 受信データを解析し、タイマーのステートマシンを遷移させる共通インターフェース
 * ※有線(UDP)・無線(ESP-NOW)の両ルートからコールされる
 */
void processCommand(String msg) {
  unsigned long now = millis();
  
  if (msg.startsWith("{")) {
    // [JSON ペイロード処理] 上位PCからの事前エントリー情報受信
    JsonDocument doc;
    if (!deserializeJson(doc, msg)) {
      if (doc["type"] == "ENTRY") nextRiderID = doc["id"].as<String>();
    }
  } 
  else if (msg == "START") {
    // [START トリガー処理]
    if (isSingleSensorMode && !runners.empty()) {
      // シングルセンサーモード：START信号をSTOP（ラップ）として解釈
      if (now - lastStopActionTime > TIMING_GUARD_MS && now - runners[0].startMillis > TIMING_GUARD_MS) {
        completeRunner(runners[0], (now - runners[0].startMillis) / 1000.0, false);
        runners.erase(runners.begin());
        lastStopActionTime = now;
      }
    } else {
      // 通常モード：新規ランナーのキューイング
      if (now - lastStartActionTime > TIMING_GUARD_MS) {
        if (runners.size() < 9) { // 最大キューサイズの制限
          runners.push_back({nextRiderID, now, 0.0, false});
          nextRiderID = "X999"; 
          lastStartActionTime = now;
        }
      }
    }
  } 
  else if (msg == "STOP") {
    // [STOP トリガー処理]
    if (now - lastStopActionTime > TIMING_GUARD_MS) {
      if (!runners.empty() && now - runners[0].startMillis > TIMING_GUARD_MS) {
        completeRunner(runners[0], (now - runners[0].startMillis) / 1000.0, false);
        runners.erase(runners.begin());
        lastStopActionTime = now;
      }
    }
  }
  else if (msg == "FORCE_DNF") { 
    handleDNF(); 
  }
}

/**
 * ESP-NOW パケット受信時に駆動する非同期割込みハンドラ (ESP Core v3.x 仕様)
 */
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  char msg[len + 1];
  memcpy(msg, incomingData, len);
  msg[len] = '\0';
  
  processCommand(String(msg)); // 共通ハンドラへルーティング
}

/* ====================================================================
 * 初期化ルーチン (Boot Sequence)
 * ==================================================================== */
void setup() {
  Serial.begin(115200);
  pinMode(BTN_RESET, INPUT_PULLUP);
  
  // LEDフロントエンド初期化
  matrix.begin();
  matrix.setBrightness(100);

  // ------------------------------------------------------------------
  // 1. W5500（有線LAN）初期化シーケンス
  // ------------------------------------------------------------------
  pinMode(ETH_RST_PIN, OUTPUT);
  digitalWrite(ETH_RST_PIN, LOW); delay(100);
  digitalWrite(ETH_RST_PIN, HIGH); delay(500); // 物理リセット
  
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, ETH_CS_PIN);
  Ethernet.init(ETH_CS_PIN);
  Ethernet.begin(mac, ip_eth, dns, gateway, subnet);
  
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("[ERROR] W5500 not found.");
  } else {
    ethUdp.begin(UDP_PORT);
    Serial.println("[INFO] W5500 UDP Listening initialized.");
  }

  // ------------------------------------------------------------------
  // 2. ESP-NOW（無線LAN）初期化シーケンス
  // ------------------------------------------------------------------
  WiFi.mode(WIFI_STA); 
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // 確実な受信のため1chに強制ロック
  WiFi.disconnect();
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW Init Failed.");
  } else {
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("[INFO] ESP-NOW Receiver Ready.");
  }
}

/* ====================================================================
 * メインループ（定常タスク監視スケジューラ）
 * ==================================================================== */
void loop() {
  unsigned long now = millis();

  // ------------------------------------------------------------------
  // [Task A] 有線LANルート：UDPパケットのポーリング
  // ------------------------------------------------------------------
  int pEth = ethUdp.parsePacket();
  if (pEth) {
    int len = ethUdp.read(packetBuffer, 511);
    if (len > 0) {
      packetBuffer[len] = 0;
      processCommand(String(packetBuffer));
    }
  }

  // ------------------------------------------------------------------
  // [Task B] 物理UI（ハードウェアボタン）の状態監視とデバウンス処理
  // ------------------------------------------------------------------
  static bool lastBtnState = HIGH;
  static unsigned long btnPressTime = 0;
  bool currentBtnState = digitalRead(BTN_RESET);
  
  if (currentBtnState == LOW && lastBtnState == HIGH) {
    btnPressTime = now; // エッジ検出（押下）
  } else if (currentBtnState == HIGH && lastBtnState == LOW) {
    // エッジ検出（離上）時の長押し判定
    unsigned long pressDuration = now - btnPressTime;
    if (pressDuration > 2000) {
      // 2秒長押し：シングルセンサーモードのトグル切替
      isSingleSensorMode = !isSingleSensorMode;
      matrix.fillScreen(0);
      showStatus(isSingleSensorMode ? "SINGLE ON" : "SINGLE OFF", matrix.Color(255, 255, 0));
      matrix.show();
      delay(1500); // 状態確認のための表示フリーズ
    } else if (pressDuration > 50) {
      // 短押し：フォースDNF処理
      handleDNF();
    }
  }
  lastBtnState = currentBtnState;

  // ------------------------------------------------------------------
  // [Task C] マトリクスLEDフロントエンドのレンダリングパイプライン
  // ------------------------------------------------------------------
  matrix.fillScreen(0); // フレームバッファのクリア
  
  unsigned long elapsedResult = now - resultStartTime;
  bool isShowingResult = (resultStartTime != 0);

  // 状態1: リザルト（確定タイム/DNF）の静的表示フェーズ
  if (isShowingResult && lastFinishedRunner.isDnf) {
    if (elapsedResult < 2000) {
      if (elapsedResult < 1000) showStatus(lastFinishedRunner.id, matrix.Color(200, 200, 200));
      else showStatus("DNF", matrix.Color(255, 0, 0));
    } else { resultStartTime = 0; } // 表示期間終了でステート解除
  } 
  else if (isShowingResult && !lastFinishedRunner.isDnf) {
    if (elapsedResult < 15000) {
      if (elapsedResult < 1000) showStatus(lastFinishedRunner.id, matrix.Color(200, 200, 200));
      else showStatus(formatTime(lastFinishedRunner.result), matrix.Color(0, 255, 0));
    } else { resultStartTime = 0; }
  }
  // 状態2: 計測中（ランニング）の動的表示フェーズ
  else if (!runners.empty()) {
    float elapsed = (now - runners[0].startMillis) / 1000.0;
    
    // キュー待機人数のインジケータ描画
    matrix.setTextColor(matrix.Color(255, 100, 0)); 
    matrix.setCursor(0, 1); 
    matrix.print(runners.size());
    matrix.drawFastVLine(6, 0, 8, matrix.Color(30, 30, 30)); // セパレータ線
    
    // 経過タイムのリアルタイム描画
    String s = formatTime(elapsed); 
    matrix.setTextColor(matrix.Color(255, 255, 255));
    matrix.setCursor(64 - (s.length() * 6), 1); 
    matrix.print(s);
  } 
  // 状態3: アイドル（待機）表示フェーズ
  else {
    if (isSingleSensorMode) {
      showStatus("S-RDY:" + nextRiderID, matrix.Color(0, 255, 255));
    } else {
      showStatus("RDY:" + nextRiderID, matrix.Color(0, 255, 0));
    }
  }
  
  matrix.show(); // VSYNC（バッファの物理転送）
  
  delay(20); // 描画リフレッシュレートの適正化 (約50fps)
}
