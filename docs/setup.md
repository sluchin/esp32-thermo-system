# Zephyr OS Development Environment Setup

このドキュメントは、ESP32C3 で動く Zephyr RTOS ベースのサーモシステムについて、開発環境を作る手順をまとめたものです。

## Docker を使用したセットアップ（推奨）

### 1. Docker のインストール

Docker がまだインストールされていなければ、環境に合わせて、次の手順でインストールしてください。

#### Linux (Ubuntu / Debian)

公式のリポジトリから、Docker Engine と Docker Compose プラグインをインストールします。

```bash
# 必要なパッケージをインストール
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg

# Docker 公式 GPG キーを追加
sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg

# apt リポジトリを設定
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
  $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# Docker Engine & Docker Compose プラグインをインストール
sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

# 現在のユーザーを docker グループに追加 (sudo なしで実行可能にする)
sudo usermod -aG docker $USER
newgrp docker
```

#### macOS

1. [Docker Desktop for Mac](https://docs.docker.com/desktop/setup/install/mac-install/) をダウンロードして、インストールします (Apple Silicon と Intel に対応)。
2. インストールしたら、Docker Desktop を起動します。

#### Windows

1. WSL2 (Windows Subsystem for Linux 2) を有効にします。
2. [Docker Desktop for Windows](https://docs.docker.com/desktop/setup/install/windows-install/) をダウンロードして、インストールします。
3. 設定で「Use the WSL 2 based engine」が有効なことを確かめます。

---

### 2. ホストのユーザー ID を環境変数にエクスポートする

コンテナは、ホストのユーザーで動きます (`docker-compose.yml` の `user: "${UID:-0}:${GID:-0}"`)。そのため、`build/` などの生成物が root の所有になりません。

`UID` と `GID` はシェルの変数で、環境変数ではありません。`docker compose` に渡すため、シェルの設定ファイルでエクスポートします。設定しないと root (0:0) で動き、`build/` などの生成物が root の所有になります。

zsh の場合 (`~/.zshrc`):

```bash
export UID
export GID
```

bash の場合 (`~/.bashrc`):

```bash
export UID
export GID=$(id -g)
```

bash の `UID` は読み取り専用なので、`export UID=$(id -u)` とは書けません (`export UID` だけで足ります)。bash には `GID` 変数がないので、`id -g` で設定します。

設定したら、新しいシェルを開くか、`source ~/.zshrc` (または `source ~/.bashrc`) を実行します。次のコマンドで確かめます。

```bash
docker compose run --rm dev id
```

自分のユーザー ID とグループ ID が表示されれば成功です。

### 3. Docker を使用したビルドと開発

Docker と Docker Compose がインストールされていれば、次のコマンドで、ビルドや開発用のシェルを実行できます (Docker Compose V2 の `docker compose` コマンドを使います)。

最初に、Zephyr のソースと依存ツールを含むイメージをビルドします (時間がかかります)。`west.yml` を変更したときも、もう一度実行します。

```bash
docker compose build
```

```bash
# Thermo Node をビルド (ESP32C3用)
docker compose run --rm build-node

# Thermo Gateway をビルド (ESP32C3用)
docker compose run --rm build-gateway

# Thermo Node をビルド (シミュレーション用)
docker compose run --rm build-node-sim

# Thermo Gateway をビルド (シミュレーション用)
docker compose run --rm build-gateway-sim

# インタラクティブ開発シェル
docker compose run --rm dev

# 生成物 (build/) を全て消す (Doxygen の出力の build/docs/ も消す. 次のビルドは最初からやり直す)
docker compose run --rm clean
```

### Docker セットアップの利点

- ✅ Zephyr SDK の自動インストール
- ✅ 依存関係をすべて含む
- ✅ クロスプラットフォーム対応（Linux/macOS/Windows）
- ✅ ホストシステムを汚さない

詳細は、`Dockerfile` と `docker-compose.yml` を参照してください。

---

## ローカル開発環境セットアップ

Docker を使わないときの手順です。

### 前提条件

- Linux（Ubuntu 20.04以上推奨）、macOS、またはWindows Subsystem for Linux (WSL2)
- Python 3.8以上
- Git
- CMake 3.20.0以上
- GNU ARM Embedded Toolchain

## インストール手順

### 1. 依存ツールのインストール

```bash
# Ubuntu/Debian の場合
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    python3-pip \
    python3-venv \
    git \
    wget \
    curl \
    libusb-1.0-0-dev \
    libusb-1.0-0

# Python 仮想環境の作成
python3 -m venv venv
source venv/bin/activate
```

### 2. Zephyr SDK のインストール

```bash
# Zephyr SDK をダウンロード (Linux x86_64 の場合)
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.17.4/zephyr-sdk-0.17.4_linux-x86_64.tar.xz

# インストール
tar xf zephyr-sdk-0.17.4_linux-x86_64.tar.xz -C ~/
~/zephyr-sdk-0.17.4/setup.sh

# インストールしたあと, ホストツールをセットアップする
cd ~/zephyr-sdk-0.17.4 && ./setup.sh
```

### 3. West のインストール

```bash
pip install west
```

### 4. Zephyr リポジトリの初期化

```bash
# リポジトリのクローン
git clone https://github.com/yourusername/esp32-thermo-system.git
cd esp32-thermo-system

# West ワークスペースの初期化
west init --mr main .
west update
```

### 5. 環境変数の設定

```bash
# ~/.bashrc または ~/.zshrc に以下を追加
export ZEPHYR_BASE=~/esp32-thermo-system/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-0.17.4
```

## ビルド方法

### Thermo Node のビルド

```bash
cd esp32-thermo-system
west build -b xiao_esp32c3 app/thermo-node
```

### Thermo Gateway のビルド

```bash
cd esp32-thermo-system
west build -b xiao_esp32c3 app/thermo-gateway
```

### クリーンビルド

```bash
west build -p always -b xiao_esp32c3 app/thermo-node
```

## フラッシング方法

### 前提条件

- esptool のインストール (コマンド名は `esptool`。`esptool.py` は古い名前)

```bash
pipx install esptool
```

### フラッシング手順

1. ESP32C3 を USB で接続

2. デバイスポート確認

   ```bash
   ls /dev/ttyACM*  # Linux (XIAO ESP32C3 は USB を内蔵しているので ttyACM)
   ls /dev/tty.usbmodem*  # macOS
   ```

   シリアルポートを使うには、ユーザーを `dialout` グループに入れます (入れたら、ログインし直します)。

   ```bash
   sudo usermod -aG dialout $USER
   ```

3. ファームウェアをフラッシング

   ```bash
   # Thermo Node
   esptool -p /dev/ttyACM0 write-flash 0x0 build/thermo-node/zephyr/zephyr.bin

   # Thermo Gateway
   esptool -p /dev/ttyACM0 write-flash 0x0 build/thermo-gateway/zephyr/zephyr.bin

   # スクリプト (TARGET は node / gateway. 2 台つなぐときは台ごとに PORT を指定する)
   scripts/flash.sh gateway /dev/ttyACM0
   ```

## Thermo ノードのセンサ

Thermo ノードは、温度、湿度、気圧をセンサから読みます。使うセンサは、**Devicetree** で決めます (コードは変えません)。

| 優先 | Devicetree の設定 | センサ |
|:---|:---|:---|
| 1 | alias `thermo-sensor` のセンサのノードがある | Zephyr のセンサ API で読む。AHT20、DHT11 など。温度と湿度。alias `thermo-pressure` のセンサ (BMP280 など) があれば、気圧も読む |
| 2 | `zephyr,user` に `io-channels` がある | ADC を読んで、温度に換算する (アナログの温度センサ。湿度と気圧はなし) |
| 3 | どちらもない (`native_sim` など) | 乱数のシミュレーション値 (温度、湿度、気圧) |

既定は、AHT20 + BMP280 のモジュールです。DHT11 にするときは、`THERMO_SENSOR=dht11` を付けてビルドします (下の「DHT11 の配線」)。

### AHT20 + BMP280 の配線 (既定)

「Aht20+Bmp280 高精度デジタル湿度気圧センサー」などの名前で売られている、4 ピンのモジュール (DC 2.8 V〜5 V) です。1 枚の基板に、2 つのチップが載っています。

| チップ | 測るもの | I2C のアドレス | Devicetree |
|:---|:---|:---|:---|
| AHT20 | 温度、湿度 | `0x38` | `aht20` のノード (`aosong,aht20`)。alias `thermo-sensor` |
| BMP280 | 気圧 (と温度。温度は使わない) | `0x77` (モジュールによっては `0x76`) | `bmp280` のノード (`bosch,bme280`。チップの ID で BMP280 として動く)。alias `thermo-pressure` |

I2C なので、OLED や RTC と同じ線 (拡張ボードの I2C。D4 が SDA、D5 が SCL) につなぎます。アドレスが違うので、同時に使えます。

| モジュール | XIAO ESP32C3 (拡張ボード) |
|:---|:---|
| `VCC` (`VIN`) | `3V3` |
| `GND` | `GND` |
| `SDA` | `D4` (SDA) |
| `SCL` | `D5` (SCL) |

- 拡張ボードの **Grove の I2C のコネクタ** (印字 `I2C`) につなげば、ほかの配線は要りません (Grove のケーブルは、黒が `GND`、赤が `VCC`、白が `SDA`、黄が `SCL`)。モジュールのピンの並びは製品ごとに違うので、基板の印字で確かめます。
- `VCC` は `3V3` につなぎます。モジュールは 5 V でも動きますが、I2C の線を 3.3 V より上げないためです。
- 気圧は、10 分の 1 hPa まで送ります (例: `1013.2`)。測れる範囲は、BMP280 が気圧 300〜1100 hPa (誤差 ±1 hPa)、AHT20 が温度 -40〜85 ℃ (誤差 ±0.3 ℃) と湿度 0〜100 % (誤差 ±2 %) です。
- **起動のログに `Pressure sensor is not ready, measuring without the pressure` が出たとき** (BMP280 が見つからず、気圧が測れないとき): BMP280 の I2C アドレスは、モジュールによって `0x77` か `0x76` です。既定は `0x77` なので、`0x76` のモジュールでは、このメッセージが出ます。次の手順で、`0x76` に変えます。
  1. `app/thermo-node/boards/xiao_esp32c3.overlay` を開いて、`bmp280@77` のブロックを探します。
  2. ノードの名前の `77` を `76` に、`reg` の `0x77` を `0x76` に書き換えます。名前と `reg` の数字は、そろえます。

     ```dts
     /* 変える前 */
     bmp280: bmp280@77 {
         reg = <0x77>;

     /* 変えたあと */
     bmp280: bmp280@76 {
         reg = <0x76>;
     ```

  3. ビルドし直して、書き込みます。起動のログに `Pressure sensor is not ready` が出なくなれば、成功です。

  このメッセージが出ても、温度と湿度は測って送ります。なくなるのは、気圧だけです。
- `Sensor is not ready` が出て起動が止まるときは、AHT20 が見つかっていません。配線 (特に `SDA` と `SCL` の入れ違い) を確かめます。

### DHT11 の配線

DHT11 (気圧なし) にするときは、`THERMO_SENSOR=dht11` を付けてビルドします。このとき、`app/thermo-node/sensors/dht11.overlay` と `sensors/dht11.conf` (`CONFIG_DHT_LOCK_IRQS=y`) が足されます。overlay は、センサの alias を DHT11 にして、AHT20 と BMP280 を使わない設定です。

```bash
# Docker
THERMO_SENSOR=dht11 docker compose run --rm build-node

# West
west build -b xiao_esp32c3 app/thermo-node -- -DEXTRA_DTC_OVERLAY_FILE=sensors/dht11.overlay -DEXTRA_CONF_FILE=sensors/dht11.conf
```

3 ピンの DHT11 モジュール (基板の印字は、ピンを下にして左から `+` `out` `-`) を、XIAO ESP32C3 の次のピンにつなぎます。

| DHT11 モジュール | 役割 | XIAO ESP32C3 |
|:---|:---|:---|
| `+` | 電源 | `3V3` (3.3 V) |
| `out` | データ (信号線) | `D7` (GPIO20) |
| `-` | グランド | `GND` |

- 基板にプルアップ抵抗が付いているので、追加の部品は要りません。
- 設定は `app/thermo-node/sensors/dht11.overlay` にあります (`dio-gpios = <&xiao_d 7 ...>`)。別のピンにつなぐときは、この `7` (D7) を変えます。
- データのピンに `D7` を選んだ理由: `D6` (GPIO21) には、起動のときに ROM のログが出るので、センサの信号にノイズが乗ります。`D8` (GPIO8) と `D9` (GPIO9) は、起動のときの設定 (ストラッピング) に使うピンです。
- DHT11 の範囲は、温度 0〜50 ℃ (1 ℃ きざみ、誤差 ±2 ℃)、湿度 20〜95 % (1 % きざみ、誤差 ±5 %) です。読み取りの間隔は、1 秒以上あけます (ノードは 5 秒ごと)。
- DHT22 (AM2303) を使うときは、`sensors/dht11.overlay` の `dht11` のノードに `dht22;` を足します。

#### Seeed Studio XIAO 拡張ボード (103030356) を使う場合

1. XIAO ESP32C3 を、拡張ボードの上側のソケットに差します。**USB-C のコネクタは、ボードの上の辺に向けます。** 上の辺は、OLED の反対側で、右のソケットの印字 `VBUS GND 3V3 10 9 8 7` の `VBUS` がある側です。向きを間違えると、故障の原因になります。XIAO の裏の印字 (`5V` `GND` `3V3` `D10` ...) が、ボードの印字と合っているかを確かめてください。
2. 両端がメスのジャンパワイヤで、DHT11 の足と、ボードの**下側にあるオスのピンのヘッダ** (黒い 2 列 × 4 本) をつなぎます。ヘッダの印字は、上の列が左から `GND` `3V3` `SWDIO` `SWCLK`、下の列が左から `GND` `5V` `TX'6` `RX'7` です。

   | DHT11 | ボードの下側のヘッダ |
   |:---|:---|
   | `+` | 上の列の `3V3` (左から 2 番目) |
   | `out` | 下の列の `RX'7` (右端。D7 = GPIO20) |
   | `-` | 上の列の `GND` (左端)。下の列の `GND` (左端) でも同じ |

3. 上側のソケット (メス) には、メスのワイヤをつなげません。メスのワイヤしかないときは、下側のオスのヘッダを使います。
4. `5V` には、つながないでください。DHT11 は 3.3 V でも動きます。XIAO ESP32C3 の信号は 3.3 V なので、5 V につなぐと、信号が 3.3 V を超えるおそれがあります。

### ADC のアナログセンサ (LM35 など) に替える

1. `app/thermo-node/boards/xiao_esp32c3.overlay` から、`aliases` の `thermo-sensor` と `thermo-pressure`、および `aht20` と `bmp280` のノードを消します。
2. `zephyr,user` に、`io-channels` と ADC のチャンネルの設定を書きます (例は `app/thermo-node/tests/sensor/adc.overlay`)。
3. `prj.conf` に、温度への換算を書きます。
   - `CONFIG_THERMO_ADC_MV_PER_DEG`: 1 ℃ あたりの電圧 [mV] (既定 10。LM35 の値)
   - `CONFIG_THERMO_ADC_OFFSET_MV`: 0 ℃ のときの電圧 [mV] (既定 0。LM35 の値)
4. ビルドディレクトリを消して、ビルドし直します (`rm -rf build/thermo-node`)。overlay の追加は、既存のビルドには反映されません。

### OLED の表示と時刻 (拡張ボード)

拡張ボードの OLED (SSD1306、128 x 64 ドット) に、温度、湿度、日付、時刻と、温度と湿度のグラフを表示します。ノードの XIAO ESP32C3 を拡張ボードに差せば、配線は要りません (OLED と RTC は、ボードの I2C。D4 が SDA、D5 が SCL。アドレスは、OLED が `0x3C`、RTC が `0x51`)。気圧は、OLED には表示しません (ログと AWS には送ります)。

```text
2026-10-08
14:05:09
Temp 23.5 C
Humi 45.0 %
```

- 画面は 3 ページあります。拡張ボードの**ユーザボタン** (D1 = GPIO3) を押すたびに、順に切り替わります (最後の次は、最初に戻ります)。ボタンを押すと、1 秒を待たずに、すぐに表示し直します。
- **ボタンを押さずに 10 秒たつと、OLED は消えます** (焼き付きと消費電力を減らすため。`CONFIG_THERMO_DISPLAY_TIMEOUT_SEC`。既定は `10`。`0` にすると消えません)。起動のとき (起動画面から) と、ボタンを押したときに点灯します。**消えている間にボタンを押すと、点灯するだけで、ページは切り替わりません。** 点灯してから、もう一度押すと切り替わります。点灯のために押したときは、ボタンの音も鳴りません。消えている間も、測定と BLE の通知は続きます。
  1. 現在値 (上の 4 行。日付、時刻、温度、湿度の順)
  2. 温度のグラフ: 1 行目に、履歴の最小と最大 (`19.7~25.1 C`) を出して、その下に折れ線を描きます。右端が最新です。縦軸は、最小の 5.0 下から、最大の 5.0 上までです。変化がなくても線が真ん中に見えて、小さな変化を拡大しすぎません。
  3. 湿度のグラフ (`45.0~52.0 %`)。縦軸は、温度と同じく、上下に 5.0 % の余白をとります。湿度を測れないセンサ (ADC) のときは、`No data` です。
- グラフの 1 点は、`CONFIG_THERMO_GRAPH_STEP_SEC` 秒 (既定は 170 秒) の測定値の平均です。128 点を覚えるので、既定では直近の約 6 時間を示します (30 にすると約 64 分)。測定値のなかった区切りには、点も線も描きません (点が 1 つだけのときは、点だけです)。

  **履歴の復元** (`CONFIG_THERMO_HISTORY_STORE`。SD カードと RTC と OLED が有効なときの既定は `y`): 測定値は、全て SD カードに保存してあります (下の「SD カード」)。起動のあと、RTC に時刻が入ったら、1 回だけ、直近の約 6 時間の記録 (送信済みも含む) から区切りごとの平均を作って、履歴に戻します。電源が切れていた間は、測定値のない区切りになり、グラフに空白ができます。RTC に時刻がない間と、SD カードがないときは、復元しません (そのときの履歴は RAM だけにあり、電源を入れ直すと消えます)。最初の区切りが終わったあとに時刻が入ったとき (履歴にすでに点があるとき) も、復元しません。
- 温度と湿度は、測定のたびに (5 秒ごと) 更新します。湿度を測れないとき (ADC のセンサ) は、`Humi --.- %` です。測定値がまだないときは、`Temp --.- C` と `Humi --.- %` です。
- 日付と時刻は、1 秒ごとに更新します。RTC (PCF8563) の時刻を読んで、ローカルタイムにします。RTC は UTC を保持します。ローカルタイムは、UTC に `CONFIG_THERMO_UTC_OFFSET_MIN` (既定は 540 分 = UTC+9。日本標準時) を足した値です。
- **RTC の時刻がまだ設定されていないとき** (最初の電源投入のときや、電池が切れたあと) は、`----/--/--` と `--:--:--` を表示します。時刻は、ゲートウェイから受け取ります (次の「ゲートウェイからの時刻の同期」)。届かないときは、ノードが WiFi で取ります (「WiFi での時刻の取得」)。
- 電源を切っても時刻を保つには、拡張ボードの RTC 用の電池 (ボタン電池) が要ります。電池の型は、ボードの資料で確かめてください。
- OLED の初期化に失敗したとき (OLED がない、接触不良など) は、ログに `Failed to initialize the OLED` を出して、表示だけをあきらめます。RTC の初期化に失敗したときも、`Failed to initialize the RTC` を出して、時刻の行を出しません。ボタンも同じです。どの場合も、測定と BLE の通知は続きます。
- 初期化に失敗した RTC、OLED、ボタンは、**60 秒ごとに初期化をやり直します。** 起動のときの一時的な失敗 (電源の立ち上がりや接触の瞬断) から、リセットなしで復帰するためです。使えるようになると、`RTC initialized`、`OLED initialized`、`Button initialized` のログが出ます。
- OLED と RTC の設定は、Zephyr の `seeed_xiao_expansion_board` のシールドと同じです (`app/thermo-node/boards/xiao_esp32c3.overlay`)。シールドは SD カードの SPI も有効にするので、シールドは使わずに、OLED と RTC のノードだけを書いています。
- RTC には、Zephyr の RTC ドライバ (v4.3.0 の `nxp,pcf8563`) ではなく、このアプリのドライバを使います (`app/thermo-node/drivers/rtc_pcf8563.c`。Devicetree の compatible は `thermo,pcf8563`。binding は `app/thermo-node/dts/bindings/rtc/thermo,pcf8563.yaml`)。Zephyr のドライバは、月と年の扱いがチップと合っていないためです (月は 0 から 11 で検証するので、12 月を設定できません。年は、1900 年からの年数をそのまま BCD にします)。このドライバが実装するのは、Zephyr の RTC API (`rtc_set_time()` と `rtc_get_time()`) だけです (アラームと割り込みはありません)。`node_time.c` は、この API で時刻を読み書きして、UNIX 時刻への換算と、ローカルタイムへの変換をします。

#### 起動画面と起動音

電源を入れたとき (リセットしたときも) に、OLED に約 3 秒の起動画面を出して、ブザーで短いメロディを鳴らします。

- **画面**: 画面の上に題名 (`THERMO NODE`) を出します。その下を、口を開け閉めする丸い絵が、ドットを食べながら左から右へ走ります。少し後ろから、敵の絵が追います。50 フレームを、60 ミリ秒ごとに表示します。
- **音**: 拡張ボードの**パッシブブザー** (D3 = GPIO5) を、PWM (LEDC のチャンネル 0) で鳴らします。周波数が音の高さで、デューティ比は 50% です。メロディ (約 2.6 秒) はシステムのワークキューで鳴らすので、BLE の開始や測定は待たされません。
- 起動画面は、測定のループに入る前の約 3 秒の間に動きます (BLE のアドバタイズは、すでに始まっています)。起動のときに OLED かブザーが使えなかったときは、その画面や音を出しません (あとで使えるようになっても出しません)。失敗しても、測定と通知は続きます。
- 画面と音は、ビルドのオプションで別々に外せます。画面は `CONFIG_THERMO_BOOT_SCREEN=n` で外します (`THERMO_DISPLAY` が `n` のときも、なくなります)。音は `CONFIG_THERMO_BUZZER=n` で外します (Docker では `THERMO_BUZZER=n docker compose run --rm build-node`)。

```bash
# West
west build -p always -b xiao_esp32c3 app/thermo-node -- -DCONFIG_THERMO_BOOT_SCREEN=n -DCONFIG_THERMO_BUZZER=n
```

**素材 (絵、題名、メロディ) の差し替え**

素材は、`app/thermo-node/src/boot_assets.c` が定義します (このアプリのオリジナルの絵と曲です。形式は `boot_assets.h`)。**ビルドのときに `app/thermo-node/private/src/boot_assets.c` があれば、そちらを使います。** 別のプライベートリポジトリをサブモジュールとして `app/thermo-node/private/` に置けば、公開のリポジトリに素材を入れずに、好きな絵と曲に差し替えられます。

```bash
# 例: プライベートリポジトリをサブモジュールとして置く (リポジトリの名前は例)
git submodule add git@github.com:<ユーザー>/thermo-private-assets.git app/thermo-node/private
git submodule update --init            # 別の場所で取り込むとき (権限のある人だけ)
```

- 差し替えるファイルでは、`boot_assets.h` で宣言したもの全てを、同じ名前と形で定義します (`boot_chomper` (3 つ)、`boot_chaser`、`boot_title` (12 文字まで。配列の大きさが合わないと、コンパイルエラー)、`boot_melody`、`boot_melody_count`)。サブモジュールがなければ (権限のない人や CI)、オリジナルの素材を使います。**サブモジュールなしでも、そのままビルドできます。**
- メロディは、周波数の数値ではなく、音名で書けます (`notes.h`。`boot_assets.h` が読み込みます)。`{NOTE_C5, 100}` のように、音名と長さ [ms] を並べます。`NOTE_C5` はオクターブ 5 のドです。半音は `NOTE_CS5` などで、フラットは同じ高さのシャープの名前で書きます。休符は `NOTE_REST` です。平均律で、A4 を 440 Hz としています。
- **著作権と商標に注意してください。** 既存のゲームの絵や曲は、その権利者の著作物と商標です。**このリポジトリ (公開) には入れません。** サブモジュールの URL (`.gitmodules`) は公開されますが、中身は、権限のある人にしか見えません。
- **素材を入れてビルドしたファームウェアの実行ファイル (`zephyr.elf`、`zephyr.bin`) を、公開しないでください** (GitHub Actions の成果物、リリースなど)。素材が、実行ファイルの中に入るためです。GitHub Actions の CI はサブモジュールを取り込まない (`actions/checkout` の既定) ので、オリジナルの素材でビルドします。
- 公開のリポジトリの履歴に一度でもコミットした素材は、あとで消しても履歴に残ります。公開のリポジトリには、コミットしないでください。

**ボタンを押したときの音**

ユーザーボタン (拡張ボードの D1。ページの切り替えに使います) を押すと、ブザーで短い音を鳴らします (`CONFIG_THERMO_BUTTON_SOUND`。既定は `y`)。前提は、`CONFIG_THERMO_BUZZER=y` と `CONFIG_THERMO_DISPLAY=y` です。ボタンを、OLED のページの切り替えに使うためです。

- 音は、`app/thermo-node/src/button_assets.c` が定義します (このアプリのオリジナルで、約 0.1 秒の上がる 2 音です。形式は `button_assets.h`)。
- **起動音と同じ仕組みで、差し替えられます。** ビルドのときに、**`app/thermo-node/private/src/button_assets.c` があれば、そちらを使います** (別のプライベートリポジトリを、サブモジュールとして置きます)。差し替えるファイルでは、`button_assets.h` で宣言した `button_melody` (音の表) と `button_melody_count` (音の数) を、同じ名前と形で定義します。音は、`{NOTE_A5, 40}` のように、音名 (`notes.h`) と長さ [ms] で書きます。ファイルがなければオリジナルの音を使うので、**サブモジュールなしでも、そのままビルドできます。**
- 押すたびに、鳴っている音 (起動のメロディなど) を止めて、最初から鳴らします。ブザーが使えないときや、鳴らせなかったときは、ログを出すだけで、ページは切り替えます。
- 著作権と商標の注意と、実行ファイルを公開しないことは、起動音の素材と同じです (上の注意を守ってください)。
- 外すとき: `-DCONFIG_THERMO_BUTTON_SOUND=n`。
- **実機で未確認**: 音の大きさ、押したときの反応の速さ、連打したときの音。
- 自分の機器に書き込んで個人で使う範囲かどうかは、権利者と各国の法律によります。

**確認の範囲**: 単体テストでは、メロディの順序、周波数から周期への換算、休符、再生の中断、PWM の失敗、起動画面の各フレーム (題名、ドット、絵の位置、はみ出しの切り取り、口の開き方、失敗)、起動の処理 (`main`) を確かめています。なお、パッシブ型のブザーは、音が小さいことがあります。

### ゲートウェイからの時刻の同期

ノードは、ふだんは WiFi を使いません。ゲートウェイが SNTP で得た時刻を BLE で受け取って、RTC に設定します。BLE で時刻が届かないときだけ、WiFi で取ります (次の「WiFi での時刻の取得」)。

1. ゲートウェイは、ノードに接続して、Thermo サービスの中から時刻の特性 (UUID の末尾が `9F3C1A02-...`。書き込み専用) を探します。
2. 見つけたら、UTC の UNIX 時刻 (uint32、リトルエンディアン、4 バイト) を書き込みます。ノードは、それを RTC に設定します (範囲は 2000 年から 2099 年)。
3. ゲートウェイが SNTP でまだ同期していないときは、書き込みを見送って、10 秒ごとに確かめます。同期できたら書き込みます。そのため、ゲートウェイが WiFi につながるまで、ノードの時刻は未設定のままです。
4. その後も 1 時間ごとに書き込み直して、RTC のずれを直します。ノードが拒否したときは、60 秒後にやり直します。
5. ノードに時刻の特性がないとき (古いファームウェアや、`CONFIG_THERMO_RTC=n` のとき) は、ゲートウェイがログに警告を出すだけで、温度の受信は続きます。クラウドなし (`native_sim`) でビルドしたゲートウェイは、時刻を書き込みません。
6. 時刻の特性は認証をしないので、近くの BLE 機器なら誰でも書き込めます (影響するのは、表示する時刻だけです)。書き込む時刻の誤差は、1 秒ほどです (BLE の通信の遅れ)。

### WiFi での時刻の取得 (BLE の予備)

ノードは、起動の 1 分後から 1 分ごとに、RTC の時刻を確かめます。ゲートウェイから時刻が届かず、RTC に時刻がなければ、WiFi に接続して SNTP で時刻を取り、RTC に設定して、すぐに WiFi を切ります。WiFi での取得は、最大 5 回まで試します (`CONFIG_THERMO_WIFI_TIME`。既定は `y`。`CONFIG_THERMO_RTC=y` が前提)。

1. ノードは、起動の 1 分後から 1 分ごとに、RTC に時刻があるかを確かめます。その間に BLE でゲートウェイから時刻が届けば、**WiFi で取る必要はないので、何もせずに確認をやめます。**
2. 時刻がなければ、WiFi につないで、SNTP (`CONFIG_THERMO_NTP_SERVER`。既定は `pool.ntp.org`) で UTC の時刻を取り、RTC に設定して、WiFi を切ります。取れたら、確認をやめます。
3. 失敗したとき (SSID の未設定、接続の失敗、SNTP の応答なし) は、ログに出して、次の確認 (1 分後) でやり直します。**5 回試して取れなかったら、やめます** (その後に BLE で時刻が届けば、RTC に設定されます)。WiFi は、つなぎっぱなしにしません。
4. 取っている間 (接続に最大 20 秒、SNTP に最大 5 秒) は、メインのループが止まるので、温度の測定と OLED の更新も止まります。RTC が使えないときや、WiFi の初期化に失敗したときは、使えるようになるまで、回数に数えずに待ちます。

SSID とパスワードは、フラッシュ (settings の NVS) に保存します。シェルに直接入力したり貼り付けたりすると、シェルの受信バッファ (64 バイト) があふれて、文字が欠けたり固まったりします。そのため、**スクリプト `scripts/send-wifi.sh` で、少しずつ送ります** (ゲートウェイの `scripts/send-cred.sh` と同じ考え方です)。ポートを開いたままの `picocom` などは、先に終わらせます。

```bash
scripts/send-wifi.sh <SSID> <パスワード> [ポート]
scripts/send-wifi.sh "my home ap" - /dev/ttyACM0      # パスワードを - にすると画面に出さずに入力を求める
scripts/send-wifi.sh home-ap "" /dev/ttyACM0          # パスワードが空: オープンネットワーク
SYNC=1 scripts/send-wifi.sh home-ap - /dev/ttyACM0    # 送ったあと, 時刻の取得 (thermo sync) も試す (約 30 秒)
```

スクリプトは、`thermo set ssid`、`thermo set psk`、`thermo show` を順に送ります (ポートの既定は `/dev/ttyACM0`)。最後に `ssid:` の後ろに送った SSID が表示されれば、設定できています。WiFi での直接の送信 (下の節) に使うエンドポイントと証明書は、ゲートウェイ用の `scripts/send-cred.sh` で、同じように送れます。

```text
thermo set ssid <SSID>
thermo set psk <パスワード>    (オープンネットワークでは設定しない)
thermo set endpoint <ホスト名>  (AWS IoT Core のエンドポイント. 直接の送信に使う. 下の節)
thermo set client_id <ID>      (MQTT のクライアント ID. 直接の送信に使う. 下の節)
thermo show                    (設定と証明書の状態を表示する. パスワードは隠す)
thermo save-certs              (cred add で登録した証明書をフラッシュに保存する. 直接の送信に使う. scripts/send-cred.sh はこれを自動で実行する. 保存しないとリセットや書き込みで消える)
thermo sync                    (WiFi で時刻をすぐに取って RTC に設定する. 1 分待たずに試せる. 試みの回数には数えない)
thermo sd                      (SD カードが使えるかを確かめる. 使えなければマウントをやり直す. 未送信の数を表示する)
thermo reset                   (設定と証明書を消す)
```

- `thermo sync` を使えば、1 分待たずに、WiFi での取得を確かめられます。対応する WiFi は 2.4 GHz だけです (ESP32-C3)。
- 無効にするとき: `-DCONFIG_THERMO_WIFI_TIME=n`。外れるのは、このアプリの WiFi のコードだけで、FLASH は約 68 KB 減って 745 KB になるだけです。`prj.conf` が WiFi とネットワークを有効にしているためで、完全に外すには、`prj.conf` のその部分も外します。
- **未確認**: 長時間の動作で、BLE の広告と DHT11 の読み取りが WiFi と同時に動き続けることと、スタックとヒープの余裕。FLASH は 813 KB です (従来は 445 KB)。WiFi と SNTP をメインスレッドで呼ぶので、メインスレッドのスタックは 6144 バイトにしています。足りないときは、`prj.conf` で増やします。

OLED と RTC は、ビルドのオプションで別々に外せます。Devicetree の alias (`thermo-display`、`thermo-rtc`) があるとき (`xiao_esp32c3`) の既定は、どちらも `y` です。

- `CONFIG_THERMO_DISPLAY=n`: OLED、CFB、表示のコード、グラフの履歴、ボタンがビルドから外れます。
- `CONFIG_THERMO_RTC=n`: RTC と時刻のコードがビルドから外れます (OLED には、日付と時刻の行が出ません)。

```bash
# Docker: 環境変数 THERMO_DISPLAY と THERMO_RTC を n にしてビルドする (既定は y)
THERMO_DISPLAY=n docker compose run --rm build-node
THERMO_RTC=n docker compose run --rm build-node

# West
west build -p always -b xiao_esp32c3 app/thermo-node -- -DCONFIG_THERMO_DISPLAY=n -DCONFIG_THERMO_RTC=n
```

### ゲートウェイにつながらないときの、WiFi での直接の送信と、SD カードへの保存

ゲートウェイが止まっているなどで BLE でつながらないときは、ノードが WiFi につないで、AWS IoT Core に MQTT (TLS) で直接送ります。全ての測定値は、送信済みフラグつきで SD カードに保存します。BLE も WiFi も使えない間の測定値は、未送信のまま保存し続けて、つながったときに、測った時刻のまま古い順に送ります。設定は `CONFIG_THERMO_DIRECT_MQTT` (既定は `y`。`CONFIG_THERMO_WIFI_TIME=y` が前提) と、SD カードの `CONFIG_THERMO_BACKLOG` (SD カードの Devicetree のノード `sdhc0` があれば、既定は `y`) です。

**動き**

1. ゲートウェイにつながっていない測定 (5 秒ごと) が **5 回続いたら** (約 25 秒)、WiFi と MQTT を動かして、未送信の記録を AWS IoT Core に送ります (次の 5 と 6)。SD カードがないときは、測定値を 5 秒ごとにそのまま送ります。トピックは `thermo/<クライアント ID>/<ノードのアドレス>/temperature` で、ゲートウェイが送る形と同じなので、DynamoDB のルールと `thermon` をそのまま使えます。
2. ゲートウェイにつながったら、直接の送信を止めて、WiFi を切ります (ゲートウェイが送るので、重複させません)。
3. WiFi か MQTT の接続が **5 回続けて失敗したら、あきらめて** WiFi を切ります。ゲートウェイにつながるまで、やり直しません。
4. 直接の送信を動かしている間は、WiFi を時刻の取得 (上の節) には使いません。
5. **SD カードに保存する**: 全ての測定値 (5 秒ごと) を、SD カードに保存します (測った UNIX 時刻、温度、湿度、気圧、送信済みフラグ。1 件 16 バイト)。**ゲートウェイにつながっている間の測定値は、送信済み**で保存します (ゲートウェイが送るため)。**つながっていない間は、未送信**で保存します。BLE にも WiFi にもつながっていなくても、保存し続けます (上限を超えたら、古い記録から上書きします)。RTC に時刻がないときは、時刻が要るので、保存できません。
6. **未送信の記録を送る**: SD カードに未送信の記録があれば、ゲートウェイにつながっていても、WiFi と MQTT を動かして、測った時刻 (`timestamp`) のまま**古い順に**送ります。publish できた記録は、フラグを送信済みに書き換えます。全て送り終えたとき、ゲートウェイにつながっていれば、WiFi を切ります。

**設定** (ゲートウェイと同じ。`thermo` コマンド)

```text
thermo set ssid <SSID>            (スマートフォンのテザリングなら 2.4 GHz にする. ESP32-C3 は 5 GHz に対応しない)
thermo set psk <パスワード>
thermo set endpoint <AWS IoT のエンドポイント>
thermo set client_id <クライアント ID>    (ノードごとに別の ID にする. ゲートウェイとも重ねない. 例: node-01)
cred add ...                      (CA 証明書, デバイス証明書, 秘密鍵. 送り方はゲートウェイと同じ. ただし, CA 以外はノード専用のものを使う. docs/aws_setup.md の 7 章)
thermo save-certs                 (scripts/send-cred.sh が自動で実行する. 手で cred add したときだけ必要)
thermo show                       (ready to connect: yes になれば揃っている)
```

**スマートフォンのテザリングに切り替える** (元の設定を残して、戻せる): `thermo tether <SSID> [パスワード]` は、今の SSID とパスワードをフラッシュに保存してから (1 項目。`tether_bak`)、テザリングの SSID とパスワードを設定します。続けて、`thermo apply` で反映します。すでに切り替えているときにもう一度実行しても、最初に保存した元の設定は上書きしません。`thermo restore` は、元の SSID とパスワードに戻して、保存した元の設定を消します (続けて `thermo apply`)。元の SSID は、`thermo show` の `tether backup:` に出ます (なければ `(none)`)。パスワードは、基板の外に出しません。Android アプリ [thermo-term](https://github.com/sluchin/thermo-term) は、USB ケーブル (Type-C) で、この 2 つのコマンドを送ります。

文字が欠けないように、`scripts/send-cred.sh` と `scripts/send-wifi.sh` で少しずつ送ります。**証明書、秘密鍵、ポリシー、クライアント ID は、ノードごとに別のものを作ります** (ゲートウェイとも共有しません)。1 台が漏れても、その 1 台だけを無効にできます。クライアント ID が重なると、AWS IoT Core が先の接続を切るので、接続の奪い合いになります。共有してよいのは、ルート CA とエンドポイントだけです。作り方は、[aws_setup.md](aws_setup.md) の 7 章にあります。

**SD カード** (拡張ボードの SD カードスロット。SPI。CS は D2)

- **FAT でフォーマットした**カードを差します (32 GB 以下の SDHC を勧めます)。Devicetree は、Zephyr の `seeed_xiao_expansion_board` と同じです (`app/thermo-node/boards/xiao_esp32c3.overlay`)。
- ファイルは `/SD:/backlog.bin` の 1 つです。1 件は 16 バイトで、測った UNIX 時刻 (4 バイト)、温度、湿度、**送信済みフラグ** (1 バイト)、気圧、CRC を持ちます。送り終えたら、フラグの 1 バイトだけを書き換えます。気圧を足す前の記録 (気圧なし) も、そのまま読めるので、SD カードを消さずに済みます。
- 記録は、`CONFIG_THERMO_BACKLOG_MAX_RECORDS` 件 (既定 100000 件 = 約 1.6 MB。5 秒ごとの測定で約 5.8 日分) の輪に入れます。いっぱいになったら、最も古い記録から上書きします。古い記録は送信済みのことが多いので、ふつうは未送信の記録が残ります (未送信が上限より多いと、古い未送信の記録も上書きされます)。
- 起動のときに、全ての記録を 1 回読みます (既定で数秒)。測定のたびにファイルに書くので、SD カードへの書き込みが増えます。
- カードがない (マウントできない) ときは、ログに `No SD card` を出して、保存しません (ほかの機能は動きます)。電源が切れたときの書きかけの記録は、起動のときに捨てます。壊れた記録は、送らずに捨てます。
- ファイルの中身は、シェルの `fs ls /SD:` などで見られます。`fs rm` と `fs write` も使えるので、書き込みの最中には実行しないでください。
- 使わないとき: `-DCONFIG_THERMO_BACKLOG=n`。

**制限と、未確認のこと**

- **実機で確認したこと**: BLE と WiFi と TLS を同時に動かして、ノード専用の証明書とクライアント ID での直接の送信が、AWS IoT Core に届くこと。
- **実機でまだ確認していないこと**: ゲートウェイが戻ったときの停止、SD カードの読み書き (配線、フォーマット、抜き差し、電源断)、あとからの送信。単体テストは、FFF のモックと RAM ディスクの FatFS で、動作の流れだけを確かめています。
- **RAM の余裕が小さい**: SD カードと TLS を加えたので、静的な RAM (`dram0`) の空きは約 1.5 KB です。そのため、`prj.conf` で、ネットワークのバッファ (`CONFIG_NET_BUF_*_COUNT` と `CONFIG_NET_PKT_*_COUNT`) と、FatFS が同時に開くファイルの数 (`CONFIG_FS_FATFS_NUM_FILES`) を減らしています。機能を足すと、`region dram0_0_seg overflowed` でビルドが失敗します。そのときは、`CONFIG_MBEDTLS_HEAP_SIZE` など、ほかの設定を見直します。
- **BLE は、1 台にしかつながりません** (`CONFIG_BT_MAX_CONN=1`)。2 にすると、BLE が使うヒープが増えて WiFi と TLS のぶんが足りなくなり、直接の送信が届かなくなります (実機で確認)。スマートフォンのアプリ [thermo-term](https://github.com/sluchin/thermo-term) の BLE のタブで温度と湿度を見るときは、ゲートウェイを止めます。スマートフォンがつないでいる間は、ノードはゲートウェイにつながっているとみなして、直接の送信をしません。
- 「送り終えた」は、publish の成功で判断します (PUBACK は待ちません)。そのため、接続が切れる直前の値は、AWS に届かずに失われることがあります。
- WiFi の設定がなく BLE だけを使うときに、SD カードの記録をゲートウェイ経由で送る機能は、まだありません ([TODO.md](../TODO.md))。

### DHT11 の読み取りの失敗と、割り込みを止める設定

DHT11 の読み取りでは、マイクロ秒単位のタイミングで 1 本の線を読みます。この間に BLE などの割り込みが入ると、`Could not fetch the sensor (-5)` がときどき出ます。Zephyr のドライバは、1 つの信号が 100 マイクロ秒を超えると、何も表示せずに `-EIO` を返します (デバッグビルドでも、`Invalid checksum` はデータが乱れたときだけ出ます)。

そのため、**DHT11 のときは、読み取りの間 (約 22 ミリ秒) だけ割り込みを止める設定 (`CONFIG_DHT_LOCK_IRQS=y`) を、既定で有効にしています** (`app/thermo-node/sensors/dht11.conf`)。この設定で、読み取りのエラーは出なくなります。

- 割り込みを止めている間 (5 秒に 1 回、約 22 ミリ秒) は、BLE の処理も止まります。ゲートウェイとの接続が切れやすくなっていないか (`Disconnected` が増えていないか) を、実機で確かめてください。
- 無効にして試すときは、次のようにビルドします。

```bash
# Docker: 環境変数 THERMO_DHT_LOCK_IRQS を n にしてビルドする (既定は y)
THERMO_SENSOR=dht11 THERMO_DHT_LOCK_IRQS=n docker compose run --rm build-node

# West
west build -p always -b xiao_esp32c3 app/thermo-node -- -DEXTRA_DTC_OVERLAY_FILE=sensors/dht11.overlay -DEXTRA_CONF_FILE=sensors/dht11.conf -DCONFIG_DHT_LOCK_IRQS=n
```

## シリアルモニター

```bash
# picocom (推奨)
picocom -b 115200 /dev/ttyACM0

# screen
screen /dev/ttyACM0 115200

# minicom
minicom -D /dev/ttyACM0 -b 115200
```

### 起動のログを見る (リセットのたびに、つなぎ直す)

XIAO ESP32C3 の USB は、リセットや書き込みのたびに一度切れて、またつながります。そのため picocom はリセットのときに終了してしまい、起動のログ (初期化の成功と失敗) を見逃します。`cat` で読み取るだけにして、つなぎ直しを自動で繰り返せば、起動のログが見えます。

```bash
while true; do [ -c /dev/ttyACM0 ] && stty -F /dev/ttyACM0 115200 raw -echo && cat /dev/ttyACM0; sleep 0.3; done
```

- 読み取りだけなので、シェルのコマンドは打てません。コマンドを打つときは、`picocom -b 115200 /dev/ttyACM0` を使います。
- `picocom` のループ (`--noinit --noreset`) は使いません。Zephyr のシェルはカーソル位置を問い合わせます (`ESC[6n`)。端末がこれに応答し、その応答がデバイスに送られ続けて、`RX ring buffer full` と `^[` が連続することがあるためです (原因は未確認です)。
- `/dev/ttyACM0` を読めないときは、ユーザーを `dialout` グループに入れます (`sudo usermod -aG dialout $USER`。ログインし直す必要があります)。
- 終了するときは、Ctrl-C です。
- 2 台 (ノードとゲートウェイ) を同時につなぐときは、`/dev/ttyACM0` と `/dev/ttyACM1` を別の端末で開きます。

### ファイルシステムのシェル (Thermo ノード)

Thermo ノードのシェルには、ファイルシステムのコマンド `fs` があります (`CONFIG_FILE_SYSTEM_SHELL=y`。フラッシュが約 1.7 KB、RAM が約 160 B 増えます)。SD カードは、起動のときに `/SD:` にマウントします。picocom から、ファイルの一覧や中身を確かめられます (`fs` だけを打つと、コマンドの一覧が出ます)。

- 主なコマンド: `fs ls /SD:`、`fs statvfs /SD:`、`fs read /SD:/backlog.bin <サイズ> <オフセット>` (16 進で表示)、`fs cat <ファイル>`、`fs cd`、`fs pwd`。`fs mount` と `fs hexdump` はありません。
- `fs ls /SD:` が `mount point not found` になるときは、起動のときにマウントできていません。**`thermo sd`** で、SD カードが使えるかを確かめます。使えなければ、マウントをやり直します (カードを挿し直したあとも、再起動せずに使えます)。失敗の理由は、続けて出るログ (`Could not mount the SD card (err ...)`) にあります。使えているときは、マウントし直さずに、未送信の数を表示します。
- **`fs rm` と `fs write` も使えます。** USB のシリアルにつないだ人は、ファイルを消したり書き換えたりできます。ログを書いている最中には、実行しないでください (FAT が壊れるおそれがあります)。
- 大きなファイルに `fs cat` を実行すると、画面に大量に流れます。
- ノードのログも、テキストで SD カードに保存します。`/SD:/boot.log` は起動から 60 秒の全てのログ、`/SD:/error.log` はエラーのログです (`fs cat /SD:/error.log` で読めます)。
- 記録の形式と読み方 (16 進のダンプの読み方と、PC で読む Python) は、[sd_log.md](sd_log.md) にあります。実機の試験項目は、[test_integration.md](test_integration.md) にあります。

## CI/CD（GitHub Actions）

このリポジトリでは、GitHub Actions が、ビルド、単体テスト、静的解析などを自動で実行します。

### ワークフロー

`.github/workflows/build.yml` は、main と `ccr-*` のブランチへの push と、main への PR で動きます。ローカルと同じ Docker イメージ (Zephyr SDK 0.17.4) で、`docker compose` のサービスを実行します。

| ジョブ | 実行するサービス | 内容 |
|:---|:---|:---|
| `build` | `build-node` / `build-gateway` / `build-node-sim` / `build-gateway-sim` | ビルド。警告が出たら失敗 |
| `test` | `test-node` / `test-gateway` / `test-common` | 単体テスト (Ztest + FFF, `native_sim`) |
| `coverage` | `coverage-node` / `coverage-gateway` / `coverage-common` | 単体テストのカバレッジ (行と分岐。100% 未満なら失敗) |
| `docs` | `docs` | ドキュメント (Doxygen)。警告があれば失敗。生成物 (`build/docs/`) は、アーティファクトに保存 |
| `analyze` | `analyze-node` / `analyze-gateway` | 静的解析 (gcc `-fanalyzer`)。指摘があれば失敗 |
| `lint` | `format` / `lint` / `whitespace` | 整形の確認 (clang-format)、Zephyr の `checkpatch.pl`、行末の空白の確認。指摘があれば失敗 |

Docker イメージのレイヤーは、GitHub Actions のキャッシュに保存します (`.github/actions/docker-image`)。イメージを作り直すのは、`Dockerfile` か `west.yml` を変えたときだけです。

CI と同じ確認は、ローカルでも実行できます。短い名前のサービス (`build` `test` `coverage` `analyze`) は、node と gateway の両方をまとめて実行します。`format` `format-fix` `lint` `whitespace` `docs` は、名前にアプリが付かず、両方を対象にします。`clean` は、`build/` (`build/docs/` も含む) を消します。CI は、個別のサービス (`test-node` など) を使います。

```bash
# node と gateway と common の全て (analyze は node と gateway)
docker compose run --rm test
docker compose run --rm coverage
docker compose run --rm analyze

# 個別
docker compose run --rm test-node
docker compose run --rm test-gateway
# 共有のコード (app/common)
docker compose run --rm test-common
# カバレッジ (行と分岐が 100% でなければ失敗する)
docker compose run --rm coverage-node
docker compose run --rm coverage-gateway
docker compose run --rm coverage-common
# ドキュメント (Doxygen. build/docs/index.html を開く. 警告があれば失敗する)
docker compose run --rm docs
docker compose run --rm analyze-node
docker compose run --rm analyze-gateway
docker compose run --rm format
docker compose run --rm lint
docker compose run --rm whitespace
```

コーディングスタイルは、Zephyr の規約に合わせています。違う点は、[coding_style.md](coding_style.md) を参照してください。

### ビルドアーティファクト

`build` ジョブは、成功しても失敗しても、次のファイルをアップロードします。

- `zephyr.elf` - デバッグ情報付き実行ファイル
- `zephyr.bin` - バイナリ (実機用)
- `zephyr.exe` - シミュレーション用の実行ファイル (`native_sim`)

アーティファクトの名前は、小文字のスネークケースです (サービス名の `-` を `_` にします)。

| 名前 | 内容 |
|:---|:---|
| `build_node` `build_gateway` `build_node_sim` `build_gateway_sim` | 上のビルド成果物と `build.log` |
| `test_node_log` `test_gateway_log` | 単体テストのログ |
| `coverage_node_log` `coverage_gateway_log` | カバレッジのログ |
| `analyze_node_log` `analyze_gateway_log` | 静的解析のログ |
| `format_thermo_log` `lint_thermo_log` `whitespace_thermo_log` | 整形、lint、行末の空白の確認のログ |
| `docs` `docs_log` | Doxygen のドキュメントと、生成のログ |

## トラブルシューティング

### Docker コマンド実行時に permission denied エラーが発生する

Linux で `docker` コマンドを実行したときに、`Got permission denied while trying to connect to the Docker daemon socket` が表示される場合の対処です。

```bash
# ユーザーを docker グループに追加
sudo usermod -aG docker $USER

# グループの変更を即時反映 (または一度ログアウトして再ログイン)
newgrp docker
```

### West コマンドが見つからない

```bash
source venv/bin/activate
pip install west
```

### Zephyr SDK が見つからない

```bash
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-0.17.4
```

### USB デバイスがない

```bash
# Linux では udev のルールを設定する
sudo usermod -a -G dialout $USER
sudo usermod -a -G uucp $USER
# ログアウトして, もう一度ログインする
```

### ビルドエラー

```bash
# キャッシュをクリア
west build -p always -b xiao_esp32c3 app/thermo-node
```

## リソース

- [Zephyr 公式ドキュメント](https://docs.zephyrproject.org/)
- [ESP32C3 ボード情報](https://docs.zephyrproject.org/latest/boards/seeed/xiao_esp32c3/doc/index.html)
- [Bluetooth Low Energy ガイド](https://docs.zephyrproject.org/latest/connectivity/bluetooth/index.html)

## サポート

問題が起きたときは、GitHub Issues で報告してください。
