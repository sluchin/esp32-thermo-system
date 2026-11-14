# native_sim 対応の仕組み

## 概要

同じソースから、ESP32C3 の実機用と、Zephyr の `native_sim` ボード (Linux の上で動くシミュレーション) 用の両方をビルドできます。使い方は、[simulation.md](simulation.md) にあります。このドキュメントは、切り替えの仕組みを説明します。

## 切り替えの仕組み

### 1. センサの読み取り

**ファイル**: `app/thermo-node/src/sensor.c`

- `CONFIG_SIMULATOR` で、実装を切り替えます。
- **実機**: Devicetree で選んだセンサ (センサ API の AHT20 + BMP280 や DHT11、または ADC) から、温度、湿度、気圧を読みます。センサがあるかは、Devicetree (alias `thermo-sensor` と、`zephyr,user` の `io-channels`) で判定します。
- **シミュレーション**: `sys_rand32_get()` で、温度 (10.0〜49.9 ℃)、湿度 (30.0〜89.9 %)、気圧 (990.0〜1029.9 hPa) の乱数を作ります。

### 2. 起動のログ

**ファイル**: `app/thermo-node/src/main.c`、`app/thermo-gateway/src/main.c`

`CONFIG_SIMULATOR` が有効なときは、起動のログが `Thermo Node started (SIMULATOR MODE)` (ゲートウェイは `Thermo Gateway started (SIMULATOR MODE)`) になります。BLE の初期化 (`ble_init()`) と、ノードのアドバタイズ (`ble_advertise()`)、ゲートウェイのスキャン (`ble_scan()`) は、実機と同じコードです。

### 3. プロジェクトの設定

`native_sim` では、`prj.conf` の代わりに `prj-native_sim.conf` を使います (`-DCONF_FILE=prj-native_sim.conf`)。実機用の `prj.conf` は変わりません。

- `app/thermo-node/prj-native_sim.conf`: `CONFIG_SIMULATOR=y`、`CONFIG_TEST_RANDOM_GENERATOR=y` (乱数)、`CONFIG_MAIN_STACK_SIZE=8192`、BLE のペリフェラルの設定 (デバイス名は `Thermo-Node-Sim`)。
- `app/thermo-gateway/prj-native_sim.conf`: `CONFIG_SIMULATOR=y`、`CONFIG_THERMO_CLOUD=n` (AWS IoT Core に送らない)、`CONFIG_MAIN_STACK_SIZE=8192`、BLE のセントラルの設定 (デバイス名は `Thermo-Gateway-Sim`)。

### 4. Devicetree の overlay

**ファイル**: `app/thermo-node/boards/native_sim_native_64.overlay`

`native_sim/native/64` 用に、空の `zephyr,user` ノードを定義します。`io-channels` がないので、ADC の経路は使われません。

### 5. Docker Compose のサービス

`docker-compose.yml` の `build-node-sim` と `build-gateway-sim` が、`native_sim` 用にビルドします。

```bash
docker compose run --rm build-node-sim
docker compose run --rm build-gateway-sim
```

## 動作の流れ

### ビルド

```text
docker compose run --rm build-node-sim
  ↓
west build -b native_sim/native/64 app/thermo-node -d build/thermo-node-sim -- -DCONF_FILE=prj-native_sim.conf
  ↓
CONFIG_SIMULATOR が定義される
  ↓
sensor.c: センサと ADC のコードを使わずに乱数に切り替える
main.c: 起動のログに SIMULATOR MODE を出す
  ↓
build/thermo-node-sim/zephyr/zephyr.exe ができる
```

### 実行

```text
./build/thermo-node-sim/zephyr/zephyr.exe
  ↓
"SIMULATOR MODE" のログを出す
  ↓
sensor_init() → シミュレーションのセンサを初期化する
  ↓
ble_init() → Bluetooth のスタックを初期化する
  ↓
ble_advertise() → BLE のアドバタイズを始める
  ↓
5 秒ごとに sensor_read() で乱数の値を読む
```
