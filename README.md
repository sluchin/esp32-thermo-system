# esp32-thermo-system

Zephyr RTOS-based BLE thermometer system featuring a sensor node and a gateway for Seeed Studio XIAO ESP32C3.

## 概要

このプロジェクトは、ESP32C3 マイクロコントローラ上で Zephyr RTOS を使用したBLE（Bluetooth Low Energy）サーモメータシステムです。

- **Thermo Node**: 温度センサを備えたエッジノード。温度データを BLE 経由で配信
- **Thermo Gateway**: 複数のノードから温度データを収集・集約するゲートウェイ

## プロジェクト構成

```
esp32-thermo-system/
├── app/
│   ├── thermo-node/      # センサノードアプリケーション
│   │   ├── CMakeLists.txt
│   │   ├── prj.conf      # Zephyr 設定
│   │   └── src/          # ソースコード
│   └── thermo-gateway/   # ゲートウェイアプリケーション
│       ├── CMakeLists.txt
│       ├── prj.conf
│       └── src/
├── .github/workflows/    # CI/CD パイプライン
├── west.yml              # West マニフェスト
├── SETUP.md              # セットアップガイド
└── README.md             # このファイル
```

## クイックスタート

### シミュレーション（推奨・最速）

ハードウェアなしで Linux PC 上でテストしたい場合：

```bash
# ビルド
docker compose run --rm build-thermo-node-sim
docker compose run --rm build-thermo-gateway-sim

# 実行
./build/thermo-node-sim/zephyr/zephyr.exe &
./build/thermo-gateway-sim/zephyr/zephyr.exe
```

詳細は [SIMULATION.md](./SIMULATION.md) を参照してください。

### Docker を使用したビルド

Docker と Docker Compose がインストールされている場合、最も簡単な方法です：

```bash
# Thermo Node をビルド（ESP32C3用）
docker compose run --rm build-thermo-node

# Thermo Gateway をビルド（ESP32C3用）
docker compose run --rm build-thermo-gateway

# Thermo Node をビルド（シミュレーション）
docker compose run --rm build-thermo-node-sim

# Thermo Gateway をビルド（シミュレーション）
docker compose run --rm build-thermo-gateway-sim

# インタラクティブ開発シェル
docker compose run --rm dev
```

### 単体テストと静的解析

単体テストは Zephyr の Ztest と FFF (モック) で書かれていて、`native_sim` 上で実行します (`app/thermo-node/tests/` と `app/thermo-gateway/tests/`)。

```bash
# 単体テスト (Thermo Node / Thermo Gateway)
docker compose run --rm test-thermo-node
docker compose run --rm test-thermo-gateway
# カバレッジ (行と分岐が 100% でなければ失敗する)
docker compose run --rm coverage-thermo-node
docker compose run --rm coverage-thermo-gateway
# ドキュメント (Doxygen。docs/index.html を開く。警告があれば失敗する)
docker compose run --rm docs-thermo

# 静的解析 (gcc -fanalyzer。指摘があれば失敗する)
docker compose run --rm analyze-thermo-node
docker compose run --rm analyze-thermo-gateway
```

### 整形と lint

コーディングスタイルは Zephyr の規約に合わせています (違う点は [CODING_STYLE.md](CODING_STYLE.md))。

```bash
# 整形の確認 (整形が必要なら失敗する)
docker compose run --rm format-thermo
# 整形して、ファイルを書き換える
docker compose run --rm format-thermo-fix
# Zephyr の checkpatch.pl による検査
docker compose run --rm lint-thermo
# 行末の空白の確認 (全てのテキストファイル)
docker compose run --rm whitespace-thermo
```

### ローカル開発環境構築

Docker を使用しない場合は、[SETUP.md](./SETUP.md) を参照してください。

```bash
# West ワークスペース初期化
west init -l .

# Thermo Node をビルド（ESP32C3用）
west build -b xiao_esp32c3 app/thermo-node

# Thermo Gateway をビルド（ESP32C3用）
west build -b xiao_esp32c3 app/thermo-gateway

# Thermo Node をビルド（シミュレーション）
west build -b native_sim/native/64 app/thermo-node -d build/thermo-node-sim -- -DCONF_FILE=prj-native_sim.conf

# Thermo Gateway をビルド（シミュレーション）
west build -b native_sim/native/64 app/thermo-gateway -d build/thermo-gateway-sim -- -DCONF_FILE=prj-native_sim.conf
```

### デバッグログ

