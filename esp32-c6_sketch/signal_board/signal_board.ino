/*
 * ====================================================================
 * MGTS (Moto Gymkhana Timing System) - Signal Board Core Unit (Hub Linked)
 * ====================================================================
 */

#include <SPI.h>
#include <Ethernet.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <vector>
#include <ArduinoJson.h>
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

// コントロールハブのMACアドレス
uint8_t hubAddress[] = {0x58, 0xE6, 0xC5, 0x12, 0x97, 0xCC};

const unsigned long TIMING_GUARD_MS = 3000; 
const unsigned long DELAY_SEND_MS = 1000;   

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

struct Runner { String id; unsigned long startMillis; float result; bool isDnf; };
std::vector<Runner> runners;         
String nextRiderID = "X999";         
unsigned long resultStartTime = 0;   
Runner lastFinishedRunner;           

unsigned long lastStartActionTime = 0; 
unsigned long lastStopActionTime = 0;  
bool isSingleSensorMode = false;     

bool pendingResultSend = false;      
String pendingResultId = "";         
float pendingResultTimeVal = 0.0;    
unsigned long resultLockedAt = 0;    

enum SignalState {
  STATE_READY,
  STATE_DELAY,
  STATE_RED1,
  STATE_RED2,
  STATE_YELLOW,
  STATE_GREEN,
  STATE_RUNNING,
  STATE_FLYING
};
SignalState currentSignalState = STATE_READY;

unsigned long expectedGreenTime = 0; 

int16_t sine_table[256];

/* ====================================================================
 * ユーティリティ関数
 * ==================================================================== */
String formatTime(float t) {
  if (t < 60.0) return String(t, 3);
  int m = (int)t / 60; int s = (int)t % 60; int ms = (int)((t - (int)t) * 1000);
  char buf[16]; sprintf(buf, "%d.%02d.%03d", m, s, ms); return String(buf);
}

void notifyControlHub(String eventType, float diffSec) {
  String json = "{\"type\":\"" + eventType + "\",\"diff\":" + String(diffSec, 3) + "}";
  esp_now_send(hubAddress, (uint8_t *)json.c_str(), json.length());
  IPAddress bc(255, 255, 255, 255);
  ethUdp.beginPacket(bc, UDP_PORT);
  ethUdp.print(json);
  ethUdp.endPacket();
  Serial.println("[NOTIFY] " + json);
}

void sendResultToPC(String id, float timeValue) {
  String json = "{\"type\":\"RESULT\",\"id\":\"" + id + "\",\"time\":" + String(timeValue, 3) + "}";
  IPAddress bc(255, 255, 255, 255);
  ethUdp.beginPacket(bc, UDP_PORT); 
  ethUdp.print(json); 
  ethUdp.endPacket();
}

void completeRunner(Runner r, float timeResult, bool dnf) {
  r.result = timeResult; 
  r.isDnf = dnf; 
  lastFinishedRunner = r; 
  resultStartTime = millis();
  
  pendingResultId = r.id;
  pendingResultTimeVal = r.result;
  resultLockedAt = millis();
  pendingResultSend = true; 
}

void handleDNF() {
  if (!runners.empty()) {
    lastStopActionTime = millis(); 
    completeRunner(runners[0], 999.999, true);
    runners.erase(runners.begin());
  }
}

void showStatus(String txt, uint16_t color) {
  matrix.setTextColor(color);
  int x = (64 - (txt.length() * 6)) / 2; 
  matrix.setCursor(x >= 0 ? x : 0, 1); matrix.print(txt);
}

void playTone(float frequency, unsigned long duration_ms) {
  size_t bytes_written;
  unsigned long num_samples = (44100 * duration_ms) / 1000;
  const int BUF_SIZE = 1024; 
  int16_t buffer[BUF_SIZE * 2];
  
  uint32_t phase = 0;
  uint32_t phase_step = (frequency * 4294967296.0) / 44100.0; 
  
  unsigned long samples_sent = 0;
  while (samples_sent < num_samples) {
    int to_send = min((unsigned long)BUF_SIZE, num_samples - samples_sent);
    for (int i = 0; i < to_send; i++) {
      unsigned long current_sample = samples_sent + i;
      int env = 256; 
      if (current_sample < 1000) env = (current_sample * 256) / 1000;
      else if (num_samples - current_sample < 1000) env = ((num_samples - current_sample) * 256) / 1000;
      
      int table_idx = phase >> 24; 
      int16_t sample = (sine_table[table_idx] * env) >> 8;
      buffer[i * 2] = sample; buffer[i * 2 + 1] = sample; 
      phase += phase_step; 
    }
    i2s_write(I2S_NUM, buffer, to_send * sizeof(int16_t) * 2, &bytes_written, portMAX_DELAY);
    samples_sent += to_send;
  }
  i2s_zero_dma_buffer(I2S_NUM);
}

