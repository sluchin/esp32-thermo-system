# TODO

## 未対応

- [x] **AWS IoT Core への送信** (XIAO ESP32C3。外付けアンテナあり): WiFi、SNTP、TLS (相互認証)、MQTT で、SwitchBot の値を、10 秒ごとに送る。手順は [AWS_SETUP.md](AWS_SETUP.md)。TLS の設定 (PEM、ECDHE-RSA、AES-GCM、PSA、バッファ 6144、時間制限 30 秒) は、[AWS_SETUP.md](AWS_SETUP.md) の 5.1。
  - [ ] 残り: 長時間 (数時間) 動かして、再接続とメモリ (`CONFIG_MBEDTLS_HEAP_SIZE`) を確認する。(外付けアンテナなしでは、WiFi が不安定なので、アンテナは必須)
- [ ] **スマートフォンのテザリングで、簡単につなぐ** (Android アプリから SSID を書き込む): ノードとゲートウェイを、スマートフォンのテザリングで、ネットにつなぐとき、シェルの `thermo set ssid ...` を、手で打たずに、Android アプリから、SSID (とパスワード) を、書き込めるようにする。
  - **先に確認すること**: Android の「USB テザリング」は、相手が USB のホストで、RNDIS/NCM に対応しているときだけ使える。XIAO ESP32C3 の USB は、デバイス側 (USB シリアル/JTAG) なので、**USB テザリングでは、つなげない** (ネットを共有してもらう通信は、基板が USB ホストになる必要がある)。使うのは、スマートフォンの **Wi-Fi テザリング (ホットスポット)**。USB ケーブルは、SSID を書き込むときだけに使う。ホットスポットは、**2.4 GHz** に設定する (ESP32-C3 は 5 GHz に対応しない。Android の既定が 5 GHz のことがある)。暗号化は、WPA2-PSK を使う (WPA3 だけの設定は、つながらないことがある)。
  - **書き込みの方法**: スマートフォンと基板を、**USB ケーブル (Type-C と Type-C)** でつないで、スマートフォンが USB のホスト (OTG) になり、基板の USB シリアル (CDC ACM) に、`thermo set ssid` / `thermo set psk` を送る (ノードもゲートウェイも、同じシェルなので、共通で使える。BLE のペアリングが要らない)。Android アプリは、USB シリアルのライブラリ (例: usb-serial-for-android) を使う。ケーブルは、データ通信に対応したものを使う (充電専用は、不可)。
  - 候補 (B): BLE の書き込み用の特性 (暗号化、ボンディング) を、ノードに足す (ノードだけ。ゲートウェイは、セントラルなので、別の対応が要る)。A (USB シリアル) を先に進める。
  - **決めたこと**: Android アプリは、自作する。**決めること**: アプリの置き場所 (このリポジトリの `android/`、`thermon`、新しいリポジトリ)。アプリの機能 (SSID とパスワードの入力、設定の確認 `thermo show`、証明書の登録 `cred add` も扱うか)。パスワードの扱い (端末に保存しない。ログに出さない)。ホットスポットの SSID を、アプリが読めるか (Android の制限で、読めないことがある。手で入力する)。
  - 関連: 下の「ノードの BLE が使えないときの、WiFi での直接の MQTT」。