`LOG_DBG` と `LOG_HEXDUMP_DBG` (BLE で送受信したデータの 16 進ダンプ) は、通常のビルドでは、コードごと消えます。有効にするときは、cmake のオプション `THERMO_DEBUG_LOG` を `ON` にして、ビルドします (`CONFIG_LOG_DEFAULT_LEVEL=4` になります)。

```bash
# Docker: 環境変数 THERMO_DEBUG_LOG を ON にして、build-* のサービスを実行する (既定は OFF)
THERMO_DEBUG_LOG=ON docker compose run --rm build-thermo-node
THERMO_DEBUG_LOG=ON docker compose run --rm build-thermo-gateway

# West (Docker を使わない場合)
west build -p always -b xiao_esp32c3 app/thermo-node -- -DTHERMO_DEBUG_LOG=ON
```

設定を切り替えるときは、前のビルドが残っていると、反映されないことがあるので、ビルドのディレクトリ (`build/thermo-node` など) を削除してから、ビルドし直します (west は `-p always`)。

### フラッシング

```bash
# ビルド後、USB で接続した状態で実行
esptool.py -p /dev/ttyUSB0 write_flash 0x0 build/zephyr/zephyr.bin
```

## 機能

### Thermo Node
- ADC による温度センサ読取
- BLE GATT サービスで温度データ配信
- ログ出力（UART シリアルコンソール）

### Thermo Gateway
- BLE スキャンで周辺ノードを検出して、GATT で接続し、温度 (ADC の生値) の通知を受信 (最大 3 台)
- SwitchBot 屋外用温湿度計 (Outdoor Meter) のアドバタイズ (接続しない) を受信して、温度 (℃)・湿度・電池残量を、10 秒に 1 回、AWS IoT Core に送信
- WiFi + MQTT (TLS、クライアント証明書による相互認証) で、AWS IoT Core に温度を送信 (設定手順は [AWS_SETUP.md](AWS_SETUP.md))
- WiFi・エンドポイント・証明書は、シェルの `thermo` コマンドで設定して、フラッシュに保存
- デバッグシェル対応

## 対応ハードウェア

- **MCU**: ESP32C3
- **開発ボード**: Seeed Studio XIAO ESP32C3
- **通信**: BLE 5.0

## CI/CD

GitHub Actions により、以下の自動化が設定されています：

- **ビルド** (`build` ジョブ): 実機 (ESP32C3) 用とシミュレーション (`native_sim`) 用の、両アプリのビルド。警告が出たら失敗します
- **単体テスト** (`test` ジョブ): Thermo Node / Thermo Gateway の単体テスト
- **静的解析** (`analyze` ジョブ): gcc `-fanalyzer`。指摘があれば失敗します
- **整形と lint** (`lint` ジョブ): clang-format による整形の確認、Zephyr の `checkpatch.pl`、行末の空白の確認。指摘があれば失敗します
- **アーティファクト保存**: ビルド成果物 (ELF/BIN、シミュレーションは実行ファイル) を自動保存

ジョブは、ローカルと同じ Docker イメージで `docker compose` のサービスを実行します。イメージのレイヤーは、キャッシュされます。

## 開発フロー

1. `ccr-*` ブランチで機能開発
2. Push 時に GitHub Actions で、ビルド・単体テスト・静的解析・lint を自動実行
3. PR をマージする前に、全ジョブの成功を確認
4. main ブランチへのマージ

### rebase したあとの push

`git rebase` でコミットの履歴を書き換えたあとは、通常の `git push` は拒否されるので、次のコマンドで push します。

```bash
git push --force-with-lease --force-if-includes origin <ブランチ名>
```

- `--force` は使わない。リモートに、手元に取り込んでいないコミット (他の人や別の環境の push) があれば、push は失敗する。失敗したら、`git fetch` で取り込んでから、やり直す。
- push は、ユーザーに依頼されたときだけ行う (AI エージェントも同じ)。

## デバッグ

### シリアルコンソール接続

```bash
picocom -b 115200 /dev/ttyUSB0
```

### Zephyr ログレベル

`prj.conf` で以下のように設定可能：

```conf
CONFIG_LOG_MODE_IMMEDIATE=y
CONFIG_LOG_DEFAULT_LEVEL=4  # DEBUG レベル
```

## トラブルシューティング

問題が発生した場合は、[SETUP.md](./SETUP.md) のトラブルシューティングセクションを参照してください。

## ライセンス

[LICENSE](./LICENSE) を参照してください。

## リンク

- [Zephyr Project](https://www.zephyrproject.org/)
- [ESP32C3 データシート](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [XIAO ESP32C3 Wiki](https://wiki.seeedstudio.com/xiao_esp32c3_getting_started/)
