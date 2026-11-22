/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Main Display Core Unit
 * * [Architecture Highlights]
 * 1. 先読みロック (Read-Ahead Lock):
 * 条件合致時に最優先でタイムスタンプを更新し、重複パケットを即座に破棄。
 * 2. 非同期遅延送信 (Asynchronous Delayed Transmission):
 * ゴール直後の輻輳を回避するため、タイム確定とハブへの送信を切り離し、
 * 1000ms後に非同期送出する。
 * 3. クロス・ロック (Cross-Lock Guard):
 * SOLOモード時、ゴール処理直後の3秒間は物理センサーの余韻に
 * よる意図しない「次のSTART」を強制ミュートする。
 * 4. SYNCモード (System Mode 2):
 * STARTセンサーではなく、SEQ_START受信から5秒後を絶対的な「0秒」
 * 起点としてタイマーを完全同期させる。
 * 5. SEQ_START レートリミット (Rate Limit Guard):
 * 一度受信してから SEQ_LOCK_MS(10秒) は後続のSEQ_STARTを無視する。
 * シグナル基板と同じ値にすることで、両基板の受付状態を一致させる。
 * 6. コマンドキュー (Command Queue):
 * ESP-NOW受信コールバックでは受信時刻を記録してキューに積むだけにし、
 * 実処理は loop() で行う。runners への同時アクセスによる破損を防ぐ。
 * (計測は受信時刻で行うので、loop()の処理待ちは計測値に影響しない)
 * 7. SYNCモード 出走確認 (Start Confirmation):
 * SYNCモードでもSTARTセンサーを「出走した証拠」として使う(タイム起点は緑のまま)。
 * 緑+START_WINDOW_MS以内にSTARTが無い選手は自動でDNSとして除外し、
 * STOPは出走確認済みの選手にのみ割り当てる。未出走者による結果ズレを防ぐ。
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
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

uint8_t hubAddress[] = {0x58, 0xE6, 0xC5, 0x12, 0x97, 0xCC}; // ESP-NOW 送信先ハブ

// システム・ガードタイム設定
const unsigned long TIMING_GUARD_MS = 3000;  // チャタリング・不正連続入力防止 (ms)
const unsigned long DELAY_SEND_MS   = 1000;  // ハブへの非同期遅延送信のディレイ時間 (ms)
const unsigned long SEQ_LOCK_MS     = 10000; // SEQ_START 多重受付ロック (シグナル基板と必ず同じ値)
const unsigned long START_WINDOW_MS = 5000;  // SYNC: 緑点灯からこの時間内にSTARTが無ければDNS

/* ====================================================================
 * ハードウェア・ピン定義
 * ==================================================================== */
