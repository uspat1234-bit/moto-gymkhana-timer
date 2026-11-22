/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Signal Board Core Unit
 * * [Architecture Highlights]
 * 1. 自律型カウントダウンシーケンス (Autonomous Countdown Sequence)
 * 2. キャストガード・フライング判定 (Cast-Guard Flying Detection)
 * 3. 共通エッジ・ステート管理: メイン基板同様、基本を X999 とし
 * ENTRY受信でIDを更新。ペナルティ検知時はIDを付与して送信。
 * 4. 多重起動ロック (Sequence Execution Lock):
 * 既にシグナルが進行している状態(isSequenceActive)で
 * 重複してSEQ_STARTを受信した場合、連打とみなして安全に破棄する。
 * 5. ノンブロッキング音声 (Non-blocking Audio Task):
 * 音声は専用FreeRTOSタスクで再生し、loop()を止めない。
 * LED表示を先に確定(matrix.show)させてから発音することで光と音を同期。
 * ====================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <driver/i2s.h>
#include <math.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

/* ====================================================================
 * ハードウェア・ピン定義
 * ==================================================================== */
#define LED_PIN       21
#define BTN_RESET     22

#define PIN_BCK       23
#define PIN_LCK       16
#define PIN_DIN       17
#define I2S_NUM       I2S_NUM_0

/* ====================================================================
 * サウンド設定 (ここを変えれば音の長さ・高さを調整できます)
 * ==================================================================== */
const float    COUNT_TONE_HZ = 440;    // 赤・黄の音
const uint16_t COUNT_TONE_MS = 150;
const float    GREEN_TONE_HZ = 880;    // 緑(スタート)の音
const uint16_t GREEN_TONE_MS = 1500;   // ★延長: 500ms → 1500ms

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(64, 8, LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

/* ====================================================================
 * シグナル制御構造・ステート管理
 * ==================================================================== */
uint8_t hubAddress[] = {0x58, 0xE6, 0xC5, 0x12, 0xXX, 0xYY};

unsigned long seqStartTime = 0;
unsigned long expectedGreenTime = 0;
unsigned long flyingStartTime = 0;

volatile bool isSequenceActive = false;
volatile bool hasStartedPhysical = false;

enum SignalState { STATE_READY, STATE_DELAY, STATE_RED1, STATE_RED2, STATE_YELLOW, STATE_GREEN, STATE_FLYING };
volatile SignalState currentSignalState = STATE_READY;

String nextRiderID = "X999";
String currentRiderId = "X999";

int16_t sine_table[256];

/* ====================================================================
 * 音声タスク (ノンブロッキング再生)
 * ==================================================================== */
struct ToneCmd { float freq; uint16_t ms; };
QueueHandle_t toneQueue = NULL;

// 表示確定後に鳴らす音の予約 (loop()内で matrix.show() の直後に発音)
enum PendingSound : uint8_t { SND_NONE, SND_COUNT, SND_GO, SND_PENALTY };
volatile uint8_t pendingSound = SND_NONE;

void queueTone(float freq, uint16_t ms) {
  if (!toneQueue) return;
  ToneCmd c = { freq, ms };
  xQueueSend(toneQueue, &c, 0);   // 満杯なら捨てる (呼び出し側を絶対に止めない)
}

void stopPendingTones() {
  if (toneQueue) xQueueReset(toneQueue);  // 未再生の音を破棄
}

/* ====================================================================
 * ユーティリティ・コア関数
 * ==================================================================== */
void showStatus(String txt, uint16_t color) {
  matrix.setTextColor(color);
  int x = (64 - (txt.length() * 6)) / 2;
  matrix.setCursor(x >= 0 ? x : 0, 1); matrix.print(txt);
}

void notifyControlHub(String eventType, String id, float diffSec) {
  String json = "{\"type\":\"" + eventType + "\",\"id\":\"" + id + "\",\"diff\":" + String(diffSec, 3) + "}";
  esp_now_send(hubAddress, (uint8_t *)json.c_str(), json.length());
  Serial.println("[NOTIFY] " + json);
}

// ※ 音声タスク内からのみ呼ばれる (ブロッキング)
void playTone(float frequency, unsigned long duration_ms) {
  size_t bw; unsigned long num_samples = (44100 * duration_ms) / 1000; int16_t buffer[1024]; uint32_t phase = 0; uint32_t phase_step = (frequency * 4294967296.0) / 44100.0;
  unsigned long samples_sent = 0;
  while (samples_sent < num_samples) {
    int to_send = min((unsigned long)512, num_samples - samples_sent);
    for (int i = 0; i < to_send; i++) {
      int env = 256; if (samples_sent + i < 1000) env = ((samples_sent + i) * 256) / 1000; else if (num_samples - (samples_sent + i) < 1000) env = ((num_samples - (samples_sent + i)) * 256) / 1000;
      buffer[i*2] = (sine_table[phase >> 24] * env) >> 8; buffer[i*2+1] = buffer[i*2]; phase += phase_step;
    }
    i2s_write(I2S_NUM, buffer, to_send * 4, &bw, portMAX_DELAY); samples_sent += to_send;
  }
  i2s_zero_dma_buffer(I2S_NUM);
}

void audioTask(void *param) {
  ToneCmd c;
  for (;;) {
    if (xQueueReceive(toneQueue, &c, portMAX_DELAY) == pdTRUE) {
      playTone(c.freq, c.ms);
    }
  }
}

void playPenaltySound() {   // キューに積むだけなので即座に戻る
  for(int i = 0; i < 6; i++) {
    queueTone(2000, 60);
    queueTone(1400, 60);
  }
  queueTone(800, 400);
}

void dispatchSound(uint8_t snd) {
  switch (snd) {
    case SND_COUNT:   queueTone(COUNT_TONE_HZ, COUNT_TONE_MS); break;
    case SND_GO:      queueTone(GREEN_TONE_HZ, GREEN_TONE_MS); break;
    case SND_PENALTY: stopPendingTones(); playPenaltySound(); break;
    default: break;
  }
}

/* ====================================================================
 * 上位プロトコル・コマンドハンドラ
 * ==================================================================== */
void processCommand(String rawMsg) {
  String msg = rawMsg; msg.trim();

  if (msg.startsWith("{")) {
    JsonDocument doc;
    if (!deserializeJson(doc, msg)) {
      if (doc["type"] == "ENTRY") nextRiderID = doc["id"].as<String>();
    }
    return;
  }

  if (msg == "SEQ_START") {
    // 多重起動ロック (連打ガード)
    if (isSequenceActive) {
      Serial.println("[SEQ] Ignore: Sequence already running.");
      return;
    }

    currentRiderId = nextRiderID;
    nextRiderID = "X999";

    seqStartTime = millis();
    expectedGreenTime = seqStartTime + 5000;
    hasStartedPhysical = false;
    currentSignalState = STATE_DELAY;
    isSequenceActive = true;
    Serial.println("[SEQ] Start sequence initiated for ID: " + currentRiderId);
  }
  else if (msg == "START") {
    if (!isSequenceActive || hasStartedPhysical) return;

    hasStartedPhysical = true;
    long diffMs = (long)millis() - (long)expectedGreenTime;
    float diffSec = (float)diffMs / 1000.0;

    if (diffMs < 0) {
      flyingStartTime = millis();
      currentSignalState = STATE_FLYING;
      notifyControlHub("FLYING", currentRiderId, diffSec);
      pendingSound = SND_PENALTY;   // 赤画面の表示確定後に鳴らす
    } else {
      notifyControlHub("REACTION", currentRiderId, diffSec);
    }
  }
  else if (msg == "FORCE_DNF") {
    isSequenceActive = false;
    currentSignalState = STATE_READY;
    pendingSound = SND_NONE;
    stopPendingTones();
  }
}

/* ====================================================================
 * ESP-NOW 受信コールバック
 * ==================================================================== */
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  char msg[len + 1]; memcpy(msg, incomingData, len); msg[len] = '\0'; processCommand(String(msg));
}

