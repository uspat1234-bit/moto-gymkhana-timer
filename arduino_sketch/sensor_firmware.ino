/*
  Moto Gymkhana Timing System - Sensor Firmware
  Board: Arduino Nano R4 Minima (or Uno R3)
  
  機能:
  - D2ピン (Start), D3ピン (Stop) を監視
  - センサーが遮断(LOW)された瞬間を検知
  - USBシリアル通信で "START" / "STOP" を送信
  - チャタリング(信号のバタつき)防止機能付き
*/

// センサーをつないだピン番号
const int PIN_START = 2; // D2ピン
const int PIN_STOP  = 3; // D3ピン

// 前回の状態を覚えておく変数 (初期状態はHIGH=反応なし)
int lastStart = HIGH;
int lastStop  = HIGH;

void setup() {
  // PCとの通信速度 (Python側の BAUD_RATE = 9600 と合わせる)
  Serial.begin(9600);

  // ピンを入力モードに設定
  // 回路図で10kΩのプルアップ抵抗をつけていますが、
  // 念のためマイコン内部のプルアップも有効にして二重の安全策をとります
  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_STOP,  INPUT_PULLUP);
}

void loop() {
  // 現在の状態を読み取る (0:LOW/反応あり, 1:HIGH/なし)
  int currentStart = digitalRead(PIN_START);
  int currentStop  = digitalRead(PIN_STOP);

  // --- スタートセンサーの判定 ---
  // 「前はHIGH(なし) だったのに、急に LOW(あり) になった瞬間」だけ通知
  if (lastStart == HIGH && currentStart == LOW) {
    Serial.println("START"); // PCへ送信 (改行付き)
    delay(50); // チャタリング防止 (50ms待機して誤反応を防ぐ)
  }

  // --- ゴールセンサーの判定 ---
  if (lastStop == HIGH && currentStop == LOW) {
    Serial.println("STOP"); // PCへ送信
    delay(50);
  }

  // 現在の状態を「過去」として保存し、次のループへ
  lastStart = currentStart;
  lastStop  = currentStop;
}
```

---

### 🛠️ 2. 書き込み手順（開発者用）

1.  **Arduino IDE** を起動します。
2.  上記のコードを貼り付けます。
3.  メニューの **[ファイル]** > **[名前を付けて保存]** をクリックし、`sensor_firmware` と入力して保存します。
4.  Arduino Nano R4をPCに接続します。
5.  ツールバーのドロップダウンからボード（Arduino Nano R4 Minima）とCOMポートを選択します。
6.  **「➡️（書き込み）」ボタン** をクリックします。

---

### 📦 3. 配布用バイナリ(.hex)の作り方

非エンジニアの方に配布するための「書き込みツール」に同梱するファイル（`.hex`）は、以下の手順で作成します。

1.  Arduino IDEで上記のスケッチを開いた状態で、メニューの **[スケッチ]** > **[コンパイルしたバイナリを出力]** をクリックします。
2.  スケッチが保存されているフォルダ（`.../Documents/Arduino/sensor_firmware/` など）を開きます。
3.  `build` フォルダの中に **`sensor_firmware.ino.hex`** というファイルが生成されています。
4.  これをコピーして、配布用パッケージ（`Writer.bat` などと一緒にするフォルダ）に入れます。