- [x] **ノードの BLE が使えないときの、WiFi での直接の MQTT** (実装した。**実機では、まだ確認していない**): ゲートウェイにつながらない測定が 5 回続いたら、WiFi と MQTT で、10 秒ごとに、直接送る。ゲートウェイにつながったら止める。接続が 5 回失敗したらあきらめる。ノードごとの証明書、クライアント ID、ポリシー。共有コードは `app/common/` (`cfg` `cfg_shell` `cloud` `ntp` `payload` `wifi_link`)、ノードの制御は `app/thermo-node/src/direct.c`。手順は [SETUP.md](SETUP.md) と [AWS_SETUP.md](AWS_SETUP.md) の 7 章。
- [x] **ノードの、SD カードへの保存と、あとからの送信 (ストア・アンド・フォワード)** (実装した。**実機では、まだ確認していない**): ゲートウェイにつながっていない間の測定値 (UNIX 時刻、温度、湿度。10 秒ごと) を、まだ MQTT につながっていないとき、または、先に送る記録があるときに、SD カードに溜める (`backlog.c`。FatFS。16 バイトの固定長、CRC つき)。送っていない記録があれば、ゲートウェイにつながっていても、WiFi と MQTT で、古い順に、測った時刻のまま送って、publish できた分を送り終えたことにする。
- [ ] 上の 2 つの、**実機での確認**: BLE と WiFi と TLS と SD カードを同時に動かしたときの動作と RAM (静的な RAM の空きは、約 1.5 KB しかない)、ゲートウェイを止めたときの切り替えと、戻ったときの停止、SD カードの読み書き (配線、FAT のフォーマット、抜き差し、電源断)、あとからの送信 (AWS の DynamoDB に、測った時刻で入ること)。
- [ ] SD カードの記録を、**PUBACK を確認してから**、送り終えたことにする (今は、publish の成功だけ。接続が切れる直前の値は、失われることがある)。
- [ ] BLE だけで、WiFi の設定がないノードの記録を、ゲートウェイ経由で送る (BLE の特性で、記録をゲートウェイに渡して、ゲートウェイが、MQTT で、測った時刻つきで送る)。
- [ ] `native_sim` で、クラウドへの送信 (ローカルの Mosquitto など) を確認する。いまの `native_sim` は、`CONFIG_THERMO_CLOUD=n`。
- [ ] **アドバタイズ方式** (BLE): ノードが、温度と湿度を、アドバタイズデータ (製造者固有データ) に載せて送り、ゲートウェイは、接続せずに、スキャンだけで受信する。(2026-10-04)
  - 目的: 台数の制限をなくす。GATT 接続は、同時に接続できる台数に、上限がある (`ESP32_BT_CTLR_LE_MAX_CONN` は、既定で 3、設定できる範囲は 1〜9。WiFi と TLS を同時に動かすと、実際は 3〜5 台が目安)。
  - まずは GATT 接続 + 通知で実装した。アドバタイズ方式は、あとで実装して、CMake のオプション (例: `-DTHERMO_BLE_MODE=gatt|advertising`) で、GATT 方式と切り替えられるようにする。
  - 注意点: アドバタイズは、届く保証がなく、盗聴やなりすましに弱い。連番 (欠落の検出) や、認証コード (なりすまし対策) を、ペイロードに入れるか検討する。
  - 切り替えるときの設計: ノードの `ble.c` と、ゲートウェイの `ble.c` を、方式ごとのファイルに分けて、同じヘッダ (`ble.h`) の関数 (`ble_init()` など) と、温度を受け取るコールバックを、共通にする。MQTT 側 (ゲートウェイ) は、方式に依存しない。
- [x] **OLED の表示と RTC の時刻** (拡張ボードの SSD1306 と PCF8563): 温度、湿度、日付、時刻の表示は、実装した (`app/thermo-node/src/oled.c` と `node_time.c`。`THERMO_DISPLAY=n` と `THERMO_RTC=n` で、別々に外せる。単体テストは `tests/oled` と `tests/node_time` と `tests/rtc_pcf8563`)。RTC は、Zephyr のドライバの月と年の扱いが、チップと合わないので、このアプリのドライバ (`app/thermo-node/drivers/rtc_pcf8563.c`。Zephyr の RTC API を実装する) を使う。I2C のアドレスは、OLED が `0x3C`、RTC が `0x51`。残りの段階:
  - [x] 段階 2: 拡張ボードの RTC (PCF8563) の時刻を、OLED に表示する (日本標準時)。時刻の設定は、段階 3 と段階 4 (`node_time_set()` が、BLE で届いた時刻と、WiFi で取った時刻を、RTC に設定する)。
  - [x] 段階 3: ゲートウェイが SNTP で取得した時刻を、BLE の GATT で、ノードに書き込み、ノードが RTC に設定する。ノードは、時刻の特性 (`9F3C1A02-...`、書き込み専用) を持ち、ゲートウェイは、接続したノードに、1 時間ごとに書き込む (`ble.c`)。単体テストで確認している。
  - [x] OLED のグラフ: 温度と湿度の履歴 (30 秒ごとの平均を 128 点) の折れ線を、ユーザボタン (D1) で切り替えるページに表示する (`history.c`、`button.c`)。
  - [x] 段階 4: BLE で時刻が届かないときの予備として、ノードが、WiFi で、直接、SNTP の時刻を取得する (`wifi_time.c`。`CONFIG_THERMO_WIFI_TIME`)。起動の 1 分後から 1 分ごとに、RTC に時刻があるかを確かめ、なければ、WiFi につないで時刻を取り、RTC に設定して、WiFi を切る (最大 5 回)。SSID とパスワードは、シェル (`thermo set`) か、`scripts/send-wifi.sh` で設定する。
  - [ ] 残り: WiFi と BLE の同時動作で、アドバタイズと DHT11 の読み取りが、続くか (長時間)。スタックとヒープの余裕。