/* ====================================================================
 * 初期化ルーチン
 * ==================================================================== */
void setup() {
  Serial.begin(115200); pinMode(BTN_RESET, INPUT_PULLUP); matrix.begin(); matrix.setBrightness(100);
  for (int i = 0; i < 256; i++) sine_table[i] = (int16_t)(15000.0 * sin(2.0 * M_PI * i / 256.0));

  // DAC 音声ライン初期化
  i2s_config_t i2s_config = { .mode = (i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_TX), .sample_rate = 44100, .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, .communication_format = I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, .dma_buf_count = 8, .dma_buf_len = 64, .use_apll = false, .tx_desc_auto_clear = true };
  i2s_pin_config_t pin_config = { .bck_io_num = PIN_BCK, .ws_io_num = PIN_LCK, .data_out_num = PIN_DIN, .data_in_num = I2S_PIN_NO_CHANGE };
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL); i2s_set_pin(I2S_NUM, &pin_config); i2s_zero_dma_buffer(I2S_NUM);

  // 音声タスク起動 (ESP-NOWより先に用意しておく)
  toneQueue = xQueueCreate(32, sizeof(ToneCmd));
  xTaskCreate(audioTask, "audio", 6144, NULL, 3, NULL);

  // ESP-NOW 初期化
  WiFi.mode(WIFI_STA); esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); WiFi.disconnect();
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_peer_info_t peer = {}; memcpy(peer.peer_addr, hubAddress, 6); peer.channel = 1; esp_now_add_peer(&peer);
  }
}

