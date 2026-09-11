/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Main Display Core Unit
 * * [Architecture Highlights]
 * 1. 先読みロック (Read-Ahead Lock):
 * 有線/無線の時間差パケットによる競合を防ぐため、条件合致時に
 * 最優先でタイムスタンプを更新し、重複パケットを即座に破棄。
 * 2. 非同期遅延送信 (Asynchronous Delayed Transmission):
 * ゴール直後のネットワーク輻輳を回避するため、タイム確定処理と
 * PCへのESP-NOW送信を切り離し、1000ms後に非同期送出する。
 * 3. クロス・ロック (Cross-Lock Guard):
 * シングルモード時、ゴール処理直後の3秒間は物理センサーの余韻に
 * よる意図しない「次のSTART」を強制ミュートする。
 *
 * ※W5500(有線LAN)機能は未使用のため撤去済み。ESP-NOWのみで動作します。
 * ※このボードはMACアドレスの強制上書きを行いません（工場出荷時のMACをそのまま使用）。
 *   2台目以降のボード用。センサー側の宛先MAC設定は、このボード起動時に
 *   シリアルモニタへ出力される実際のMACアドレスに合わせて設定してください。
 * ====================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <vector>
#include <ArduinoJson.h>

/* ====================================================================
 * ESP-NOW 関連設定
 * ==================================================================== */
// ESP-NOW 送信先ハブのMACアドレス（実際のハブのMACに合わせてください）
uint8_t hubAddress[] = {0x58, 0xE6, 0xC5, 0x12, 0xXX, 0xXX};

// システム・ガードタイム設定
const unsigned long TIMING_GUARD_MS = 3000; // チャタリング・不正連続入力防止 (ms)
const unsigned long DELAY_SEND_MS = 1000;   // PCへの非同期遅延送信のディレイ時間 (ms)

/* ====================================================================
 * ハードウェア・ピン定義
 * ==================================================================== */
#define LED_PIN       21
#define BTN_RESET     22

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(64, 8, LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

/* ====================================================================
 * 計測データ構造・ステート管理
 * ==================================================================== */
struct Runner { String id; unsigned long startMillis; float result; bool isDnf; };
std::vector<Runner> runners;
String nextRiderID = "X999";
unsigned long resultStartTime = 0;
Runner lastFinishedRunner;

unsigned long lastStartActionTime = 0;
unsigned long lastStopActionTime = 0;
bool isSingleSensorMode = false;

// [非同期遅延送信用 ステート変数]
bool pendingResultSend = false;
String pendingResultId = "";
float pendingResultTimeVal = 0.0;
unsigned long resultLockedAt = 0;

/* ====================================================================
 * ユーティリティ・コア関数
 * ==================================================================== */
String formatTime(float t) {
  if (t < 60.0) return String(t, 3);
  int m = (int)t / 60; int s = (int)t % 60; int ms = (int)((t - (int)t) * 1000);
  char buf[16]; sprintf(buf, "%d.%02d.%03d", m, s, ms); return String(buf);
}

void sendResultToPC(String id, float timeValue) {
  String json = "{\"type\":\"RESULT\",\"id\":\"" + id + "\",\"time\":" + String(timeValue, 3) + "}";

  // ESP-NOW経由での送信 (ハブ経由でPCのシリアルへ届けるため)
  esp_now_send(hubAddress, (uint8_t *)json.c_str(), json.length());

  Serial.println("[TX_PC] Delayed RESULT packet sent: " + json);
}

/**
 * ランナー完了処理（非同期送信キューへ登録）
 */
void completeRunner(Runner r, float timeResult, bool dnf) {
  r.result = timeResult;
  r.isDnf = dnf;
  lastFinishedRunner = r;
  resultStartTime = millis();

  // 即時送信せず、遅延送信用のステートをセットする
  pendingResultId = r.id;
  pendingResultTimeVal = r.result;
  resultLockedAt = millis();
  pendingResultSend = true;
}

void handleDNF() {
  if (!runners.empty()) {
    lastStopActionTime = millis(); // 優先的にロック確保
    completeRunner(runners[0], 999.999, true);
    runners.erase(runners.begin());
  }
}

void showStatus(String txt, uint16_t color) {
  matrix.setTextColor(color);
  int x = (64 - (txt.length() * 6)) / 2;
  matrix.setCursor(x >= 0 ? x : 0, 1); matrix.print(txt);
}

/* ====================================================================
 * 上位プロトコル・コマンドハンドラ (★先読みロック＆クロスロック実装)
 * ==================================================================== */
void processCommand(String msg) {
  unsigned long now = millis();

  if (msg.startsWith("{")) {
    JsonDocument doc;
    if (!deserializeJson(doc, msg)) {
      if (doc["type"] == "ENTRY") nextRiderID = doc["id"].as<String>();
    }
  }
  else if (msg == "START") {
    if (isSingleSensorMode && !runners.empty()) {
      // [シングルモード：ゴール時の処理]
      if (now - lastStopActionTime > TIMING_GUARD_MS && now - runners[0].startMillis > TIMING_GUARD_MS) {
        lastStopActionTime = now; // ★[最優先] 即座にロック確保
        completeRunner(runners[0], (now - runners[0].startMillis) / 1000.0, false);
        runners.erase(runners.begin());
      }
    } else {
      // [通常モード：スタート または シングルモード：新規スタート]
      if (now - lastStartActionTime > TIMING_GUARD_MS) {

        // ★クロス・ロック: シングルモード時、ゴール直後の3秒間は物理センサーの余韻による誤検知STARTを弾く
        if (isSingleSensorMode && (now - lastStopActionTime < TIMING_GUARD_MS)) {
          return;
        }

        lastStartActionTime = now; // ★[最優先] 即座にロック確保
        if (runners.size() < 9) {
          runners.push_back({nextRiderID, now, 0.0, false});
          nextRiderID = "X999";
        }
      }
    }
  }
  else if (msg == "STOP") {
    // [通常モード：ゴール時の処理]
    if (now - lastStopActionTime > TIMING_GUARD_MS) {
      if (!runners.empty() && now - runners[0].startMillis > TIMING_GUARD_MS) {
        lastStopActionTime = now; // ★[最優先] 即座にロック確保
        completeRunner(runners[0], (now - runners[0].startMillis) / 1000.0, false);
        runners.erase(runners.begin());
      }
    }
  }
  else if (msg == "FORCE_DNF") {
    handleDNF();
  }
}

/* ====================================================================
 * ESP-NOW 受信コールバック
 * ==================================================================== */
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  char msg[len + 1];
  memcpy(msg, incomingData, len);
  msg[len] = '\0';
  Serial.println("[RX_ESPNOW] " + String(msg));
  processCommand(String(msg));
}

