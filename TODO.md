# TODO

## 未対応

- [ ] **SwitchBot 屋外用温湿度計の、実機での確認**: アドバタイズの並び (サービスデータ 0xFD3D の機種コード 'w' と電池残量、製造者データ 0x0969 の温度と湿度の位置) は、公開仕様の記憶に基づく。実機で、温度と湿度が、アプリの SwitchBot アプリの値と、合うか確認する。他の SwitchBot の機種 (Meter、Meter Plus など) は、機種コードごとに並びが違うので、未対応。
- [ ] **AWS IoT Core への送信の、実機での確認**: WiFi と MQTT (TLS) は、実装して、単体テスト (モック) とビルドまでは確認したが、実機と AWS IoT Core には、つないでいない。手順は [AWS_SETUP.md](AWS_SETUP.md)。確認すること: (1) TLS のハンドシェイクが、`CONFIG_MBEDTLS_HEAP_SIZE` と `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN` で通るか (足りなければ増やす。ただし、RAM は、約 98%使っている)、(2) `cred buf` / `cred add` で PEM を登録できるか、(3) WiFi と BLE を同時に動かして、温度が欠けないか。
- [ ] `native_sim` で、クラウドへの送信 (ローカルの Mosquitto など) を確認する。いまの `native_sim` は、`CONFIG_THERMO_CLOUD=n`。
- [ ] **アドバタイズ方式** (BLE): ノードが、温度 (ADC の生値) を、アドバタイズデータ (製造者固有データ) に載せて送り、ゲートウェイは、接続せずに、スキャンだけで受信する。(2026-10-04)
  - 目的: 台数の制限をなくす。GATT 接続は、同時に接続できる台数に、上限がある (`ESP32_BT_CTLR_LE_MAX_CONN` は、既定で 3、設定できる範囲は 1〜9。WiFi と TLS を同時に動かすと、実際は 3〜5 台が目安)。
  - まずは GATT 接続 + 通知で実装した。アドバタイズ方式は、あとで実装して、CMake のオプション (例: `-DTHERMO_BLE_MODE=gatt|advertising`) で、GATT 方式と切り替えられるようにする。
  - 注意点: アドバタイズは、届く保証がなく、盗聴やなりすましに弱い。連番 (欠落の検出) や、認証コード (なりすまし対策) を、ペイロードに入れるか検討する。
  - 切り替えるときの設計: ノードの `ble.c` と、ゲートウェイの `ble.c` を、方式ごとのファイルに分けて、同じヘッダ (`ble.h`) の関数 (`ble_init()` など) と、温度を受け取るコールバックを、共通にする。MQTT 側 (ゲートウェイ) は、方式に依存しない。
- [ ] **実機用の ADC オーバーレイ** (`app/thermo-node/boards/xiao_esp32c3.overlay`): いまは、実機のビルドでも、`zephyr,user` の `io-channels` がないので、シミュレーション値 (乱数) を返す。温度センサの接続ピンと型番が決まったら、追加する。(ADC の経路は、ADC エミュレータの単体テストで確認している)
- [ ] **温度 (℃) への変換**: いまは、ADC の生値 (raw) のまま送る。センサが決まったら、変換式を足す (ノード側で変換するか、AWS 側で変換するかも、決める)。
- [ ] 実機の Bluetooth アダプタを使う `native_sim` の実行 (`--bt-dev=hciN`) を、docker compose のサービスにする。(`run-sim` は、仮想コントローラ `btvirt` で動く)
- [ ] ビルドとテストの確認: 標準ヘッダとローカル変数の行末コメントを足した変更を、Docker と Zephyr の環境で、ビルドして、単体テストを実行する。(コメントだけの変更だが、未確認)。BLE の送受信の `LOG_HEXDUMP_DBG` の追加も、ビルドして、単体テストを実行して、確認する。
- [ ] 行末コメントの見直し: `tests/` のローカル変数のコメントは、変数名から機械的に付けた。ファイルごとの意味に合っているか、確認して直す。
- [ ] 整形の確認: 手作業で揃えたコメントの桁を、Docker の `format-thermo` で確認する。(この環境の clang-format 18 は、 `.clang-format` で、無関係な行まで書き換える)
- [ ] 使っていない可能性のある `#include` を、確認して、削除する: `cfg.h` の `<stddef.h>`、`tests/ntp` の `<string.h>`、`tests/shell` と `tests/switchbot` の `<stdlib.h>`、`thermo-node/src/sensor.c` の `<stdbool.h>`。(間接的に必要かもしれないので、ビルドで確認する)

## 対応済み

- [x] ペイロードの UNIX 時刻 (`timestamp` [s]): 受信したときの時刻を、publish するときに、(今の UNIX 時刻) - (受信してからの経過) で求める。`uptime_ms` も、残す。
- [x] サーバ証明書の有効期限の確認: `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` で、mbedTLS がシステム時計 (SNTP で同期) を使う。1 度も同期していないときは、TLS が必ず失敗するので、MQTT に接続せず、間隔をあけて、やり直す。(ビルドと実機での確認は、未対応。実機では、RAM の増加と、期限切れの証明書で接続が拒否されることを確認する)
- [x] SNTP による時刻の同期 (ゲートウェイ): WiFi の接続のたびに、`CONFIG_THERMO_NTP_SERVER` (既定 `pool.ntp.org`) に問い合わせて、システム時計を合わせる (`CONFIG_THERMO_NTP_RESYNC_S` の間は、再同期しない。失敗しても、MQTT の接続は続ける)。単体テストは `tests/ntp`。(ビルドと実機での確認は、未対応)
- [x] AWS IoT Core への MQTT 送信 (ゲートウェイ): WiFi の接続、MQTT over TLS (相互認証)、JSON (生値) の publish、再接続 (間隔を倍々に増やす)、シェル (`thermo` コマンド) による設定と、フラッシュへの保存。(実機での確認は、未対応の項目を参照)
- [x] BLE の GATT 接続 + 通知: ノードが温度 (ADC の生値) を通知し、ゲートウェイが接続・購読して受信する (UUID は `app/common/thermo_ble_uuid.h`。`native_sim` + `btvirt` の `run-sim` で、接続から温度の受信まで確認済み)。
- [x] Ztest + FFF の単体テストを、両アプリに追加した (`app/*/tests/`。sensor は ADC エミュレータを使う)。
- [x] GitHub Actions を、Docker イメージで動く、`build` / `test` / `analyze` / `lint` ジョブに作り直した (レイヤーキャッシュあり)。
- [x] 静的解析 (gcc `-fanalyzer`)、警告オプション約 55 個、MISRA-C の例外規定 (`MISRA.md`) を整備した。
- [x] Zephyr のコーディング規約に合わせた (clang-format、checkpatch.pl、行末の空白の確認。例外は `CODING_STYLE.md`)。
