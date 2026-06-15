# MGTS (Moto Gymkhana Timing System) 🏍️⏱️

[![Hardware: XIAO ESP32-C6](https://img.shields.io/badge/Hardware-XIAO_ESP32--C6-green.svg)](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html)
[![Network: ESP-NOW & W5500](https://img.shields.io/badge/Network-ESP--NOW_%7C_W5500-blue.svg)]()
[![Frontend: Flet](https://img.shields.io/badge/Frontend-Python%2FFlet-blueviolet.svg)]()

モトジムカーナの競技向けに設計された、高可用性・超低遅延のハイブリッド計時システム（Hardware & Software）です。

## 概要 (Overview)
屋外の過酷な通信環境やノイズ（雨滴、落ち葉、遮断状態の継続など）に耐えうるインフラグレードの設計思想を取り入れています。センサーからのトリガー信号を、ルーター不要の超低遅延無線プロトコル（ESP-NOW）と、物理LAN（W5500モジュールによるUDPブロードキャスト）の2経路で同時に送信する **Active-Active型の超冗長ネットワークアーキテクチャ** を採用しています。

## 主な特徴 (Features)
* **究極のネットワーク冗長化:** L2層（ESP-NOW）とL3層（有線LAN）の同時パケット送出により、パケットロスを物理的に回避。
* **高精度なエッジトリガー制御:** 20msのソフトウェアデバウンスと送信ロック機構により、センサー前での滞留による通信ストームを完全に防止。
* **拡張性の高いPCアプリ連携:** 計時処理とマトリクスLED表示を行うメインコア基板から、JSON形式のUDPパケットでPC（Flutter/Fletアプリ）へ結果をリアルタイム送信。
* **Hardware as Code:** 回路図や基板レイアウトはテキスト（JSONベース）でGit管理され、完全なトレーサビリティを確保。

## ハードウェア・インターフェース仕様 (Hardware Interfaces)

### メインLED基板 (Main Display Core)
筐体にマウントされた物理アーケードボタン（GPIO 22）を用いて、運用時のリカバリー操作やモード切替を行います。
* **短押し (50ms ～ 2秒未満): 強制DNF（リタイア）処理**
  * 転倒やコースアウト時など、現在計測中のランナーの記録を即座に「999.999 (DNF)」として確定・送信し、次の出走者の計測待機状態へ強制移行します。
* **長押し (2秒以上): シングルセンサーモード切替**
  * 画面に「SINGLE ON / OFF」と表示され、モードがトグルで切り替わります。
  * **SINGLE ON:** 8の字練習などで、1つのセンサーを「スタート」と「ゴール」の両方に兼用（ラップ計測）するモードです。

### センサーノード基板 (Sensor Node)
* **センサー入力 (GPIO 21):**
  * OMRON製光電センサー（E3Z-R66など）からのNPN信号を監視します。
  * 遮断された瞬間のエッジを検知し、チャタリングフィルター（20ms）を通過後、システム全体へトリガーパケットを送出します。

## システムアーキテクチャ (Architecture)

1. **Sensor Node (スタート / ゴール)**
   * XIAO ESP32-C6 + W5500モジュールで構成。
   * トリガー検知と同時に `START` または `STOP` コマンドを無線/有線でマルチキャスト。
2. **Main Display Core (LEDパネル基板)**
   * センサーからのコマンドを非同期で待ち受ける計時サーバー。
   * 64x8 マトリクスLEDによるリアルタイムな状態表示とタイム描画。
3. **Frontend App (PC側)**
   * Python/Fletで構築されたフロントエンドUI。
   * UDP経由での確定リザルト受信、および任意のエントリー番号予約や強制DNFコマンドの送信。

## ディレクトリ構成 (Directory Structure)

モノレポ（Monorepo）構成を採用し、ハードウェア構成とソフトウェアのバージョン整合性を担保しています。

```text
.
├── README.md                 # 本ドキュメント
├── hardware/                 # ハードウェア設計ファイル・BOM
│   ├── BOM.md                # 統合部品表（メイン表示部・センサーノード部）
│   ├── main_board/           # メインLED基板の回路図およびPCBレイアウト (EasyEDA)
│   └── sensor_node/          # センサー基板の回路図およびPCBレイアウト (EasyEDA)
├── esp32-c6_sketch/          # XIAO ESP32-C6用 ファームウェアソース (Arduino C++)
│   ├── main_board/
│   └── sensor_node/
└── app/                      # PC側表示・管理用アプリケーション (Python/Flet) ※今後実装予定
