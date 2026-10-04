# TODO

## 未対応

- [ ] **AWS IoT Core への MQTT 送信** (ゲートウェイ): WiFi、証明書、エンドポイントを、Zephyr シェルで設定して、受信した温度 (生値) を、JSON で publish する。
- [ ] **アドバタイズ方式** (BLE): ノードが、温度 (ADC の生値) を、アドバタイズデータ (製造者固有データ) に載せて送り、ゲートウェイは、接続せずに、スキャンだけで受信する。(2026-10-04)
  - 目的: 台数の制限をなくす。GATT 接続は、同時に接続できる台数に、上限がある (`ESP32_BT_CTLR_LE_MAX_CONN` は、既定で 3、設定できる範囲は 1〜9。WiFi と TLS を同時に動かすと、実際は 3〜5 台が目安)。
  - まずは GATT 接続 + 通知で実装した。アドバタイズ方式は、あとで実装して、CMake のオプション (例: `-DTHERMO_BLE_MODE=gatt|advertising`) で、GATT 方式と切り替えられるようにする。
  - 注意点: アドバタイズは、届く保証がなく、盗聴やなりすましに弱い。連番 (欠落の検出) や、認証コード (なりすまし対策) を、ペイロードに入れるか検討する。
  - 切り替えるときの設計: ノードの `ble.c` と、ゲートウェイの `ble.c` を、方式ごとのファイルに分けて、同じヘッダ (`ble.h`) の関数 (`ble_init()` など) と、温度を受け取るコールバックを、共通にする。MQTT 側 (ゲートウェイ) は、方式に依存しない。
- [ ] **実機用の ADC オーバーレイ** (`app/thermo-node/boards/xiao_esp32c3.overlay`): いまは、実機のビルドでも、`zephyr,user` の `io-channels` がないので、シミュレーション値 (乱数) を返す。温度センサの接続ピンと型番が決まったら、追加する。(ADC の経路は、ADC エミュレータの単体テストで確認している)
- [ ] **温度 (℃) への変換**: いまは、ADC の生値 (raw) のまま送る。センサが決まったら、変換式を足す (ノード側で変換するか、AWS 側で変換するかも、決める)。
- [ ] 実機の Bluetooth アダプタを使う `native_sim` の実行 (`--bt-dev=hciN`) を、docker compose のサービスにする。(`run-sim` は、仮想コントローラ `btvirt` で動く)

## 対応済み

- [x] BLE の GATT 接続 + 通知: ノードが温度 (ADC の生値) を通知し、ゲートウェイが接続・購読して受信する (UUID は `app/common/thermo_ble_uuid.h`。`native_sim` + `btvirt` の `run-sim` で、接続から温度の受信まで確認済み)。
- [x] Ztest + FFF の単体テストを、両アプリに追加した (`app/*/tests/`。sensor は ADC エミュレータを使う)。
- [x] GitHub Actions を、Docker イメージで動く、`build` / `test` / `analyze` / `lint` ジョブに作り直した (レイヤーキャッシュあり)。
- [x] 静的解析 (gcc `-fanalyzer`)、警告オプション約 55 個、MISRA-C の例外規定 (`MISRA.md`) を整備した。
- [x] Zephyr のコーディング規約に合わせた (clang-format、checkpatch.pl、行末の空白の確認。例外は `CODING_STYLE.md`)。