- [ ] **ログを mini SD カードに保存する** (拡張ボードの SD カードスロット。SPI): Zephyr の FAT (FatFS) で、CSV (時刻、温度、湿度) を追記する。Zephyr は、ext4 に対応していない (ext2、FAT、LittleFS だけ)。PC で読むには FAT32 が簡単。SPI の SD は、シールドの `seeed_xiao_expansion_board` と同じ設定 (CS は D2)。SD の取り外しと、書き込み中の電源断の対策 (定期的に同期する) も、考える。
- [ ] **Grove のセンサ**: I2C の Grove (D4/D5。OLED と RTC と同じバス) に、CO2 (SCD40 など) や、温湿度と気圧 (BME280 など) のセンサをつなぐ。BLE の特性の形式と、AWS のペイロードを広げる必要がある。
- [ ] **ADC のアナログセンサ** (LM35 など) に替えるときの、実機での確認: 実装は、ある (オーバーレイの `thermo-sensor` を消して、`zephyr,user` の `io-channels` を書く。換算は `CONFIG_THERMO_ADC_MV_PER_DEG` と `CONFIG_THERMO_ADC_OFFSET_MV`。ADC エミュレータの単体テストで確認している)。センサを買ったら、実機で、換算の値を、確認する。
- [ ] 実機の Bluetooth アダプタを使う `native_sim` の実行 (`--bt-dev=hciN`) を、docker compose のサービスにする。(`run-sim` は、仮想コントローラ `btvirt` で動く)
- [ ] 行末コメントの見直し: `tests/` のローカル変数のコメントのうち、変数名から機械的に付けたものは、ファイルごとの意味に合っているか、確認して直す。(2026-10-11: コメントがなかった 29 件には、意味を書いた)

## 対応済み

- [x] **SwitchBot 屋外用温湿度計**: 屋外用温湿度計 (Outdoor Meter) の、温度・湿度・電池残量を受け取る。他の SwitchBot の機種 (Meter、Meter Plus など) は、機種コードごとに並びが違うので、未対応。
- [x] **Thermo ノード**: DHT11 の読取 (配線は [SETUP.md](SETUP.md)。D7)、BLE の配信、ゲートウェイの GATT 接続と通知の受信、ノードの温度の AWS IoT Core への送信 ([AWS_SETUP.md](AWS_SETUP.md) の 1.1)、WiFi と BLE の同時動作で温度が欠けないか。
- [x] **温湿度センサ (DHT11)**: ノードは、Zephyr のセンサ API で、DHT11 を読む (`app/thermo-node/boards/xiao_esp32c3.overlay`。D7 (GPIO20))。BLE の通知は、温度 (int16、0.1 ℃) と湿度 (uint16、0.1 %) の 4 バイトで、AWS には `temperature_c` と `humidity` を送る。単体テストは、偽のセンサで確認している。(実機での確認は、上の項目)
- [x] **起動画面と起動音** (拡張ボードの OLED とパッシブブザー): 実装した (`oled_show_boot()`、`buzzer.c`、素材は `boot_assets.c`。`THERMO_BOOT_SCREEN` と `THERMO_BUZZER` で外せる。単体テストは `tests/buzzer`、`tests/oled`、`tests/main`)。ブザーは D3 (GPIO5) につなぐ。素材のサブモジュール (`app/thermo-node/private/`) は、使い方を `SETUP.md` に書いてある。ボタン (D1) を押したときの音も、同じ仕組みで鳴らす (`button_assets.c`。`THERMO_BUTTON_SOUND` で外せる)。
- [x] ペイロードの UNIX 時刻 (`timestamp` [s]): 受信したときの時刻を、publish するときに、(今の UNIX 時刻) - (受信してからの経過) で求める。`uptime_ms` も、残す。
- [x] サーバ証明書の有効期限の確認: `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` で、mbedTLS がシステム時計 (SNTP で同期) を使う。1 度も同期していないときは、TLS が必ず失敗するので、MQTT に接続せず、間隔をあけて、やり直す。(ビルドと実機での確認は、未対応。実機では、RAM の増加と、期限切れの証明書で接続が拒否されることを確認する)
- [x] SNTP による時刻の同期 (ゲートウェイ): WiFi の接続のたびに、`CONFIG_THERMO_NTP_SERVER` (既定 `pool.ntp.org`) に問い合わせて、システム時計を合わせる (`CONFIG_THERMO_NTP_RESYNC_SEC` の間は、再同期しない。失敗しても、MQTT の接続は続ける)。単体テストは `tests/ntp`。(ビルドと実機での確認は、未対応)
- [x] AWS IoT Core への MQTT 送信 (ゲートウェイ): WiFi の接続、MQTT over TLS (相互認証)、JSON の publish、再接続 (間隔を倍々に増やす)、シェル (`thermo` コマンド) による設定と、フラッシュへの保存。(実機での確認は、未対応の項目を参照)
- [x] BLE の GATT 接続 + 通知: ノードが温度と湿度を通知し、ゲートウェイが接続・購読して受信する (UUID は `app/common/thermo_ble_uuid.h`。`native_sim` + `btvirt` の `run-sim` で、接続から温度の受信まで動かせる)。
- [x] Ztest + FFF の単体テストを、両アプリに追加した (`app/*/tests/`。sensor は偽のセンサと ADC エミュレータを使う)。
- [x] GitHub Actions を、Docker イメージで動く、`build` / `test` / `analyze` / `lint` ジョブに作り直した (レイヤーキャッシュあり)。
- [x] 静的解析 (gcc `-fanalyzer`)、警告オプション約 55 個、MISRA-C の例外規定 (`MISRA.md`) を整備した。
- [x] Zephyr のコーディング規約に合わせた (clang-format、checkpatch.pl、行末の空白の確認。例外は `CODING_STYLE.md`)。