#define LED_PIN       21
#define BTN_RESET     22

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(64, 8, LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

/* ====================================================================
 * コマンドキュー (ESP-NOWコールバック → loop)
 * ==================================================================== */
struct CmdMsg { char text[251]; unsigned long rxMillis; };
QueueHandle_t cmdQueue = NULL;

/* ====================================================================
 * 計測データ構造・ステート管理
 * ==================================================================== */
struct Runner { String id; unsigned long startMillis; float result; bool isDnf; bool started; };
std::vector<Runner> runners;
String nextRiderID = "X999";
unsigned long resultStartTime = 0;
Runner lastFinishedRunner;
String lastResultLabel = "DNF";      // DNF / DNS 表示用

unsigned long lastStartActionTime = 0;
unsigned long lastStopActionTime = 0;
unsigned long lastSeqStartTime = 0;
bool hasSeqStarted = false;          // 起動直後の1回目を必ず受け付けるため

// モード管理 (0: MULT, 1: SOLO, 2: SYNC)
int systemMode = 0;

// [非同期遅延送信用 キュー] (DNSとゴールが1秒以内に重なっても取りこぼさない)
struct PendingResult { String id; float timeVal; String status; unsigned long lockedAt; };
std::vector<PendingResult> pendingResults;

/* ====================================================================
 * ユーティリティ・コア関数
 * ==================================================================== */
String formatTime(float t) {
  if (t < 60.0) return String(t, 3);
  int m = (int)t / 60; int s = (int)t % 60; int ms = (int)((t - (int)t) * 1000);
  char buf[16]; sprintf(buf, "%d.%02d.%03d", m, s, ms); return String(buf);
}

void sendResultToHub(String id, float timeValue, String status) {
  String json = "{\"type\":\"RESULT\",\"id\":\"" + id + "\",\"time\":" + String(timeValue, 3);
  if (status != "OK") json += ",\"status\":\"" + status + "\"";   // DNF / DNS のときだけ付与
  json += "}";
  esp_now_send(hubAddress, (uint8_t *)json.c_str(), json.length());
  Serial.println("[TX_HUB] Delayed RESULT packet sent: " + json);
}

/**
 * ランナー完了処理（非同期送信キューへ登録）
 */
void completeRunner(Runner r, float timeResult, String status) {
  r.result = timeResult;
  r.isDnf = (status != "OK");
  lastFinishedRunner = r;
  lastResultLabel = status;
  resultStartTime = millis();
  pendingResults.push_back({r.id, r.result, status, millis()});
}

void handleDNF(unsigned long now) {
  if (!runners.empty()) {
    lastStopActionTime = now; // 優先的にロック確保
    completeRunner(runners[0], 999.999, "DNF");
    runners.erase(runners.begin());
  }
}

// 出走確認済みの先頭ランナーの位置 (いなければ -1)
int firstStartedRunner() {
  for (size_t i = 0; i < runners.size(); i++) if (runners[i].started) return (int)i;
  return -1;
}

void showStatus(String txt, uint16_t color) {
  matrix.setTextColor(color);
  int x = (64 - (txt.length() * 6)) / 2;
  matrix.setCursor(x >= 0 ? x : 0, 1); matrix.print(txt);
}

/* ====================================================================
 * 上位プロトコル・コマンドハンドラ (loop()からのみ呼ぶ)
 * now = コマンドを受信した時刻 (計測はこの時刻基準)
 * ==================================================================== */
void processCommand(String msg, unsigned long now) {
  msg.trim();

  if (msg.startsWith("{")) {
    JsonDocument doc;
    if (!deserializeJson(doc, msg)) {
      if (doc["type"] == "ENTRY") nextRiderID = doc["id"].as<String>();
    }
  }
  // SYNCモード時はSEQ_STARTでタイマーを同期登録
  else if (msg == "SEQ_START") {
    // SEQ_LOCK_MS 以内の連続受信は無視 (シグナル基板と同じロック時間)
    if (hasSeqStarted && now - lastSeqStartTime < SEQ_LOCK_MS) {
      Serial.println("[SEQ] Ignore: Sequence locked.");
      return;
    }
    hasSeqStarted = true;
    lastSeqStartTime = now;

    if (systemMode == 2) {
      if (runners.size() < 9) {
        runners.push_back({nextRiderID, now + 5000, 0.0, false, false}); // 5000ms後を0.000秒、出走未確認
        nextRiderID = "X999";
      }
    }
  }
  else if (msg == "START") {
    // SYNCモード: タイム起点は緑のまま。STARTは「出走確認」にだけ使う
    // (フライングでも出走扱い。FLY判定はシグナル基板側)
    if (systemMode == 2) {
      if (now - lastStartActionTime <= TIMING_GUARD_MS) return;   // センサーのチャタリング除去
      for (auto &r : runners) {
        if (!r.started && (long)now >= (long)r.startMillis - 5000) {  // SEQ_START以降のSTARTのみ
          r.started = true;
          lastStartActionTime = now;
          Serial.println("[SYNC] Start confirmed: " + r.id);
          break;
        }
      }
      return;
    }

    if (systemMode == 1 && !runners.empty()) {
      // [SOLOモード：ゴール時の処理]
      if (now - lastStopActionTime > TIMING_GUARD_MS && (long)now - (long)runners[0].startMillis > TIMING_GUARD_MS) {
        lastStopActionTime = now;
        completeRunner(runners[0], (float)((long)now - (long)runners[0].startMillis) / 1000.0, "OK");
        runners.erase(runners.begin());
      }
    } else {
      // [MULTモード：スタート または SOLOモード：新規スタート]
      if (now - lastStartActionTime > TIMING_GUARD_MS) {
        // クロス・ロック: SOLOモード時、ゴール直後の3秒間はSTARTを弾く
        if (systemMode == 1 && (now - lastStopActionTime < TIMING_GUARD_MS)) {
          return;
        }
        lastStartActionTime = now;
        if (runners.size() < 9) {
          runners.push_back({nextRiderID, now, 0.0, false, true});
          nextRiderID = "X999";
        }
      }
    }
  }
  else if (msg == "STOP") {
    // [全モード共通：ゴール時の処理]
    // 出走確認済みの先頭ランナーにだけ割り当てる (MULT/SOLOは全員確認済み)
    if (now - lastStopActionTime > TIMING_GUARD_MS) {
      int idx = firstStartedRunner();
      if (idx >= 0 && (long)now - (long)runners[idx].startMillis > TIMING_GUARD_MS) {
        lastStopActionTime = now;
        completeRunner(runners[idx], (float)((long)now - (long)runners[idx].startMillis) / 1000.0, "OK");
        runners.erase(runners.begin() + idx);
      }
    }
  }
  else if (msg == "FORCE_DNF") {
    handleDNF(now);
  }
}

/* ====================================================================
 * ESP-NOW 受信コールバック (受信時刻を付けてキューに積むだけ)
 * ==================================================================== */
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  CmdMsg m;
  m.rxMillis = millis();               // ★計測用の時刻はここで確定
  int n = len < 250 ? len : 250;
  memcpy(m.text, incomingData, n);
  m.text[n] = '\0';
  if (cmdQueue) xQueueSend(cmdQueue, &m, 0);
}

