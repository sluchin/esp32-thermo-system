# TODO

## 未対応

- [x] **SwitchBot 屋外用温湿度計の、実機での確認**: 屋外用温湿度計 (Outdoor Meter) で、温度・湿度・電池残量が、SwitchBot のアプリの値と、合うことを確認した。他の SwitchBot の機種 (Meter、Meter Plus など) は、機種コードごとに並びが違うので、未対応。
- [x] **AWS IoT Core への送信の、実機での確認**: 実機 (XIAO ESP32C3、外付けアンテナあり) で、WiFi、SNTP、TLS (相互認証)、MQTT の接続を確認して、SwitchBot の値が、10 秒ごとに、MQTT テストクライアントに届くことを確認した。手順は [AWS_SETUP.md](AWS_SETUP.md)。TLS の設定 (PEM、ECDHE-RSA、AES-GCM、PSA、バッファ 6144、時間制限 30 秒) は、[AWS_SETUP.md](AWS_SETUP.md) の 5.1。
  - [ ] 残り: 長時間 (数時間) 動かして、再接続とメモリ (`CONFIG_MBEDTLS_HEAP_SIZE`) を確認する。(外付けアンテナなしでは、WiFi が不安定なので、アンテナは必須)
- [ ] **Thermo ノードの実機での確認** (ノードの実機がないので、未着手。いまの実機での確認は、SwitchBot の受信と、AWS IoT Core への送信だけ): DHT11 の読取 (配線は [SETUP.md](SETUP.md)。D7)、BLE の配信、ゲートウェイの GATT 接続と通知の受信、ノードの温度の AWS IoT Core への送信 ([AWS_SETUP.md](AWS_SETUP.md) の 1.1)、WiFi と BLE の同時動作で温度が欠けないか。
- [ ] `native_sim` で、クラウドへの送信 (ローカルの Mosquitto など) を確認する。いまの `native_sim` は、`CONFIG_THERMO_CLOUD=n`。
- [ ] **アドバタイズ方式** (BLE): ノードが、温度と湿度を、アドバタイズデータ (製造者固有データ) に載せて送り、ゲートウェイは、接続せずに、スキャンだけで受信する。(2026-10-04)
  - 目的: 台数の制限をなくす。GATT 接続は、同時に接続できる台数に、上限がある (`ESP32_BT_CTLR_LE_MAX_CONN` は、既定で 3、設定できる範囲は 1〜9。WiFi と TLS を同時に動かすと、実際は 3〜5 台が目安)。
  - まずは GATT 接続 + 通知で実装した。アドバタイズ方式は、あとで実装して、CMake のオプション (例: `-DTHERMO_BLE_MODE=gatt|advertising`) で、GATT 方式と切り替えられるようにする。
  - 注意点: アドバタイズは、届く保証がなく、盗聴やなりすましに弱い。連番 (欠落の検出) や、認証コード (なりすまし対策) を、ペイロードに入れるか検討する。
  - 切り替えるときの設計: ノードの `ble.c` と、ゲートウェイの `ble.c` を、方式ごとのファイルに分けて、同じヘッダ (`ble.h`) の関数 (`ble_init()` など) と、温度を受け取るコールバックを、共通にする。MQTT 側 (ゲートウェイ) は、方式に依存しない。
- [x] **温湿度センサ (DHT11)**: ノードは、Zephyr のセンサ API で、DHT11 を読む (`app/thermo-node/boards/xiao_esp32c3.overlay`。D7 (GPIO20))。BLE の通知は、温度 (int16、0.1 ℃) と湿度 (uint16、0.1 %) の 4 バイトで、AWS には `temperature_c` と `humidity` を送る。単体テストは、偽のセンサで確認している。(実機での確認は、上の項目)
- [ ] **OLED の表示と RTC の時刻** (拡張ボードの SSD1306 と PCF8563): 温度、湿度、日付、時刻の表示は、実装した (`app/thermo-node/src/oled.c` と `node_time.c`。`THERMO_DISPLAY=n` と `THERMO_RTC=n` で、別々に外せる。単体テストは `tests/oled` と `tests/node_time` と `tests/rtc_pcf8563`)。RTC は、Zephyr のドライバの月と年の扱いが、チップと合わないので、このアプリのドライバ (`app/thermo-node/drivers/rtc_pcf8563.c`。Zephyr の RTC API を実装する) を使う。実機で、表示の向きと、コントラストと、I2C のアドレス (OLED が `0x3C`、RTC が `0x51`)、RTC の読み書き (特に、世紀のビットと、電圧低下のビット) を、確認する。残りの段階:
  - [x] 段階 2: 拡張ボードの RTC (PCF8563) の時刻を、OLED に表示する (日本標準時)。時刻の設定は、段階 3 (`node_time_set()` は、実装済みで、単体テストがある。アプリからは、まだ呼んでいない)。
  - [x] 段階 3: ゲートウェイが SNTP で取得した時刻を、BLE の GATT で、ノードに書き込み、ノードが RTC に設定する (ノードに WiFi は載せない)。ノードは、時刻の特性 (`9F3C1A02-...`、書き込み専用) を持ち、ゲートウェイは、接続したノードに、1 時間ごとに書き込む (`ble.c`)。単体テストで確認している。実機では、未確認 (ゲートウェイが SNTP で同期してから、ノードの時刻が合うまでの流れ、1 時間後の書き直し、ノードの再接続のあとの再設定)。
  - [x] OLED のグラフ: 温度と湿度の履歴 (30 秒ごとの平均を 128 点) の折れ線を、ユーザボタン (D1) で切り替えるページに表示する (`history.c`、`button.c`)。実機で、ボタンの反応 (押した感触と、チャタリング)、グラフの見やすさ、スタック (`CONFIG_MAIN_STACK_SIZE=4096`) を確認する。
  - [ ] 将来: ノードが、WiFi で、直接、NTP の時刻を取得する案 (ゲートウェイと同じ仕組みを、ノードにも載せる。WiFi と BLE の同時動作の確認が要る)。