void playPenaltySound() {
  for(int i = 0; i < 4; i++) {
    playTone(300, 150);
    playTone(450, 150);
  }
}

void updateDisplay() {
  unsigned long now = millis();
  matrix.fillScreen(0); 
  
  uint16_t c_dim    = matrix.Color(10, 10, 10); 
  uint16_t c_red    = matrix.Color(255, 0, 0);
  uint16_t c_yellow = matrix.Color(255, 180, 0);
  uint16_t c_green  = matrix.Color(0, 255, 0);

  matrix.fillRect(2, 0, 12, 8, c_dim);
  matrix.fillRect(18, 0, 12, 8, c_dim);
  matrix.fillRect(34, 0, 12, 8, c_dim);
  matrix.fillRect(50, 0, 12, 8, c_dim);

  if (currentSignalState == STATE_RED1) {
    matrix.fillRect(2, 0, 12, 8, c_red); 
  }
  else if (currentSignalState == STATE_RED2) {
    matrix.fillRect(2, 0, 12, 8, c_red); 
    matrix.fillRect(18, 0, 12, 8, c_red);
  }
  else if (currentSignalState == STATE_YELLOW) {
    matrix.fillRect(2, 0, 12, 8, c_red); 
    matrix.fillRect(18, 0, 12, 8, c_red);
    matrix.fillRect(34, 0, 12, 8, c_yellow);
  }
  else if (currentSignalState == STATE_GREEN) {
    matrix.fillRect(2, 0, 12, 8, c_green); 
    matrix.fillRect(18, 0, 12, 8, c_green);
    matrix.fillRect(34, 0, 12, 8, c_green);
    matrix.fillRect(50, 0, 12, 8, c_green);
  }
  else if (currentSignalState == STATE_FLYING) {
    matrix.fillRect(2, 0, 12, 8, c_red); 
    matrix.fillRect(18, 0, 12, 8, c_red);
    matrix.fillRect(34, 0, 12, 8, c_red);
    matrix.fillRect(50, 0, 12, 8, c_red);
  }
  else if (currentSignalState == STATE_RUNNING) {
    matrix.fillScreen(0); 
    if (!runners.empty()) {
      float elapsed = (now - runners[0].startMillis) / 1000.0;
      matrix.setTextColor(matrix.Color(255, 255, 255));
      String s = formatTime(elapsed); 
      matrix.setCursor(64 - (s.length() * 6), 1); 
      matrix.print(s);
    }
  }
  else if (currentSignalState == STATE_READY || currentSignalState == STATE_DELAY) {
    matrix.fillScreen(0); 
    unsigned long elapsedResult = now - resultStartTime;
    if (resultStartTime != 0 && elapsedResult < 15000) {
      showStatus(formatTime(lastFinishedRunner.result), c_green);
    } else {
      resultStartTime = 0;
      showStatus("READY", c_green);
    }
  }
  matrix.show(); 
}