/* ====================================================================
 * 初期化ルーチン
 * ==================================================================== */
void setup() {
  Serial.begin(115200);
  pinMode(BTN_RESET, INPUT_PULLUP);
  matrix.begin(); matrix.setBrightness(100);

  cmdQueue = xQueueCreate(16, sizeof(CmdMsg));

  // ESP-NOW 初期化
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  WiFi.disconnect();
  Serial.println("[INFO] This board's MAC address: " + WiFi.macAddress());

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
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
  // [Task 0] 受信コマンドの処理 (ESP-NOW)
  CmdMsg m;
  while (xQueueReceive(cmdQueue, &m, 0) == pdTRUE) {
    Serial.println("[RX_ESPNOW] " + String(m.text));
    processCommand(String(m.text), m.rxMillis);
  }

  // [Task 0b] シリアルモニタからのコマンド (デバッグ用)
  static String serialBuf = "";
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length() > 0) {
        Serial.println("[RX_SERIAL] " + serialBuf);
        processCommand(serialBuf, millis());
        serialBuf = "";
      }
    } else if (serialBuf.length() < 250) {
      serialBuf += c;
    }
  }

  unsigned long now = millis();

  // [Task 1] SYNC 出走タイムアウト: 緑+START_WINDOW_MS までにSTARTが無ければDNS
  for (size_t i = 0; i < runners.size(); ) {
    if (!runners[i].started && (long)now - (long)runners[i].startMillis > (long)START_WINDOW_MS) {
      Serial.println("[SYNC] No start detected -> DNS: " + runners[i].id);
      completeRunner(runners[i], 999.999, "DNS");
      runners.erase(runners.begin() + i);
    } else {
      i++;
    }
  }

  // [Task 1b] 非同期遅延送信タスク (確定から1秒後にハブへ送信)
  for (size_t i = 0; i < pendingResults.size(); ) {
    if (now - pendingResults[i].lockedAt > DELAY_SEND_MS) {
      sendResultToHub(pendingResults[i].id, pendingResults[i].timeVal, pendingResults[i].status);
      pendingResults.erase(pendingResults.begin() + i);
    } else {
      i++;
    }
  }

  // [Task 2] 物理ボタン: 短押し=DNF / 2秒長押し=モード切替
  static bool lastBtnState = HIGH;
  static unsigned long btnPressTime = 0;
  static bool isLongPressed = false;
  bool currentBtnState = digitalRead(BTN_RESET);

  if (currentBtnState == LOW && lastBtnState == HIGH) {
    btnPressTime = now;
    isLongPressed = false;
  } else if (currentBtnState == LOW && lastBtnState == LOW) {
    if (!isLongPressed && (now - btnPressTime > 2000)) {
      systemMode = (systemMode + 1) % 3;
      matrix.fillScreen(0);
      if (systemMode == 0) showStatus("MULT", matrix.Color(0, 255, 0));
      else if (systemMode == 1) showStatus("SOLO", matrix.Color(255, 255, 0));
      else if (systemMode == 2) showStatus("SYNC", matrix.Color(255, 0, 255));
      matrix.show(); delay(1500);   // この間の受信はキューに溜まり、受信時刻で処理される
      isLongPressed = true;
    }
  } else if (currentBtnState == HIGH && lastBtnState == LOW) {
    if (!isLongPressed && (now - btnPressTime > 50)) {
      processCommand("FORCE_DNF", now);
    }
  }
  lastBtnState = currentBtnState;

  // [Task 3] マトリクスLED レンダリング
  now = millis();
  matrix.fillScreen(0);
  unsigned long elapsedResult = now - resultStartTime;
  bool isShowingResult = (resultStartTime != 0);

  if (isShowingResult && lastFinishedRunner.isDnf) {
    if (elapsedResult < 2000) {
      if (elapsedResult < 1000) showStatus(lastFinishedRunner.id, matrix.Color(200, 200, 200));
      else showStatus(lastResultLabel, matrix.Color(255, 0, 0));
    } else { resultStartTime = 0; }
  }
  else if (isShowingResult && !lastFinishedRunner.isDnf) {
    if (elapsedResult < 15000) {
      if (elapsedResult < 1000) showStatus(lastFinishedRunner.id, matrix.Color(200, 200, 200));
      else showStatus(formatTime(lastFinishedRunner.result), matrix.Color(0, 255, 0));
    } else { resultStartTime = 0; }
  }
  else if (!runners.empty()) {
    long diffMs = (long)now - (long)runners[0].startMillis;
    matrix.setTextColor(matrix.Color(255, 100, 0)); matrix.setCursor(0, 1); matrix.print(runners.size());
    matrix.drawFastVLine(6, 0, 8, matrix.Color(30, 30, 30));

    String s;
    if (diffMs < 0) {
      matrix.setTextColor(matrix.Color(255, 180, 0)); // マイナス待機中はオレンジ文字
      s = String((float)diffMs / 1000.0, 2);
    } else {
      matrix.setTextColor(matrix.Color(255, 255, 255));
      s = formatTime((float)diffMs / 1000.0);
    }
    matrix.setCursor(64 - (s.length() * 6), 1); matrix.print(s);
  }
  else {
    if (systemMode == 1) showStatus("S-RDY:" + nextRiderID, matrix.Color(0, 255, 255));
    else if (systemMode == 2) showStatus("Y-RDY:" + nextRiderID, matrix.Color(255, 0, 255));
    else showStatus("M-RDY:" + nextRiderID, matrix.Color(0, 255, 0));
  }

  matrix.show();
  delay(20);
}