- [ ] **ログを mini SD カードに保存する** (拡張ボードの SD カードスロット。SPI): Zephyr の FAT (FatFS) で、CSV (時刻、温度、湿度) を追記する。Zephyr は、ext4 に対応していない (ext2、FAT、LittleFS だけ)。PC で読むには FAT32 が簡単。SPI の SD は、シールドの `seeed_xiao_expansion_board` と同じ設定 (CS は D2)。SD の取り外しと、書き込み中の電源断の対策 (定期的に同期する) も、考える。
- [ ] **Grove のセンサ**: I2C の Grove (D4/D5。OLED と RTC と同じバス) に、CO2 (SCD40 など) や、温湿度と気圧 (BME280 など) のセンサをつなぐ。BLE の特性の形式と、AWS のペイロードを広げる必要がある。
- [ ] **起動画面と起動音** (拡張ボードの OLED とパッシブブザー): 実装した (`oled_show_boot()`、`buzzer.c`、素材は `boot_assets.c`。`THERMO_BOOT_SCREEN` と `THERMO_BUZZER` で外せる。単体テストは `tests/buzzer`、`tests/oled`、`tests/main`)。実機で、アニメーションの速さ、音の大きさと音程、ブザーが D3 (GPIO5) につながっているか、PWM (LEDC) が動くかを、確認する。素材のサブモジュール (`app/thermo-node/private/`) は、使い方だけを書いてある (`SETUP.md`)。
- [ ] **ADC のアナログセンサ** (LM35 など) に替えるときの、実機での確認: 実装は、ある (オーバーレイの `thermo-sensor` を消して、`zephyr,user` の `io-channels` を書く。換算は `CONFIG_THERMO_ADC_MV_PER_DEG` と `CONFIG_THERMO_ADC_OFFSET_MV`。ADC エミュレータの単体テストで確認している)。センサを買ったら、実機で、換算の値を、確認する。
- [ ] 実機の Bluetooth アダプタを使う `native_sim` の実行 (`--bt-dev=hciN`) を、docker compose のサービスにする。(`run-sim` は、仮想コントローラ `btvirt` で動く)
- [ ] ビルドとテストの確認: 標準ヘッダとローカル変数の行末コメントを足した変更を、Docker と Zephyr の環境で、ビルドして、単体テストを実行する。(コメントだけの変更だが、未確認)。BLE の送受信の `LOG_HEXDUMP_DBG` の追加も、ビルドして、単体テストを実行して、確認する。
- [ ] 行末コメントの見直し: `tests/` のローカル変数のコメントは、変数名から機械的に付けた。ファイルごとの意味に合っているか、確認して直す。
- [ ] 整形の確認: 手作業で揃えたコメントの桁を、Docker の `format-thermo` で確認する。(この環境の clang-format 18 は、 `.clang-format` で、無関係な行まで書き換える)

## 対応済み

- [x] ペイロードの UNIX 時刻 (`timestamp` [s]): 受信したときの時刻を、publish するときに、(今の UNIX 時刻) - (受信してからの経過) で求める。`uptime_ms` も、残す。
- [x] サーバ証明書の有効期限の確認: `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` で、mbedTLS がシステム時計 (SNTP で同期) を使う。1 度も同期していないときは、TLS が必ず失敗するので、MQTT に接続せず、間隔をあけて、やり直す。(ビルドと実機での確認は、未対応。実機では、RAM の増加と、期限切れの証明書で接続が拒否されることを確認する)
- [x] SNTP による時刻の同期 (ゲートウェイ): WiFi の接続のたびに、`CONFIG_THERMO_NTP_SERVER` (既定 `pool.ntp.org`) に問い合わせて、システム時計を合わせる (`CONFIG_THERMO_NTP_RESYNC_S` の間は、再同期しない。失敗しても、MQTT の接続は続ける)。単体テストは `tests/ntp`。(ビルドと実機での確認は、未対応)
- [x] AWS IoT Core への MQTT 送信 (ゲートウェイ): WiFi の接続、MQTT over TLS (相互認証)、JSON の publish、再接続 (間隔を倍々に増やす)、シェル (`thermo` コマンド) による設定と、フラッシュへの保存。(実機での確認は、未対応の項目を参照)
- [x] BLE の GATT 接続 + 通知: ノードが温度と湿度を通知し、ゲートウェイが接続・購読して受信する (UUID は `app/common/thermo_ble_uuid.h`。`native_sim` + `btvirt` の `run-sim` で、接続から温度の受信まで確認済み)。
- [x] Ztest + FFF の単体テストを、両アプリに追加した (`app/*/tests/`。sensor は偽のセンサと ADC エミュレータを使う)。
- [x] GitHub Actions を、Docker イメージで動く、`build` / `test` / `analyze` / `lint` ジョブに作り直した (レイヤーキャッシュあり)。
- [x] 静的解析 (gcc `-fanalyzer`)、警告オプション約 55 個、MISRA-C の例外規定 (`MISRA.md`) を整備した。
- [x] Zephyr のコーディング規約に合わせた (clang-format、checkpatch.pl、行末の空白の確認。例外は `CODING_STYLE.md`)。
