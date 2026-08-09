#include <Arduino.h>
#include <LittleFS.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutputI2S.h>
#include <vector>
fs::FS &SPIFFS = LittleFS;

// ==========================================
// PCM5102 配線 (実際のGPIO番号は環境に合わせて変更)
// ==========================================
#define I2S_BCLK  5   // PCM5102 BCK
#define I2S_LRC   6   // PCM5102 LCK
#define I2S_DOUT  4   // PCM5102 DIN

// メイン基板からのコマンド受信用UART (TXのみ接続の想定なのでRXだけ使う)
#define CMD_RX_PIN 18   // メイン基板側の送信ピンに接続

AudioGeneratorWAV *wav = nullptr;
AudioFileSourceLittleFS *file = nullptr;
AudioOutputI2S *out = nullptr;

std::vector<String> playQueue;   // 再生待ちファイル名キュー
bool isPlaying = false;

// ==========================================
// キューに追加
// ==========================================
void enqueue(const String &filename) {
  playQueue.push_back(filename);
}

// ==========================================
// キューの先頭を再生開始
// ==========================================
void playNextInQueue() {
  if (playQueue.empty()) {
    isPlaying = false;
    return;
  }

  String filename = playQueue.front();
  playQueue.erase(playQueue.begin());

  String path = "/" + filename;
  if (!LittleFS.exists(path)) {
    Serial.printf("ファイルが見つかりません: %s\n", path.c_str());
    playNextInQueue(); // 見つからなければスキップして次へ
    return;
  }

  if (file) { delete file; file = nullptr; }
  if (wav)  { delete wav;  wav  = nullptr; }

  file = new AudioFileSourceLittleFS(path.c_str());
  wav = new AudioGeneratorWAV();
  wav->begin(file, out);
  isPlaying = true;

  Serial.printf("再生開始: %s\n", path.c_str());
}

// ==========================================
// タイム読み上げ (ミリ秒の整数、LED表示と完全一致させる)
// 例: 6609ms → "time.wav","6s.wav","ten.wav","6.wav","0.wav","9.wav"
// ==========================================
void queueTimeAnnouncement(long totalMs) {
  long minutes = totalMs / 60000;
  long remMs = totalMs % 60000;
  long wholeSec = remMs / 1000;
  long ms3digit = remMs % 1000;   // 0〜999のミリ秒3桁

  int digit1 = (ms3digit / 100) % 10;
  int digit2 = (ms3digit / 10) % 10;
  int digit3 = ms3digit % 10;

  enqueue("time.wav");
  if (minutes > 0) {
    enqueue(String(minutes) + "m.wav");
  }
  enqueue(String(wholeSec) + "s.wav");
 
  enqueue(String(digit1) + ".wav");
  enqueue(String(digit2) + ".wav");
  enqueue(String(digit3) + ".wav");
}

// ==========================================
// トップタイム比の読み上げ (100.1%〜199.9%を想定)
// isFastest=true なら「総合ファステスト」を、falseなら実比率を再生
// ratioX10 は比率を10倍した整数 (例: 150.7% → 1507)
// ==========================================
void queueTopRatio(bool isFastest, int ratioX10) {
  if (isFastest) {
    enqueue("overall_fastest.wav");
    return;
  }

  int wholePart = ratioX10 / 10;      // 整数部 (100〜199)
  int decimalPart = ratioX10 % 10;    // 小数点第1位 (0〜9)

  // 範囲外の安全対策 (100〜199の範囲にクランプ)
  if (wholePart < 100) wholePart = 100;
  if (wholePart > 199) wholePart = 199;

  enqueue("num_" + String(wholePart) + ".wav");
  enqueue("ten.wav");
  enqueue(String(decimalPart) + ".wav");
  enqueue("percent.wav");
}

// ==========================================
// 受信コマンドのパース
// 想定フォーマット:
//   T:6609            → タイム読み上げ (ミリ秒の整数)
//   P:F               → トップタイム比: 総合ファステスト
//   P:1507            → トップタイム比: 150.7% (10倍した整数)
//   W:overall_fastest → 単発ワード再生 (拡張子.wavは自動付与)
// ==========================================
void handleCommand(const String &cmd) {
  String trimmed = cmd;
  trimmed.trim();
  if (trimmed.length() == 0) return;

  Serial.printf("コマンド受信: [%s]\n", trimmed.c_str());

  if (trimmed.startsWith("T:")) {
    long ms = trimmed.substring(2).toInt();
    queueTimeAnnouncement(ms);

  } else if (trimmed.startsWith("P:")) {
    String param = trimmed.substring(2);
    if (param == "F") {
      queueTopRatio(true, 0);
    } else {
      int ratioX10 = param.toInt();
      queueTopRatio(false, ratioX10);
    }

  } else if (trimmed.startsWith("W:")) {
    String word = trimmed.substring(2);
    enqueue(word + ".wav");

  } else {
    Serial.printf("不明なコマンド: %s\n", trimmed.c_str());
  }
}

// ==========================================
// セットアップ
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS マウント失敗！");
  } else {
    Serial.println("LittleFS マウント成功");
  }

  out = new AudioOutputI2S();
  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  out->SetGain(0.8); // 音量調整 (0.0〜1.0)

  // メイン基板からのコマンド受信専用UART (RXのみ使用)
  Serial1.begin(115200, SERIAL_8N1, CMD_RX_PIN, -1);

  Serial.println("音声サブボード 準備完了");
}

// ==========================================
// メインループ
// ==========================================
void loop() {
  // メイン基板からのコマンド受信
  if (Serial1.available()) {
    String cmd = Serial1.readStringUntil('\n');
    handleCommand(cmd);
  }

  // ★デバッグ用: PC(USBシリアル)から直接コマンドを送れるようにする
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleCommand(cmd);
  }

  // 再生処理
  if (isPlaying && wav) {
    if (wav->isRunning()) {
      if (!wav->loop()) {
        wav->stop();
        isPlaying = false;
      }
    } else {
      isPlaying = false;
    }
  }

  // 再生中でなく、キューに何かあれば次を再生
  if (!isPlaying && !playQueue.empty()) {
    playNextInQueue();
  }
}