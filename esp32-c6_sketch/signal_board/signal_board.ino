/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Signal Board Core Unit
 * * [Architecture Highlights]
 * 1. 自律型カウントダウンシーケンス (Autonomous Countdown Sequence):
 * ハブからの開始トリガーを受信後、内部ステートマシンにより
 * 赤・黄・緑のシグナル遷移と音声出力をミリ秒精度で厳密に制御。
 * 2. キャストガード・フライング判定 (Cast-Guard Flying Detection):
 * スタートセンサー検知時、符号付き整数への強制キャストを挟むことで
 * タイマーのアンダーフローを完全に防ぎ、ミリ秒単位の正確な差分を検出。
 * ====================================================================
 */

#include <SPI.h>
#include <Ethernet.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <driver/i2s.h>
#include <math.h>

/* ====================================================================
 * L3 ネットワーク構成 (有線LAN / W5500)
 * ==================================================================== */
IPAddress ip_eth(192, 168, 1, 30);      
IPAddress dns(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x30 }; 
const int UDP_PORT   = 5005;            

/* ====================================================================
 * ハードウェア・ピン定義
 * ==================================================================== */
#define LED_PIN       21  
#define BTN_RESET     22  

#define ETH_CS_PIN    1   
#define ETH_RST_PIN   2   
#define SPI_SCK       19  
#define SPI_MISO      20  
#define SPI_MOSI      18  

#define PIN_BCK       23  
#define PIN_LCK       16  
#define PIN_DIN       17  
#define I2S_NUM       I2S_NUM_0

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(64, 8, LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

EthernetUDP ethUdp;
char packetBuffer[512]; 

/* ====================================================================
 * シグナル制御構造・ステート管理
 * ==================================================================== */
uint8_t hubAddress[] = {0x58, 0xE6, 0xC5, 0x12, 0x97, 0xCC};

unsigned long seqStartTime = 0;
unsigned long expectedGreenTime = 0;
unsigned long flyingStartTime = 0;

bool isSequenceActive = false;
bool hasStartedPhysical = false;

enum SignalState { STATE_READY, STATE_DELAY, STATE_RED1, STATE_RED2, STATE_YELLOW, STATE_GREEN, STATE_FLYING };
SignalState currentSignalState = STATE_READY;

int16_t sine_table[256];

/* ====================================================================
 * ユーティリティ・コア関数
 * ==================================================================== */
void showStatus(String txt, uint16_t color) {
  matrix.setTextColor(color);
  int x = (64 - (txt.length() * 6)) / 2; 
  matrix.setCursor(x >= 0 ? x : 0, 1); matrix.print(txt);
}

void notifyControlHub(String eventType, float diffSec) {
  String json = "{\"type\":\"" + eventType + "\",\"diff\":" + String(diffSec, 3) + "}";
  esp_now_send(hubAddress, (uint8_t *)json.c_str(), json.length());
  Serial.println("[NOTIFY] " + json);
}

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

void playPenaltySound() {
  for(int i = 0; i < 6; i++) {
    playTone(2000, 60); 
    playTone(1400, 60); 
  }
  playTone(800, 400);
}

/* ====================================================================
 * 上位プロトコル・コマンドハンドラ
 * ==================================================================== */
void processCommand(String rawMsg) {
  String msg = rawMsg; msg.trim(); 
  
  if (msg == "SEQ_START") {
    seqStartTime = millis();
    expectedGreenTime = seqStartTime + 5000; 
    isSequenceActive = true;
    hasStartedPhysical = false;
    currentSignalState = STATE_DELAY;
    Serial.println("[SEQ] Start sequence initiated.");
  }
  else if (msg == "START") {
    if (!isSequenceActive || hasStartedPhysical) return;
    
    hasStartedPhysical = true; 
    long diffMs = (long)millis() - (long)expectedGreenTime;
    float diffSec = (float)diffMs / 1000.0; 
    
    if (diffMs < 0) { 
      currentSignalState = STATE_FLYING; 
      flyingStartTime = millis();
      notifyControlHub("FLYING", diffSec); 
      playPenaltySound(); 
    } else {
      notifyControlHub("REACTION", diffSec); 
    }
  } 
  else if (msg == "FORCE_DNF") { 
    isSequenceActive = false;
    currentSignalState = STATE_READY; 
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

  // W5500 初期化
  pinMode(ETH_RST_PIN, OUTPUT);
  digitalWrite(ETH_RST_PIN, LOW); delay(100);
  digitalWrite(ETH_RST_PIN, HIGH); delay(500); 
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, ETH_CS_PIN);
  Ethernet.init(ETH_CS_PIN);
  Ethernet.begin(mac, ip_eth, dns, gateway, subnet);
  
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("W5500 not found.");
  } else {
    ethUdp.begin(UDP_PORT);
  }

  // ESP-NOW 初期化
  WiFi.mode(WIFI_STA); esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); WiFi.disconnect();
  if (esp_now_init() == ESP_OK) { 
    esp_now_register_recv_cb(OnDataRecv); 
    esp_now_peer_info_t peer = {}; memcpy(peer.peer_addr, hubAddress, 6); peer.channel = 1; esp_now_add_peer(&peer); 
  }

  // DAC 音声ライン初期化
  i2s_config_t i2s_config = { .mode = (i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_TX), .sample_rate = 44100, .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, .communication_format = I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, .dma_buf_count = 8, .dma_buf_len = 64, .use_apll = false, .tx_desc_auto_clear = true };
  i2s_pin_config_t pin_config = { .bck_io_num = PIN_BCK, .ws_io_num = PIN_LCK, .data_out_num = PIN_DIN, .data_in_num = I2S_PIN_NO_CHANGE };
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL); i2s_set_pin(I2S_NUM, &pin_config); i2s_zero_dma_buffer(I2S_NUM);
}

