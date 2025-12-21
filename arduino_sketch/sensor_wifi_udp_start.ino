#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h> // Wi-Fiの詳細情報を取得するために必要

// --- ★設定エリア★ ---

// 1. Wi-Fi設定 (スマホのテザリング情報)
const char* SSID     = "motogym";
const char* PASSWORD = "password123";

// 2. センサーの役割 ("START" または "STOP")
const String SENSOR_ROLE = "START"; 

// 3. ピン設定 (Seeed Studio XIAO ESP32C6)
const int PIN_SENSOR = 0;   // D0
const int PIN_LED    = 15;  // オンボードLED

// 4. 通信設定
const int UDP_PORT = 5005;

// 5. センサー不感時間 (ミリ秒)
const int SENSOR_COOLDOWN_MS = 2000; 
// --------------------

WiFiUDP udp;
int lastSensorState = HIGH;
unsigned long lastHeartbeatTime = 0;
unsigned long lastTriggerTime = 0;

void connectToWiFi();
void sendUdpPacket(String message);
void sendHeartbeat();
void printConnectionDetails();

// プロトコル名を文字列で返すヘルパー関数
String getProtocolString() {
  uint8_t protocol_bitmap;
  esp_wifi_get_protocol(WIFI_IF_STA, &protocol_bitmap);
  
  if (protocol_bitmap & WIFI_PROTOCOL_11AX) return "11ax"; // Wi-Fi 6
  if (protocol_bitmap & WIFI_PROTOCOL_11N) return "11n";   // Wi-Fi 4
  if (protocol_bitmap & WIFI_PROTOCOL_11G) return "11g";
  if (protocol_bitmap & WIFI_PROTOCOL_11B) return "11b";
  return "unknown";
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_SENSOR, INPUT_PULLUP); 
  pinMode(PIN_LED, OUTPUT);

  connectToWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED, LOW);
    connectToWiFi();
  } else {
    digitalWrite(PIN_LED, HIGH); 
  }

  int currentSensorState = digitalRead(PIN_SENSOR);
  unsigned long currentTime = millis();

  // 反応検知
  if (lastSensorState == HIGH && currentSensorState == LOW && (currentTime - lastTriggerTime > SENSOR_COOLDOWN_MS)) {
    Serial.println("Sensor Active!");
    
    lastTriggerTime = currentTime;
    digitalWrite(PIN_LED, LOW);
    
    String msg = SENSOR_ROLE;
    // START版なので特別な変換は不要ですが、一応残しておきます
    if (msg == "GOAL") msg = "STOP";
    
    Serial.print("Sending UDP: ");
    Serial.println(msg);
    sendUdpPacket(msg);
    
    delay(100); 
    digitalWrite(PIN_LED, HIGH);
  }

  lastSensorState = currentSensorState;
  
  // 死活監視
  // 3秒ごとに送信
  if (millis() - lastHeartbeatTime > 3000) {
    sendHeartbeat();
    lastHeartbeatTime = millis();
  }
  
  delay(10);
}

void connectToWiFi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);

  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  printConnectionDetails();

  digitalWrite(PIN_LED, HIGH);
}

void printConnectionDetails() {
  Serial.print("Protocol: ");
  Serial.println(getProtocolString());
  Serial.print("Signal: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void sendUdpPacket(String message) {
  udp.beginPacket(IPAddress(255, 255, 255, 255), UDP_PORT);
  udp.print(message);
  udp.endPacket();
}

// 詳細情報を含めて送信
void sendHeartbeat() {
  String role = SENSOR_ROLE;
  if (role == "STOP") role = "GOAL";
  
  long rssi = WiFi.RSSI();           // 電波強度
  String proto = getProtocolString(); // プロトコル(11ax等)
  
  String json = "{\"status\":\"alive\",\"sensor\":\"" + role + 
                "\",\"rssi\":" + String(rssi) + 
                ",\"proto\":\"" + proto + "\"}";
  
  udp.beginPacket(IPAddress(255, 255, 255, 255), UDP_PORT);
  udp.print(json);
  udp.endPacket();
}
