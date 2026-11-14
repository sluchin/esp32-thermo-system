# native_sim による Zephyr シミュレーション

Zephyr の `native_sim` ボードを使うと、ESP32C3 のアプリケーションを、Linux PC の上でシミュレーションできます。

## 概要

`native_sim` では、次のことができます。

- **ハードウェアなしでテスト**: ESP32 の実機なしで、アプリケーションをビルドして実行する
- **速い開発のループ**: Docker のコンテナで、すばやくビルドする
- **BLE のシミュレーション**: 仮想の Bluetooth コントローラで、BLE を動かす
- **センサのシミュレーション**: 温度、湿度、気圧の乱数を作る

## ビルド方法

### 1. Docker Compose を使う (推奨)

```bash
# Thermo Node をシミュレーション用にビルド
docker compose run --rm build-node-sim

# Thermo Gateway をシミュレーション用にビルド
docker compose run --rm build-gateway-sim
```

### 2. west コマンドを直接使う

```bash
# Thermo Node
west build -b native_sim/native/64 app/thermo-node -d build/thermo-node-sim -- -DCONF_FILE=prj-native_sim.conf

# Thermo Gateway
west build -b native_sim/native/64 app/thermo-gateway -d build/thermo-gateway-sim -- -DCONF_FILE=prj-native_sim.conf
```

## 実行方法

ビルドしたら、できた実行ファイルを実行します。

```bash
# Thermo Node を実行
./build/thermo-node-sim/zephyr/zephyr.exe

# Thermo Gateway を実行
./build/thermo-gateway-sim/zephyr/zephyr.exe
```

ただし、このままでは Bluetooth コントローラ (HCI) がないので、BLE の初期化に失敗します (`HCI driver is not ready`)。BLE を動かすときは、次の仮想コントローラを使います。

### BLE を使う実行 (仮想コントローラ)

BlueZ の `btvirt` が作る仮想の Bluetooth コントローラに、Node と Gateway の両方をつなぎます。Bluetooth のハードウェアは要らず、Docker の中で完結します。

```bash
docker compose run --rm run-sim
```

Node がアドバタイズして、Gateway がそれを見つけるログが出ます (止めるときは Ctrl+C)。`btvirt` は、`Dockerfile` が `bluez-test-tools` としてインストールします。イメージにまだ入っていなければ、先に `docker compose build` を実行してください。

`btvirt` は、暗号の処理に AF_ALG ソケットを使います。Docker の既定の seccomp と AppArmor は、これを拒否します。拒否されると、`btvirt` が接続を切り、Node と Gateway は BLE の初期化で止まります。そのため、`run-sim` は `seccomp=unconfined` と `apparmor=unconfined` を指定しています。

ホストの上で直接実行するときは、`btvirt -s` を起動してから、各実行ファイルに `--bt-dev=/tmp/bt-server-bredrle` を付けます。

### 実行例

```text
*** Booting Zephyr OS build v4.3.0 ***
[00:00:00.000,000] <inf> thermo_node: Thermo Node started (SIMULATOR MODE)
[00:00:00.000,500] <inf> thermo_node: Version 0.1.0 (git 584daa5c1f2e)
[00:00:00.001,000] <wrn> sensor_thermo_node: No sensor in device tree, using simulated values
[00:00:00.002,000] <inf> ble_thermo_node: Bluetooth initialized
[00:00:00.003,000] <inf> ble_thermo_node: Advertising started
[00:00:05.004,000] <inf> thermo_node: Temperature: 28.4 C, humidity: 61.7 %
[00:00:05.004,000] <inf> thermo_node: Pressure: 1003.8 hPa
[00:00:10.005,000] <inf> thermo_node: Temperature: 19.2 C, humidity: 47.3 %
[00:00:10.005,000] <inf> thermo_node: Pressure: 1021.5 hPa
...
```

## シミュレーションモードの特徴

### センサ

- **シミュレーション**: 温度 (10.0〜49.9 ℃)、湿度 (30.0〜89.9 %)、気圧 (990.0〜1029.9 hPa) の乱数を返す
- **実機**: Devicetree で選んだセンサから読む (AHT20 + BMP280 や DHT11 などのセンサ API、または ADC。[setup.md](setup.md) の「Thermo ノードのセンサ」)

### BLE (Bluetooth Low Energy)

- **シミュレーション**: BlueZ の `btvirt` が作る仮想コントローラを使う (「BLE を使う実行」を参照)
- **実機**: ESP32C3 のハードウェアの BLE を使う

### デバイス名

デバイス名は、シミュレーション用と実機用で違います。

- **Thermo Node**
  - 実機: `Thermo-Node`
  - シミュレーション: `Thermo-Node-Sim`

- **Thermo Gateway**
  - 実機: `Thermo-Gateway`
  - シミュレーション: `Thermo-Gateway-Sim`

## 設定ファイル

### app/thermo-node/prj-native_sim.conf

native_sim 用の Zephyr のプロジェクトの設定です。おもな項目は、次のとおりです。

- `CONFIG_SIMULATOR=y`: シミュレーションのモードにする
- `CONFIG_TEST_RANDOM_GENERATOR=y`: 乱数の生成を有効にする

### app/thermo-node/boards/native_sim_native_64.overlay

native_sim/native/64 ボード用の Devicetree の overlay です。Zephyr は `boards/<ボード>_<修飾子>.overlay` を探すので、`native_sim.overlay` という名前では読まれません。

## 開発ワークフロー

1. **ローカルでの開発**

   ```bash
   # シミュレーション用にビルド
   docker compose run --rm build-node-sim
   docker compose run --rm build-gateway-sim

   # Node を実行してテスト
   ./build/thermo-node-sim/zephyr/zephyr.exe &

   # Gateway を実行してテスト
   ./build/thermo-gateway-sim/zephyr/zephyr.exe
   ```

2. **実機で試す前の確認**
   - シミュレーションで、基本の機能を確かめる
   - ハードウェアなしで、問題を見つける

3. **実機への書き込み**

   ```bash
   # ESP32C3 用にビルド
   docker compose run --rm build-node
   docker compose run --rm build-gateway

   # フラッシング
   # 2 台つないでいるときは台ごとにポートが違う (PORT で指定する)
   scripts/flash.sh node /dev/ttyACM0
   scripts/flash.sh gateway /dev/ttyACM1
   ```

## トラブルシューティング

### ビルドエラー

実行したときの `No sensor in device tree` の警告は、シミュレーションの値を使うときの正常なログです。ビルドでエラーが出るときは、次を確かめます。

- 実機用の `prj.conf` と、シミュレーション用の `prj-native_sim.conf` を取り違えていないか
- `-DCONF_FILE=` のオプションを、正しく指定しているか

### 実行時エラー

シミュレーションの実行で、機能が足りないというエラーが出るときは、次を確かめます。

- `CONFIG_SIMULATOR` が有効になっているか
- 乱数の生成の設定 (`CONFIG_TEST_RANDOM_GENERATOR`)

## 今後の拡張

- [ ] TCP/IP のシミュレーション
- [ ] 複数ノードの同時シミュレーション
- [ ] BLE トレースのエクスポート
- [ ] パフォーマンスプロファイリング

## リファレンス

- [Zephyr native_sim Documentation](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)
- [Zephyr Testing](https://docs.zephyrproject.org/latest/develop/test/index.html)