/* ====================================================================
 * メインループ（スケジューラ）
 * ==================================================================== */
void loop() {
  unsigned long now = millis();

  // [Task 1] 有線LANルート パケットチェック
  int pEth = ethUdp.parsePacket(); 
  if (pEth) { 
    int len = ethUdp.read(packetBuffer, 511); 
    if (len > 0) { packetBuffer[len] = 0; processCommand(String(packetBuffer)); } 
  }

  // [Task 2] シグナル状態の自動進行
  if (isSequenceActive) {
    if (currentSignalState != STATE_FLYING) {
      long elapsed = now - seqStartTime;
      if (elapsed >= 2000 && elapsed < 3000 && currentSignalState != STATE_RED1) { currentSignalState = STATE_RED1; playTone(440, 150); }
      else if (elapsed >= 3000 && elapsed < 4000 && currentSignalState != STATE_RED2) { currentSignalState = STATE_RED2; playTone(440, 150); }
      else if (elapsed >= 4000 && elapsed < 5000 && currentSignalState != STATE_YELLOW) { currentSignalState = STATE_YELLOW; playTone(440, 150); }
      else if (elapsed >= 5000 && currentSignalState != STATE_GREEN) { currentSignalState = STATE_GREEN; playTone(880, 500); }
      
      if (currentSignalState == STATE_GREEN && elapsed > 15000) {
        isSequenceActive = false; currentSignalState = STATE_READY;
      }
    } else {
      if (now - flyingStartTime > 3000) {
        isSequenceActive = false; currentSignalState = STATE_READY;
      }
    }
  }

  // [Task 3] 物理ボタンデバウンス処理
  static bool lastBtnState = HIGH; bool currentBtnState = digitalRead(BTN_RESET);
  if (currentBtnState == LOW && lastBtnState == HIGH) processCommand("FORCE_DNF");
  lastBtnState = currentBtnState;

  // [Task 4] マトリクスLED レンダリング
  matrix.fillScreen(0); 
  uint16_t c_dim = matrix.Color(10, 10, 10), c_red = matrix.Color(255, 0, 0), c_yellow = matrix.Color(255, 180, 0), c_green = matrix.Color(0, 255, 0);

  if (currentSignalState == STATE_READY) {
    showStatus("READY", matrix.Color(0, 255, 255)); 
  } 
  else if (currentSignalState == STATE_FLYING) {
    matrix.fillScreen(c_red);
    showStatus("FLY", matrix.Color(255, 255, 255)); 
  } 
  else {
    matrix.fillRect(2, 0, 12, 8, c_dim); matrix.fillRect(18, 0, 12, 8, c_dim); matrix.fillRect(34, 0, 12, 8, c_dim); matrix.fillRect(50, 0, 12, 8, c_dim);
    if (currentSignalState == STATE_RED1) matrix.fillRect(2, 0, 12, 8, c_red); 
    else if (currentSignalState == STATE_RED2) { matrix.fillRect(2, 0, 12, 8, c_red); matrix.fillRect(18, 0, 12, 8, c_red); }
    else if (currentSignalState == STATE_YELLOW) { matrix.fillRect(2, 0, 12, 8, c_red); matrix.fillRect(18, 0, 12, 8, c_red); matrix.fillRect(34, 0, 12, 8, c_yellow); }
    else if (currentSignalState == STATE_GREEN) { matrix.fillRect(2, 0, 12, 8, c_green); matrix.fillRect(18, 0, 12, 8, c_green); matrix.fillRect(34, 0, 12, 8, c_green); matrix.fillRect(50, 0, 12, 8, c_green); }
  }
  matrix.show(); 

  delay(20); 
}