void processCommand(String msg) {
  unsigned long now = millis();
  
  if (msg.startsWith("{")) {
    JsonDocument doc;
    if (!deserializeJson(doc, msg)) {
      if (doc["type"] == "ENTRY") nextRiderID = doc["id"].as<String>();
    }
  } 
  else if (msg == "SEQ_START") {
    if (currentSignalState == STATE_READY) {
      currentSignalState = STATE_DELAY;
      unsigned long delay_start = millis();
      expectedGreenTime = delay_start + 5000; 
      Serial.println("[SEQ] Sequence Triggered. Expected GREEN at +" + String(expectedGreenTime - now) + "ms");
    }
  }
  else if (msg == "START") {
    if (currentSignalState >= STATE_DELAY && currentSignalState <= STATE_GREEN) {
      float diffSec = (float)(now - expectedGreenTime) / 1000.0; 
      
      if (diffSec < 0.0) {
        currentSignalState = STATE_FLYING;
        notifyControlHub("FLYING", diffSec); 
        updateDisplay();
        playPenaltySound();
        return; 
      } else {
        if (currentSignalState == STATE_GREEN) {
          currentSignalState = STATE_RUNNING;
          notifyControlHub("REACTION", diffSec); 
        }
      }
    }
    
    if (isSingleSensorMode && !runners.empty()) {
      if (now - lastStopActionTime > TIMING_GUARD_MS && now - runners[0].startMillis > TIMING_GUARD_MS) {
        lastStopActionTime = now; 
        completeRunner(runners[0], (now - runners[0].startMillis) / 1000.0, false);
        runners.erase(runners.begin());
      }
    } else {
      if (now - lastStartActionTime > TIMING_GUARD_MS) {
        if (isSingleSensorMode && (now - lastStopActionTime < TIMING_GUARD_MS)) return; 
        lastStartActionTime = now; 
        if (runners.size() < 9) {
          runners.push_back({nextRiderID, now, 0.0, false});
          nextRiderID = "X999"; 
        }
      }
    }
  } 
  else if (msg == "STOP") {
    currentSignalState = STATE_READY; 
    if (now - lastStopActionTime > TIMING_GUARD_MS) {
      if (!runners.empty() && now - runners[0].startMillis > TIMING_GUARD_MS) {
        lastStopActionTime = now; 
        completeRunner(runners[0], (now - runners[0].startMillis) / 1000.0, false);
        runners.erase(runners.begin());
      }
    }
  }
  else if (msg == "FORCE_DNF") { 
    currentSignalState = STATE_READY;
    handleDNF(); 
  }
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  char msg[len + 1];
  memcpy(msg, incomingData, len);
  msg[len] = '\0';
  processCommand(String(msg));
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_RESET, INPUT_PULLUP);
  matrix.begin(); matrix.setBrightness(100);

  for (int i = 0; i < 256; i++) {
    sine_table[i] = (int16_t)(2500.0 * sin(2.0 * M_PI * i / 256.0));
  }

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

  WiFi.mode(WIFI_STA); 
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); 
  WiFi.disconnect();
  
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, hubAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = PIN_BCK,
    .ws_io_num = PIN_LCK,
    .data_out_num = PIN_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM);
}

void loop() {
  unsigned long now = millis();
  static unsigned long last_state_update = 0;

  switch(currentSignalState) {
    case STATE_READY:
      last_state_update = now;
      break;

    case STATE_DELAY:
      if (now - last_state_update > 2000) { 
        currentSignalState = STATE_RED1;
        last_state_update = now;
        updateDisplay();    
        playTone(440, 150); 
      }
      break;
      
    case STATE_RED1:
      if (now - last_state_update > 1000) { 
        currentSignalState = STATE_RED2;
        last_state_update = now;
        updateDisplay();
        playTone(440, 150); 
      }
      break;
      
    case STATE_RED2:
      if (now - last_state_update > 1000) { 
        currentSignalState = STATE_YELLOW;
        last_state_update = now;
        updateDisplay();
        playTone(440, 150); 
      }
      break;
      
    case STATE_YELLOW:
      if (now - last_state_update > 1000) { 
        currentSignalState = STATE_GREEN;
        last_state_update = now;
        updateDisplay();         
        processCommand("START"); 
        playTone(880, 500);      
      }
      break;
      
    case STATE_GREEN:
      if (now - last_state_update > 1500) { 
        currentSignalState = STATE_RUNNING;
        last_state_update = now;
      }
      break;

    case STATE_FLYING:
      if (now - last_state_update > 3000) {
        currentSignalState = STATE_READY;
        last_state_update = now;
      }
      break;
      
    case STATE_RUNNING:
      break;
  }

  int pEth = ethUdp.parsePacket();
  if (pEth) {
    int len = ethUdp.read(packetBuffer, 511);
    if (len > 0) {
      packetBuffer[len] = 0;
      processCommand(String(packetBuffer));
    }
  }

  if (pendingResultSend && (now - resultLockedAt > DELAY_SEND_MS)) {
    sendResultToPC(pendingResultId, pendingResultTimeVal);
    pendingResultSend = false; 
  }

  static bool lastBtnState = HIGH;
  static unsigned long btnPressTime = 0;
  bool currentBtnState = digitalRead(BTN_RESET);
  if (currentBtnState == LOW && lastBtnState == HIGH) {
    btnPressTime = now; 
  } else if (currentBtnState == HIGH && lastBtnState == LOW) {
    unsigned long pressDuration = now - btnPressTime;
    if (pressDuration > 2000) {
      isSingleSensorMode = !isSingleSensorMode;
      matrix.fillScreen(0);
      showStatus(isSingleSensorMode ? "SINGLE ON" : "SINGLE OFF", matrix.Color(255, 255, 0));
      matrix.show(); delay(1500); 
    } else if (pressDuration > 50) {
      Serial.println("[BTN] 手動操作による強制リセット(DNF)を実行します");
      processCommand("FORCE_DNF");
    }
  }
  lastBtnState = currentBtnState;

  updateDisplay();
  delay(20); 
}
