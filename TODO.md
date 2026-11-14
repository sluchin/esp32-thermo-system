# TODO

## 未対応

- [x] **AWS IoT Core への送信** (XIAO ESP32C3。外付けアンテナあり): WiFi、SNTP、TLS (相互認証)、MQTT で、SwitchBot の値を 10 秒ごとに送る。手順は [aws_setup.md](docs/aws_setup.md) にある。TLS の設定 (PEM、ECDHE-RSA、AES-GCM、PSA、バッファ 6144、時間制限 30 秒) は、[aws_setup.md](docs/aws_setup.md) の 5.1 にある。
  - [ ] 残り: 長時間 (数時間) 動かして、再接続とメモリ (`CONFIG_MBEDTLS_HEAP_SIZE`) を確かめる。外付けアンテナがないと WiFi が不安定なので、アンテナは必須。
- [ ] **スマートフォンのテザリングで、簡単につなぐ** (Android アプリから SSID を書き込む): ノードとゲートウェイをスマートフォンのテザリングでネットにつなぐとき、シェルの `thermo set ssid ...` を手で打たずに、Android アプリから SSID とパスワードを書き込めるようにする。ファームウェアの `thermo tether` と `thermo restore`、Android アプリ [thermo-term](https://github.com/sluchin/thermo-term) は実装した。**実機では、まだ確かめていない。**
  - **先に確かめること**: Android の「USB テザリング」を使えるのは、相手が USB のホストで、RNDIS か NCM に対応しているときだけ。XIAO ESP32C3 の USB はデバイス側 (USB シリアル/JTAG) なので、**USB テザリングではつなげない** (ネットを共有してもらうには、基板が USB ホストになる必要がある)。使うのは、スマートフォンの **Wi-Fi テザリング (ホットスポット)** で、USB ケーブルは SSID を書き込むときだけに使う。ESP32-C3 は 5 GHz に対応しないので、ホットスポットは **2.4 GHz** にする (Android の既定が 5 GHz のことがある)。WPA3 だけの設定ではつながらないことがあるので、暗号化には WPA2-PSK を使う。
  - **書き込みの方法**: スマートフォンと基板を **USB ケーブル (Type-C と Type-C)** でつなぐ。スマートフォンが USB のホスト (OTG) になって、基板の USB シリアル (CDC ACM) に `thermo set ssid` と `thermo set psk` を送る。ノードとゲートウェイのシェルは同じなので、どちらにも使える。BLE のペアリングは要らない。Android アプリは、USB シリアルのライブラリ (例: usb-serial-for-android) を使う。充電専用のケーブルは使えないので、データ通信に対応したケーブルを使う。
  - 候補 (B): 暗号化とボンディングを使う、書き込み用の BLE の特性を、ノードに足す。ゲートウェイはセントラルなので、別の対応が要る。先に A (USB シリアル) を進める。
  - **決めたこと**: Android アプリは自作する。
  - **決めること**: アプリの置き場所 (このリポジトリの `android/`、`thermon`、新しいリポジトリ)。アプリの機能 (SSID とパスワードの入力のほかに、設定の確認 `thermo show` と証明書の登録 `cred add` も扱うか)。パスワードの扱い (端末に保存しない。ログに出さない)。ホットスポットの SSID をアプリが読めるか (Android の制限で読めないことがあるので、そのときは手で入力する)。
  - 関連: 下の「ノードの BLE が使えないときの、WiFi での直接の MQTT」。
- [x] **ノードの BLE が使えないときの、WiFi での直接の MQTT** (実装した。**実機では、まだ確かめていない**): ゲートウェイにつながらない測定が 5 回続いたら、WiFi と MQTT で直接送る。ゲートウェイにつながったら止める。接続に 5 回失敗したらあきらめる。証明書、クライアント ID、ポリシーは、ノードごとに用意する。共有のコードは `app/common/` (`cfg` `cfg_shell` `cloud` `ntp` `payload` `wifi_link`)、ノードの制御は `app/thermo-node/src/direct.c` にある。手順は、[setup.md](docs/setup.md) と [aws_setup.md](docs/aws_setup.md) の 7 章にある。
- [x] **ノードの、SD カードへの保存と、あとからの送信 (ストア・アンド・フォワード)** (実装した。**実機では、まだ確かめていない**): 全ての測定値 (5 秒ごとの UNIX 時刻、温度、湿度、気圧) を、送信済みフラグつきで SD カードに保存する (`backlog.c`)。FatFS を使い、1 件は CRC つきの 16 バイトの固定長。件数には上限があり、超えたら古い記録から上書きする。ゲートウェイにつながっている間の測定値は送信済みとして、つながっていない間の測定値は未送信として保存する。未送信の記録があれば、ゲートウェイにつながっていても、WiFi と MQTT で、測った時刻のまま古い順に送る。publish できた分は、送信済みにする。
- [ ] 上の 2 つの、**実機での確認**: BLE、WiFi、TLS、SD カードを同時に動かしたときの動作と RAM (静的な RAM の空きは、約 1.5 KB しかない)。ゲートウェイを止めたときの切り替えと、戻ったときの停止。SD カードの読み書き (配線、FAT のフォーマット、抜き差し、電源断)。あとからの送信 (測った時刻で、AWS の DynamoDB に入ること)。
- [ ] SD カードの記録を、**PUBACK を確かめてから**送り終えたことにする。今は publish の成功だけを見ているので、接続が切れる直前の値は、失われることがある。
- [ ] WiFi の設定がない、BLE だけのノードの記録を、ゲートウェイ経由で送る。BLE の特性で記録をゲートウェイに渡し、ゲートウェイが、測った時刻つきで MQTT で送る。
- [ ] `native_sim` で、クラウドへの送信 (ローカルの Mosquitto など) を確かめる。今の `native_sim` は `CONFIG_THERMO_CLOUD=n`。
- [ ] **アドバタイズ方式** (BLE): ノードは、測定値をアドバタイズデータ (製造者固有データ) に載せて送る。ゲートウェイは、接続せずに、スキャンだけで受信する。(2026-10-04)
  - 目的: 台数の制限をなくす。GATT 接続では、同時に接続できる台数に上限がある。`ESP32_BT_CTLR_LE_MAX_CONN` の既定は 3 で、設定できる範囲は 1〜9。WiFi と TLS を同時に動かすと、実際の目安は 3〜5 台。
  - まず、GATT 接続と通知で実装した。アドバタイズ方式はあとで実装して、CMake のオプション (例: `-DTHERMO_BLE_MODE=gatt|advertising`) で GATT 方式と切り替えられるようにする。
  - 注意点: アドバタイズは、届く保証がなく、盗聴やなりすましに弱い。欠落を見つけるための連番と、なりすましを防ぐための認証コードを、ペイロードに入れるかを検討する。
  - 切り替えるときの設計: ノードとゲートウェイの `ble.c` を、方式ごとのファイルに分ける。同じヘッダ (`ble.h`) の関数 (`ble_init()` など) と、温度を受け取るコールバックは、共通にする。ゲートウェイの MQTT 側は、方式に依存しない。
- [x] **OLED の表示と RTC の時刻** (拡張ボードの SSD1306 と PCF8563): 温度、湿度、日付、時刻の表示は実装した (`app/thermo-node/src/oled.c` と `node_time.c`)。`THERMO_DISPLAY=n` と `THERMO_RTC=n` で、別々に外せる。単体テストは `tests/oled`、`tests/node_time`、`tests/rtc_pcf8563` にある。Zephyr のドライバは月と年の扱いがチップと合わないので、RTC には、このアプリのドライバ (`app/thermo-node/drivers/rtc_pcf8563.c`。Zephyr の RTC API を実装する) を使う。I2C のアドレスは、OLED が `0x3C`、RTC が `0x51`。各段階の状況は、次のとおり。
  - [x] 段階 2: 拡張ボードの RTC (PCF8563) の時刻を、日本標準時で OLED に表示する。時刻は、段階 3 と段階 4 で設定する (`node_time_set()` が、BLE で届いた時刻と WiFi で取った時刻を、RTC に設定する)。
  - [x] 段階 3: ゲートウェイが SNTP で取った時刻を BLE の GATT でノードに書き込み、ノードが RTC に設定する。ノードは、書き込み専用の時刻の特性 (`9F3C1A02-...`) を持つ。ゲートウェイは、接続したノードに 1 時間ごとに書き込む (`ble.c`)。単体テストで確かめている。
  - [x] OLED のグラフ: 温度と湿度の履歴の折れ線を、ユーザボタン (D1) で切り替えるページに表示する (`history.c`、`button.c`)。履歴は、170 秒ごとの平均の 128 点 (約 6 時間)。測定値のない区切りには、線を引かない。起動のときに、SD カードの記録 (`backlog.c`) から区切りごとの平均を作り直して、履歴を戻す (`history_store.c`)。
  - [x] 段階 4: BLE で時刻が届かないときの予備として、ノードが WiFi で直接 SNTP の時刻を取る (`wifi_time.c`。`CONFIG_THERMO_WIFI_TIME`)。起動の 1 分後から 1 分ごとに、RTC に時刻があるかを確かめる。なければ、WiFi につないで時刻を取り、RTC に設定して、WiFi を切る (最大 5 回)。SSID とパスワードは、シェル (`thermo set`) か `scripts/send-wifi.sh` で設定する。
  - [ ] 残り: WiFi と BLE を同時に長時間動かしても、アドバタイズとセンサの読み取りが続くか。スタックとヒープに余裕があるか。
- [x] **ノードのログを SD カードに保存する** (実装した。**実機では、まだ確かめていない**): 起動から 60 秒の全てのログ (`boot.log`) と、エラーのログ (`error.log`) を、テキストで保存する (`sdlog.c`。ログのバックエンド。[sd_log.md](docs/sd_log.md))。
- [ ] **測定値を CSV で mini SD カードに保存する** (拡張ボードの SD カードスロット。SPI): Zephyr の FAT (FatFS) で、CSV (時刻、温度、湿度) を追記する。Zephyr が対応するのは ext2、FAT、LittleFS だけで、ext4 には対応していない。PC で読むには、FAT32 が簡単。SPI の SD の設定は、シールドの `seeed_xiao_expansion_board` と同じ (CS は D2)。SD の取り外しと、書き込み中の電源断の対策 (定期的に同期する) も考える。
- [ ] **MCUboot と、WiFi + HTTPS による OTA** (ノードとゲートウェイ。2026-10-19): 新しいファームウェアを WiFi と HTTPS でダウンロードして、MCUboot で入れ替える。
  - **今のフラッシュの配置のまま入る**: `xiao_esp32c3` の既定の配置 (`partitions_0x0_default_4M.dtsi`) は、`mcuboot` 64 KB (0x0)、`image-0` 1792 KB (0x20000)、`image-1` 1792 KB (0x1E0000)、`storage` 192 KB (0x3B0000)。今のイメージは、node が約 940 KB、gateway が約 850 KB で、1 つのスロットの約半分。設定 (NVS) は `storage_partition` のままなので、残るはず (実機で確かめる)。
  - 段階 1: **sysbuild で MCUboot を入れる**。`sysbuild.conf` に `SB_CONFIG_BOOTLOADER_MCUBOOT=y` を書いて、`west build --sysbuild` でビルドする。`scripts/build.sh`、`docker-compose.yml`、CI (`build.yml`) を直す。ビルドに警告を出さない (MCUboot のソースは、`app/warnings.txt` の対象外)。まず gateway で、MCUboot から起動するところまでを実機で確かめる。
  - 段階 2: **書き込みの手順を変える**。MCUboot は 0x0 に、署名つきのアプリは 0x20000 に書く。`scripts/flash.sh` と `.claude/CLAUDE.md` と `docs/setup.md` と `README.md` を直す。
  - 段階 3: **署名の鍵**。imgtool の鍵を作り、プライベートに管理する (公開のリポジトリと CI の成果物には、鍵を入れない)。CI のビルドは、MCUboot に付属するテスト用の鍵で署名する (実機には使わない)。鍵のパスは、Kconfig (`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`) で外から渡す。
  - 段階 4: **ダウンロードと書き込み**。両方のアプリで共有する `ota.c` を、`app/common/` に足す。Zephyr の `http_client` の HTTPS の GET で少しずつ受け取り、`flash_img` で `image-1` に書く。TLS には、今の mbedTLS と、サーバの CA (S3 の署名つき URL なら Amazon Root CA) を使う。書き終えたら、`boot_request_upgrade()` で次の起動での入れ替えを頼んで、再起動する。
  - 段階 5: **確定と巻き戻し**。新しいイメージは、起動して正常に動いたこと (例: WiFi と MQTT につながった) を確かめてから、`boot_write_img_confirmed()` で確定する。確定せずに再起動すると、前のイメージに戻る。**未確認**: Espressif 版の MCUboot が、どちらのスワップの方式 (swap か overwrite-only か) に対応しているか。overwrite-only では巻き戻せないので、ダウンロードしたイメージの確認 (SHA-256、署名) を、書き込みの前後で厳しくする。
  - 段階 6: **更新の知らせ方**。AWS IoT Jobs (MQTT の `$aws/things/<thing>/jobs/...`) で URL、バージョン、SHA-256 を受け取り、結果 (成功か失敗か) を返す。または、シェルのコマンド (`thermo ota <URL>`) で、手で始める (先にこちらを作る)。ノードは、ゲートウェイにつながっていて WiFi を使っていないことがあるので、更新を確かめるときだけ WiFi につなぐ。
  - 段階 7: **単体テスト**。`ota.c` の各段階 (接続、受信、書き込み、要求、確定) と、失敗の伝わり方を試す。`http_client`、`flash_img`、`boot_*` は、FFF のモックにする。カバレッジ 100% を保つ。`native_sim` のビルド (`*-sim`) は、MCUboot なしのままにする。
  - **注意すること**: ノードの静的な RAM の空きは、約 1.5 KB しかない (受信のバッファと TLS の接続を、MQTT と同時に持てるか。MQTT を切ってからダウンロードするか)。ダウンロードの途中の電源断と通信の切断 (やり直すときは、最初から)。ダウングレードを防ぐか (MCUboot の `MCUBOOT_DOWNGRADE_PREVENTION`)。起動画面と起動音の素材を入れたイメージは、公開の場所に置かない (S3 は非公開にして、署名つき URL で渡す)。
- [ ] **Grove のセンサ**: I2C の Grove (D4/D5。OLED、RTC、AHT20、BMP280 と同じバス) に、CO2 のセンサ (SCD40 など) をつなぐ。BLE の特性の形式と AWS のペイロードを、広げる必要がある。
- [ ] **ADC のアナログセンサ** (LM35 など) に替えるときの、実機での確認: 実装はある。オーバーレイの `thermo-sensor` を消して、`zephyr,user` の `io-channels` を書く。換算には、`CONFIG_THERMO_ADC_MV_PER_DEG` と `CONFIG_THERMO_ADC_OFFSET_MV` を使う。ADC エミュレータの単体テストで確かめている。センサを買ったら、換算の値を実機で確かめる。
- [ ] 実機の Bluetooth アダプタを使う `native_sim` の実行 (`--bt-dev=hciN`) を、docker compose のサービスにする。`run-sim` は、仮想コントローラ `btvirt` で動く。
- [ ] 行末コメントの見直し: `tests/` のローカル変数のコメントのうち、変数名から機械的に付けたものが、ファイルごとの意味に合っているかを確かめて直す。(2026-10-11: コメントがなかった 29 件には、意味を書いた)
- [ ] **起動のときの SD カードの記録の読み込み (`backlog_init()` の `scan()`) を速くする** (2026-11-10): 記録が上限の 100000 件あると、全件を読むので、実機で約 2.8 秒かかる (起動のログの `SD card ready: ... scan N ms`)。時刻の表示はその前に出るが、読み込みの間はメインループが止まるので、最初の測定が起動の約 6.7 秒後になる。
  - 記録は時刻順の輪なので、次に書く位置 (`head`) は、二分探索で求められる (`backlog_foreach_since()` と同じ考え方)。
  - 未送信の数と、最も古い未送信の位置は、全件を読まないと求められない。これらを小さな別のファイルか NVS に保存して、`backlog_commit()` と `backlog_append()` で更新する (電源断のあとの整合の取り方を決める)。
  - `direct_init()` を、メインループの外 (ワークキューか別のスレッド) で動かす方法もある。その間も、測定と表示は止まらない。ただし、読み込みの途中に `backlog_append()` が呼ばれないようにする。
- [ ] **起動の最初の `sdlog_flush()` の時間** (2026-11-10): `main()` の最初の `backlog_mount()` のあとの、最初の `sdlog_flush()` (`boot.log` の作成) に、約 450 ms かかる (実機の起動のログで、`Sensor initialized` と次の行の差)。その分、BLE の初期化が遅れる。作成をあとに回すと、ログのバッファ (768 バイト) があふれる。バッファの大きさと、RAM の余り (dram0_0_seg が 99.96%) を見て決める。

## 対応済み

- [x] **SwitchBot 屋外用温湿度計**: 屋外用温湿度計 (Outdoor Meter) の温度、湿度、電池残量を受け取る。ほかの SwitchBot の機種 (Meter、Meter Plus など) は、機種コードごとに並びが違うので、対応していない。
- [x] **Thermo ノード**: DHT11 の読み取り (D7。配線は [setup.md](docs/setup.md))、BLE の配信、ゲートウェイの GATT 接続と通知の受信、ノードの温度の AWS IoT Core への送信 ([aws_setup.md](docs/aws_setup.md) の 1.1)。WiFi と BLE を同時に動かしても、温度が欠けないことも確かめた。
- [x] **温湿度センサ (DHT11)**: ノードは、Zephyr のセンサ API で DHT11 (D7、GPIO20) を読む (`app/thermo-node/boards/xiao_esp32c3.overlay`)。AWS には、`temperature_c` と `humidity` を送る。単体テストは、偽のセンサで確かめている (実機での確認は、上の項目)。
- [x] **温湿度と気圧のセンサ (AHT20 と BMP280)**: Grove の I2C につなぐ。BLE の通知は、温度 (int16、0.1 ℃)、湿度 (uint16、0.1 %)、気圧 (uint16、0.1 hPa) の 6 バイト。AWS には、`pressure_hpa` も送る。
- [x] **起動画面と起動音** (拡張ボードの OLED とパッシブブザー): 実装した (`oled_show_boot()`、`buzzer.c`。素材は `boot_assets.c`)。`THERMO_BOOT_SCREEN` と `THERMO_BUZZER` で外せる。単体テストは `tests/buzzer`、`tests/oled`、`tests/main` にある。ブザーは D3 (GPIO5) につなぐ。素材のサブモジュール (`app/thermo-node/private/`) の使い方は、`docs/setup.md` に書いてある。ボタン (D1) を押したときの音も、同じ仕組みで鳴らす (`button_assets.c`。`THERMO_BUTTON_SOUND` で外せる)。
- [x] ペイロードの UNIX 時刻 (`timestamp` [s]): publish するときに、受信したときの時刻を「今の UNIX 時刻 - 受信してからの経過」で求める。`uptime_ms` も残す。
- [x] サーバ証明書の有効期限の確認: `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` で、mbedTLS が、SNTP で同期したシステム時計を使う。一度も同期していないときは TLS が必ず失敗するので、MQTT に接続せず、間隔をあけてやり直す。(ビルドと実機での確認は、まだしていない。実機では、RAM の増加と、期限切れの証明書で接続が拒否されることを確かめる)
- [x] SNTP による時刻の同期 (ゲートウェイ): WiFi に接続するたびに、`CONFIG_THERMO_NTP_SERVER` (既定は `pool.ntp.org`) に問い合わせて、システム時計を合わせる。`CONFIG_THERMO_NTP_RESYNC_SEC` の間は、同期し直さない。失敗しても、MQTT の接続は続ける。単体テストは `tests/ntp` にある。(ビルドと実機での確認は、まだしていない)
- [x] AWS IoT Core への MQTT の送信 (ゲートウェイ): WiFi の接続、MQTT over TLS (相互認証)、JSON の publish、再接続 (間隔を倍々に増やす)、シェル (`thermo` コマンド) による設定と、フラッシュへの保存。(実機での確認は、未対応の項目を見てください)
- [x] BLE の GATT 接続と通知: ノードが測定値を通知し、ゲートウェイが接続して購読し、受信する。UUID は `app/common/thermo_ble_uuid.h` にある。`native_sim` と `btvirt` の `run-sim` で、接続から温度の受信までを動かせる。
- [x] Ztest と FFF の単体テストを、両方のアプリに足した (`app/*/tests/`。sensor のテストは、偽のセンサと ADC エミュレータを使う)。
- [x] GitHub Actions を、Docker イメージで動く `build`、`test`、`analyze`、`lint` のジョブに作り直した (レイヤーキャッシュあり)。
- [x] 静的解析 (gcc の `-fanalyzer`)、約 55 個の警告オプション、MISRA-C の例外規定 (`docs/misra.md`) を整えた。
- [x] Zephyr のコーディング規約に合わせた (clang-format、checkpatch.pl、行末の空白の確認。例外は `docs/coding_style.md`)。