/* ====================================================================
 * 初期化ルーチン
 * ==================================================================== */
void setup() {
  Serial.begin(115200);
  pinMode(BTN_RESET, INPUT_PULLUP);
  matrix.begin(); matrix.setBrightness(100);

  // ESP-NOW 初期化
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  WiFi.disconnect();

  // ★このボードの実際のMACアドレスを確認用に表示（センサー側の宛先設定に使う）
  Serial.print("[INFO] This board's MAC address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);

    // コントロールハブへの送信を許可するためのピア登録
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, hubAddress, 6);
    peer.channel = 1;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }
}

/* ====================================================================
 * メインループ（スケジューラ）
 * ==================================================================== */
void loop() {
  unsigned long now = millis();

  // [Task 1] ★非同期遅延送信タスク (確定から1秒後にPC・ハブへ送信)
  if (pendingResultSend && (now - resultLockedAt > DELAY_SEND_MS)) {
    sendResultToPC(pendingResultId, pendingResultTimeVal);
    pendingResultSend = false; // 送信完了としてフラグをリセット
  }

  // [Task 2] 物理ボタンデバウンス処理 (長押し確定時に即時切替)
  static bool lastBtnState = HIGH;
  static unsigned long btnPressTime = 0;
  static bool isLongPressed = false; // 長押し処理済みフラグ
  bool currentBtnState = digitalRead(BTN_RESET);

  if (currentBtnState == LOW && lastBtnState == HIGH) {
    btnPressTime = now;
    isLongPressed = false;
  } else if (currentBtnState == LOW && lastBtnState == LOW) {
    // 押しっぱなしの状態で2秒経過したら即座にモードを切り替える
    if (!isLongPressed && (now - btnPressTime > 2000)) {
      isSingleSensorMode = !isSingleSensorMode;
      matrix.fillScreen(0);
      showStatus(isSingleSensorMode ? "SINGLE ON" : "SINGLE OFF", matrix.Color(255, 255, 0));
      matrix.show(); delay(1500);
      isLongPressed = true; // このターンの長押し処理を完了
    }
  } else if (currentBtnState == HIGH && lastBtnState == LOW) {
    // 長押しされていなければ短押し(DNF)として処理
    if (!isLongPressed && (now - btnPressTime > 50)) {
      handleDNF();
    }
  }
  lastBtnState = currentBtnState;

  // [Task 3] シリアルからのコマンド入力（デバッグ用）
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial.println("[RX_SERIAL] " + cmd);
      processCommand(cmd);
    }
  }

  // [Task 4] マトリクスLED レンダリング
  matrix.fillScreen(0);
  unsigned long elapsedResult = now - resultStartTime;
  bool isShowingResult = (resultStartTime != 0);

  if (isShowingResult && lastFinishedRunner.isDnf) {
    if (elapsedResult < 2000) {
      if (elapsedResult < 1000) showStatus(lastFinishedRunner.id, matrix.Color(200, 200, 200));
      else showStatus("DNF", matrix.Color(255, 0, 0));
    } else { resultStartTime = 0; }
  }
  else if (isShowingResult && !lastFinishedRunner.isDnf) {
    if (elapsedResult < 15000) {
      if (elapsedResult < 1000) showStatus(lastFinishedRunner.id, matrix.Color(200, 200, 200));
      else showStatus(formatTime(lastFinishedRunner.result), matrix.Color(0, 255, 0));
    } else { resultStartTime = 0; }
  }
  else if (!runners.empty()) {
    float elapsed = (now - runners[0].startMillis) / 1000.0;
    matrix.setTextColor(matrix.Color(255, 100, 0)); matrix.setCursor(0, 1); matrix.print(runners.size());
    matrix.drawFastVLine(6, 0, 8, matrix.Color(30, 30, 30));
    String s = formatTime(elapsed);
    matrix.setTextColor(matrix.Color(255, 255, 255));
    matrix.setCursor(64 - (s.length() * 6), 1); matrix.print(s);
  }
  else {
    if (isSingleSensorMode) {
      showStatus("S-RDY:" + nextRiderID, matrix.Color(0, 255, 255));
    } else {
      showStatus("RDY:" + nextRiderID, matrix.Color(0, 255, 0));
    }
  }

  matrix.show();
  delay(20);
}