/* ====================================================================
 * メインループ（スケジューラ）
 * ==================================================================== */
void loop() {
  unsigned long now = millis();

  // [Task 0] シリアルモニタからのコマンド受付 (デバッグ用・ノンブロッキング)
  //   例: SEQ_START / START / FORCE_DNF / {"type":"ENTRY","id":"A001"}
  static String serialBuf = "";
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length() > 0) {
        Serial.println("[SERIAL] " + serialBuf);
        processCommand(serialBuf);
        serialBuf = "";
      }
    } else if (serialBuf.length() < 256) {
      serialBuf += c;
    }
  }

  // [Task 1] シグナル状態の自動進行 (音は予約のみ。発音は表示確定後)
  if (isSequenceActive) {
    if (currentSignalState != STATE_FLYING) {
      long elapsed = now - seqStartTime;
      if (elapsed >= 2000 && elapsed < 3000 && currentSignalState != STATE_RED1) { currentSignalState = STATE_RED1; pendingSound = SND_COUNT; }
      else if (elapsed >= 3000 && elapsed < 4000 && currentSignalState != STATE_RED2) { currentSignalState = STATE_RED2; pendingSound = SND_COUNT; }
      else if (elapsed >= 4000 && elapsed < 5000 && currentSignalState != STATE_YELLOW) { currentSignalState = STATE_YELLOW; pendingSound = SND_COUNT; }
      else if (elapsed >= 5000 && currentSignalState != STATE_GREEN) { currentSignalState = STATE_GREEN; pendingSound = SND_GO; }

      if (currentSignalState == STATE_GREEN && elapsed > 15000) {
        isSequenceActive = false; currentSignalState = STATE_READY;
      }
    } else {
      if (now - flyingStartTime > 3000) {
        isSequenceActive = false; currentSignalState = STATE_READY;
      }
    }
  }

  // [Task 2] 物理ボタン (50msデバウンス)
  static bool lastBtnState = HIGH; static unsigned long lastBtnChange = 0;
  bool currentBtnState = digitalRead(BTN_RESET);
  if (currentBtnState != lastBtnState && now - lastBtnChange > 50) {
    lastBtnChange = now;
    if (currentBtnState == LOW) processCommand("FORCE_DNF");
    lastBtnState = currentBtnState;
  }

  // [Task 3] マトリクスLED レンダリング (変化があった時だけ描画)
  uint8_t snd = pendingSound; pendingSound = SND_NONE;   // 描画前に予約音を取得

  static int renderedState = -1;
  static String renderedNext = "", renderedCur = "";
  SignalState st = currentSignalState;

  if ((int)st != renderedState || nextRiderID != renderedNext || currentRiderId != renderedCur) {
    matrix.fillScreen(0);
    uint16_t c_dim = matrix.Color(10, 10, 10), c_red = matrix.Color(255, 0, 0), c_yellow = matrix.Color(255, 180, 0), c_green = matrix.Color(0, 255, 0);

    if (st == STATE_READY) {
      showStatus("RDY:" + nextRiderID, matrix.Color(0, 255, 255));
    }
    else if (st == STATE_DELAY) {
      showStatus("SQ:" + currentRiderId, matrix.Color(255, 128, 0));
    }
    else if (st == STATE_FLYING) {
      matrix.fillScreen(c_red);
      showStatus("FLY", matrix.Color(255, 255, 255));
    }
    else {
      matrix.fillRect(2, 0, 12, 8, c_dim); matrix.fillRect(18, 0, 12, 8, c_dim); matrix.fillRect(34, 0, 12, 8, c_dim); matrix.fillRect(50, 0, 12, 8, c_dim);
      if (st == STATE_RED1) matrix.fillRect(2, 0, 12, 8, c_red);
      else if (st == STATE_RED2) { matrix.fillRect(2, 0, 12, 8, c_red); matrix.fillRect(18, 0, 12, 8, c_red); }
      else if (st == STATE_YELLOW) { matrix.fillRect(2, 0, 12, 8, c_red); matrix.fillRect(18, 0, 12, 8, c_red); matrix.fillRect(34, 0, 12, 8, c_yellow); }
      else if (st == STATE_GREEN) { matrix.fillRect(2, 0, 12, 8, c_green); matrix.fillRect(18, 0, 12, 8, c_green); matrix.fillRect(34, 0, 12, 8, c_green); matrix.fillRect(50, 0, 12, 8, c_green); }
    }
    matrix.show();   // ← ここで光が確定

    renderedState = (int)st; renderedNext = nextRiderID; renderedCur = currentRiderId;
  }

  // [Task 4] 光が出た直後に発音 (音声タスクへ投げるだけ・待たない)
  if (snd != SND_NONE) dispatchSound(snd);

  delay(1);
}
