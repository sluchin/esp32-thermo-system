# native_sim による Zephyr シミュレーション

このプロジェクトは、Zephyr の `native_sim` ボードを使用して、Linux PC上で ESP32C3 アプリケーションをシミュレーションできるようになりました。

## 概要

`native_sim` により以下が可能です：

- **ハードウェアなしでテスト** - 実際のESP32デバイスなしでアプリケーションをビルド・実行
- **高速開発ループ** - Dockerコンテナを使用した迅速なビルド
- **BLEシミュレーション** - Bluetooth Low Energyのシミュレーション機能
- **温度センサーシミュレーション** - ランダムな温度値を生成

## ビルド方法

### 1. Docker Compose を使用（推奨）

```bash
# Thermo Node をシミュレーション用にビルド
docker compose run --rm build-thermo-node-sim

# Thermo Gateway をシミュレーション用にビルド
docker compose run --rm build-thermo-gateway-sim
```

### 2. 直接 west コマンドを使用

```bash
# Thermo Node
west build -b native_sim/native/64 app/thermo-node -d build/thermo-node-sim -- -DCONF_FILE=prj-native_sim.conf

# Thermo Gateway
west build -b native_sim/native/64 app/thermo-gateway -d build/thermo-gateway-sim -- -DCONF_FILE=prj-native_sim.conf
```

## 実行方法

ビルド後、生成された実行ファイルを実行します：

```bash
# Thermo Node を実行
./build/thermo-node-sim/zephyr/zephyr.exe

# Thermo Gateway を実行
./build/thermo-gateway-sim/zephyr/zephyr.exe
```

このままでは Bluetooth コントローラ（HCI）がないため、BLE の初期化に失敗します（`HCI driver is not ready`）。BLE を動かす場合は、次のどちらかを使ってください。

### BLE を使う実行（仮想コントローラ・推奨）

BlueZ の `btvirt` が作る仮想 Bluetooth コントローラに、Node と Gateway の両方を接続します。Bluetooth ハードウェアは不要で、Docker 内で完結します。

```bash
docker compose run --rm run-sim
```

Node がアドバタイズし、Gateway がそれを検出するログが出ます（停止は Ctrl+C）。`btvirt` は `Dockerfile` で `bluez-test-tools` としてインストールされるため、初回は `docker compose build` を実行してください。

`btvirt` は暗号処理に AF_ALG ソケットを使いますが、Docker の既定の seccomp と AppArmor はこれを拒否します。拒否されると `btvirt` が接続を切り、Node と Gateway は BLE の初期化で止まります。そのため `run-sim` は `seccomp=unconfined` と `apparmor=unconfined` を指定しています。

ホスト上で直接実行する場合は、`btvirt -s` を起動してから、各実行ファイルに `--bt-dev=/tmp/bt-server-bredrle` を付けます。

### 実行例

```
*** Booting Zephyr OS build v3.x.x ***
[00:00:00.000,000] <inf> thermo_node: Thermo Node started (SIMULATOR MODE)
[00:00:00.001,000] <inf> sensor_thermo_node: Simulated ADC sensor initialized
[00:00:00.002,000] <inf> ble_thermo_node: Bluetooth initialized
[00:00:00.003,000] <inf> ble_thermo_node: Advertising started
[00:00:05.004,000] <inf> thermo_node: Temperature: 2847 (raw ADC value)
[00:00:10.005,000] <inf> thermo_node: Temperature: 1923 (raw ADC value)
...
```

## シミュレーションモードの特徴

### 温度センサー

- **シミュレーション時**: 0～4095 のランダムな値を返す（12ビット ADC の範囲）
- **実機時**: 実際のADCから温度データを読取

### BLE（Bluetooth Low Energy）

- **シミュレーション時**: BlueZ の `btvirt` が作る仮想コントローラを使用（「BLE を使う実行」を参照）
- **実機時**: ESP32C3 のハードウェアBLEを使用

### デバイス名

シミュレーション用と実機用でデバイス名が異なります：

- **Thermo Node**
  - 実機: `Thermo-Node`
  - シミュレーション: `Thermo-Node-Sim`

- **Thermo Gateway**
  - 実機: `Thermo-Gateway`
  - シミュレーション: `Thermo-Gateway-Sim`

## 設定ファイル

### `app/thermo-node/prj-native_sim.conf`

native_sim 用の Zephyr プロジェクト設定：

- `CONFIG_SIMULATOR=y` - シミュレーターモードを有効化
- `CONFIG_TEST_RANDOM_GENERATOR=y` - ランダム数生成を有効化

### `app/thermo-node/boards/native_sim.overlay`

native_sim ボード用の device tree overlay。

## 開発ワークフロー

1. **ローカル開発**
   ```bash
   # シミュレーション用にビルド
   docker compose run --rm build-thermo-node-sim
   docker compose run --rm build-thermo-gateway-sim

   # Node を実行してテスト
   ./build/thermo-node-sim/zephyr/zephyr.exe &

   # Gateway を実行してテスト
   ./build/thermo-gateway-sim/zephyr/zephyr.exe
   ```

2. **実機テスト前の検証**
   - シミュレーションで基本機能を確認
   - ハードウェアなしで問題を検出

3. **本番環境への展開**
   ```bash
   # ESP32C3 用にビルド
   docker compose run --rm build-thermo-node
   docker compose run --rm build-thermo-gateway

   # フラッシング
   # 2 台つないでいるときは, 台ごとにポートが違う (PORT で指定する)
   scripts/flash.sh node /dev/ttyACM0
   scripts/flash.sh gateway /dev/ttyACM1
   ```

## トラブルシューティング

### ビルドエラー

「ADC not configured」というエラーが出る場合：

- 実機用 `prj.conf` と シミュレーション用 `prj-native_sim.conf` が正しく分離されているか確認
- `-DCONF_FILE=` オプションが正しく指定されているか確認

### 実行時エラー

シミュレーション実行時に実装不足の機能エラーが出る場合：

- `CONFIG_SIMULATOR` が有効になっているか確認
- ランダム数生成関連の設定を確認

## 今後の拡張

- [ ] TCP/IP のシミュレーション
- [ ] 複数ノードの同時シミュレーション
- [ ] BLE トレースのエクスポート
- [ ] パフォーマンスプロファイリング

## リファレンス

- [Zephyr native_sim Documentation](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)
- [Zephyr Simulation Environment](https://docs.zephyrproject.org/latest/develop/testing/index.html)
