# MGTS (Moto Gymkhana Timing System) 総合部品表 (BOM)

本ドキュメントは、計時システム（MGTS）を構成する「メイン表示部（LEDパネル）」および「センサーノード部（スタート・ゴール）」の調達部品一式を管理する構成管理データです。

---

## 1. メイン表示部 (LEDパネル側) × 1台分

| 大項目 | 品目 | 数量 | 金額 | 購入先リンク / 備考 |
| :--- | :--- | :---: | :---: | :--- |
| 内部基板 | 基板 | 1 | 160円 | [JLCPCB][1] |
| 内部基板 | ESP32-C6 | 1 | 1,650円 | [Amazon：マイコン本体][2] (3個パックを流用) |
| 内部基板 | ヒートシンク | 4 | 460円 | [Amazon：ヒートシンク][3] |
| 内部基板 | ダウンコンバータ | 2 | 260円 | [Amazon：ダウンコンバータ][4] |
| 内部基板 | W5500 | 1 | 833円 | [Amazon：W5500モジュール][5] |
| 内部基板 | 抵抗(10K) | 1 | 10円 | [Amazon：抵抗アソートパック][6] |
| 内部基板 | 抵抗(330) | 1 | 10円 | [Amazon：抵抗アソートパック][6] |
| 内部基板 | XHソケット(2pin) | 4 | 200円 | [Amazon：XHコネクタキット][7] |
| 内部基板 | XHソケット(3pin） | 1 | 50円 | [Amazon：XHコネクタキット][7] |
| 内部基板 | コンデンサ(1000uf) | 3 | 213円 | [Amazon：コンデンサ1000uf][8] |
| インターフェイス | LANポート | 1 | 350円 | [Amazon：パネルマウントLAN][11] |
| インターフェイス | wifiアンテナ | 1 | 450円 | [Amazon：アンテナ本体][12] |
| インターフェイス | wifiアンテナケーブル | 1 | 172円 | [Amazon：アンテナ用ケーブル][13] |
| インターフェイス | DCジャック | 1 | 44円 | [Amazon：パネルマウントDCジャック][14] |
| インターフェイス | 12vPDケーブル | 1 | 600円 | [Amazon：Type-C PDトリガーケーブル][15] |
| インターフェイス | 内部配線と接続ジャック | - | - | [Amazon：内部配線][16] / [Amazon：ジャック][17] |
| インターフェイス | USBポート | 1 | 700円 | [Amazon：パネルマウントUSB][25] |
| インターフェイス | アーケードボタン | 1 | 362円 | [Amazon：三和 OBSF-30][26] (物理リセット/モード切替) |
| ケース関連 | ケース | 1 | - | 既存資産流用または別途調達 |
| ケース関連 | スタンド台座 | 2 | 1,060円 | [Amazon：スタンド台座][20] |
| ケース関連 | ファン(共通) | 1 | 225円 | [Amazon：5Vファン][21] |
| ケース関連 | ファン(シロッコ) | 1 | 1,200円 | [Amazon：シロッコファン][27] (排熱用強力ブロワー) |
| ケース関連 | ファンフィルター | 1 | 146円 | [Amazon：ファンフィルター][22] |
| ケース関連 | HX分岐ファン用 | 1 | 100円 | [Amazon：ファン用分岐ケーブル][28] |
| ケース関連 | LEDパネル | 2 | 6,400円 | [Amazon：64x8 LEDマトリクス][29] |
| ケース関連 | LEDパネル保護板 | 1 | 1,500円 | [はざいや：アクリル板(グレースモーク)][30] (直射日光対策) |
| ケース関連 | ケース固定ステー | 2 | 1,140円 | [Amazon：固定ステー][31] |
| ケース関連 | パネル用両面テープ | 1 | 1,500円 | [Amazon：超強力両面テープ][32] |
| その他 | モバイルバッテリー | 1 | 8,000円 | [Amazon：アンカー大容量バッテリー][23] (87w/20000mAh) |
| その他 | LANケーブル | 1 | - | 既存資産流用 |
| その他 | スタンド | 2 | - | 既存資産流用 |
| その他 | ケース保護角 | 2 | 176円 | [Amazon：コーナーガード][33] |

---

## 2. センサーノード部 (スタート / ゴール共通) × 2台分合計

| 大項目 | 品目 | 数量 | 金額 | 購入先リンク / 備考 |
| :--- | :--- | :---: | :---: | :--- |
| 内部基板 | 基板 | 2 | 320円 | [JLCPCB][1] |
| 内部基板 | ESP32-C6 | 2 | 3,300円 | [Amazon：マイコン本体][2] (3個パック) |
| 内部基板 | ヒートシンク | 6 | 690円 | [Amazon：ヒートシンク][3] |
| 内部基板 | ダウンコンバータ | 2 | 260円 | [Amazon：ダウンコンバータ][4] |
| 内部基板 | W5500 | 2 | 1,666円 | [Amazon：W5500モジュール][5] |
| 内部基板 | 抵抗(10K) | 2 | 20円 | [Amazon：抵抗アソートパック][6] |
| 内部基板 | 抵抗(1K) | 2 | 20円 | [Amazon：抵抗アソートパック][6] |
| 内部基板 | XHソケット(2pin) | 4 | 200円 | [Amazon：XHコネクタキット][7] |
| 内部基板 | XHソケット(3pin） | 2 | 100円 | [Amazon：XHコネクタキット][7] |
| 内部基板 | コンデンサ(1000uf) | 4 | 284円 | [Amazon：コンデンサ1000uf][8] |
| 内部基板 | コンデンサ(0.1uf) | 2 | 200円 | [Amazon：コンデンサ0.1uf][9] |
| 内部基板 | フォトカプラ | 2 | 180円 | [Amazon：フォトカプラ][10] (センサー信号の絶縁用) |
| インターフェイス | LANポート | 2 | 700円 | [Amazon：パネルマウントLAN][11] |
| インターフェイス | wifiアンテナ | 2 | 900円 | [Amazon：アンテナ本体][12] |
| インターフェイス | wifiアンテナケーブル | 2 | 344円 | [Amazon：アンテナ用ケーブル][13] |
| インターフェイス | DCジャック | 2 | 88円 | [Amazon：パネルマウントDCジャック][14] |
| インターフェイス | 12vPDケーブル | 2 | 1,200円 | [Amazon：Type-C PDトリガーケーブル][15] |
| インターフェイス | 内部配線と接続ジャック | - | - | [Amazon：内部配線][16] / [Amazon：ジャック][17] |
| インターフェイス | センサー | 2 | - | OMRON [E3Z-R66 公式製品ページ][18] |
| ケース関連 | ケース | 2 | 2,600円 | [Amazon：防水/防塵ケース][19] |
| ケース関連 | スタンド台座 | 2 | 1,060円 | [Amazon：スタンド台座][20] |
| ケース関連 | ファン | 2 | 450円 | [Amazon：5Vファン][21] |
| ケース関連 | ファンフィルター | 2 | 292円 | [Amazon：ファンフィルター][22] |
| その他 | モバイルバッテリー | 2 | 16,000円 | [Amazon：アンカー大容量バッテリー][23] (87w/20000mAh) |
| その他 | LANケーブル | 2 | - | 既存資産流用 |
| その他 | スタンド(本体と反射板) | 4 | - | 既存資産流用 |
| その他 | 反射板 | 2 | - | [モノタロウ：アクリル反射板][24] |

---

### 🔗 共通リファレンスURL定義（一元管理）

[1]: https://jlcpcb.com/jp/?from=ppc&gad_source=1&gad_campaignid=23125350239&gclid=Cj0KCQjwornRBhCrARIsAON5exHFPxhK7nKD-1U-pn3cwD_hnPvMomwhid0xgz0xE-DB5-678Nt9KeYaAsnBEALw_wcB
[2]: https://www.amazon.co.jp/XIAO-ESP32C6-3%E5%80%8B%E3%83%91%E3%83%83%E3%82%AF-2-4GHz-5-0%E3%80%81Zigbee%E3%80%81Mater%E3%80%81Thread%E3%80%81%E3%82%AA%E3%83%B3%E3%83%9C%E3%83%BC%E3%83%89%E3%82%A2%E3%83%B3%E3%83%86%E3%83%8A%E3%80%81%E5%A4%96%E9%83%A8%E3%82%A2%E3%83%B3%E3%83%86%E3%83%8A%E3%82%A4%E3%83%B3%E3%82%BF%E3%83%BC%E3%83%95%E3%82%A7%E3%83%BC%E3%82%B9/dp/B0DJ6N55FX
[3]: https://www.amazon.co.jp/dp/B0F8V4H91N
[4]: https://www.amazon.co.jp/dp/B0CGVMRQXB
[5]: https://www.amazon.co.jp/dp/B0FZ42HVNH
[6]: https://www.amazon.co.jp/%E7%82%AD%E7%B4%A0%E7%9A%AE%E8%86%9C%E6%8A%B5%E6%8A%97%E5%99%A8-30%E7%A8%AE%E5%90%8410%E6%9C%AC-10%CE%A9%E3%80%9C1M%CE%A9-%E9%9B%BB%E5%AD%90%E5%B7%A5%E4%BD%9C%E3%81%AE%E5%9F%BA%E6%9C%AC%E9%83%A8%E5%93%81-%E9%9B%BB%E5%AD%90%E9%83%A8%E5%93%81%E3%82%BB%E3%83%83%E3%83%88/dp/B0FPX7NZDJ
[7]: https://www.amazon.co.jp/XH%E3%82%AD%E3%83%83%E3%83%88-XH%E5%9E%8B%E7%AB%AF%E5%AD%90-%E3%82%B3%E3%83%B3%E3%82%BF%E3%82%AF%E3%83%88%E3%83%94%E3%83%B3-2-54mm%E3%83%94%E3%83%83%E3%83%81%E7%AB%AF%E5%AD%90%E3%83%8F%E3%82%A6%E3%82%B8%E3%83%B3%E3%82%B0-%E3%83%97%E3%83%A9%E3%82%B0%E3%82%B3%E3%83%8D%E3%82%AF%E3%82%BF%E3%83%AF%E3%82%A4%E3%83%A4%E3%82%B3%E3%83%8D%E3%82%AF%E3%82%BF%E3%82%A2%E3%83%80%E3%83%97%E3%82%BF/dp/B09FHM1SFW
[8]: https://www.amazon.co.jp/dp/B0FP5FLN5M
[9]: https://www.amazon.co.jp/%E6%9D%91%E7%94%B0%E8%A3%BD%E4%BD%9C%E6%89%80-MURATA-%E7%A9%8D%E5%B1%A4%E3%82%BB%E3%83%A9%E3%83%A3%E3%83%9F%E3%83%83%E3%82%AF%E3%82%B3%E3%83%B3%E3%83%87%E3%83%B3%E3%82%B5-%E3%83%A9%E3%82%B8%E3%82%A2%E3%83%AB%E3%83%AA%E3%83%BC%E3%83%89-RPEF11H104Z2M1A01A/dp/B079YJ78VS
[10]: https://www.amazon.co.jp/dp/B09ZQRX29R
[11]: https://www.amazon.co.jp/gp/product/B0989ZYJH8
[12]: https://www.amazon.co.jp/dp/B09JBMLWTT
[13]: https://www.amazon.co.jp/dp/B0931SL6LG
[14]: https://www.amazon.co.jp/dp/B0FQBRHRLY
[15]: https://www.amazon.co.jp/dp/B0D3QG33J7
[16]: https://www.amazon.co.jp/dp/B0DJPQ6KT3
[17]: https://www.amazon.co.jp/dp/B011IJUKC4
[18]: https://www.fa.omron.co.jp/product/item/E3Z-R66/
[19]: https://www.amazon.co.jp/dp/B0F1ZT5SKT
[20]: https://www.amazon.co.jp/dp/B0F21DBYRP
[21]: https://www.amazon.co.jp/dp/B0G7GJZ6JC
[22]: https://www.amazon.co.jp/dp/B0G2R57LNK
[23]: https://www.amazon.co.jp/dp/B0CXNVT5J4
[24]: https://www.monotaro.com/g/04975913/
[25]: https://www.amazon.co.jp/dp/B0G12TWVNH
[26]: https://www.amazon.co.jp/%E4%B8%89%E5%92%8C%E9%9B%BB%E5%AD%90-Sanwa-OBSF-30-K-%E3%83%8F%E3%83%A1%E8%BE%BC%E3%81%BF%E5%BC%8F%E6%8A%BC%E3%81%97%E3%83%9C%E3%82%BF%E3%83%B330%CF%86-%E9%BB%92/dp/B07141VGTF
[27]: https://www.amazon.co.jp/dp/B09G5RC5FF
[28]: https://www.amazon.co.jp/dp/B0GKTWP3LL
[29]: https://www.amazon.co.jp/dp/B088M21SPB
[30]: https://www.hazaiya.co.jp/
[31]: https://www.amazon.co.jp/dp/B003UCG5Y0
[32]: https://www.amazon.co.jp/dp/B01BXNSL8K
[33]: https://www.amazon.co.jp/dp/B0F27MNCGT
