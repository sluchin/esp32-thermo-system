# Zephyr OS Development Environment Setup

このドキュメントは、ESP32C3上でZephyr RTOSベースのサーモシステムを開発するための環境構築手順です。

## Docker を使用したセットアップ（推奨）

### 1. Docker のインストール

まだ Docker がインストールされていない場合は、環境に合わせて以下の手順でインストールしてください。

#### Linux (Ubuntu / Debian)

公式リポジトリから Docker Engine および Docker Compose プラグインをインストールします：

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

# 現在のユーザーを docker グループに追加（sudo なしで実行可能にする）
sudo usermod -aG docker $USER
newgrp docker
```

#### macOS

1. [Docker Desktop for Mac](https://docs.docker.com/desktop/setup/install/mac-install/) をダウンロードしてインストールします（Apple Silicon / Intel に対応）。
2. インストール後、Docker Desktop アプリケーションを起動してください。

#### Windows

1. WSL2 (Windows Subsystem for Linux 2) を有効化します。
2. [Docker Desktop for Windows](https://docs.docker.com/desktop/setup/install/windows-install/) をダウンロードしてインストールします。
3. 設定で「Use the WSL 2 based engine」が有効になっていることを確認してください。

---

### 2. ホストのユーザー ID を環境変数にエクスポートする

コンテナはホストのユーザーで実行されます（`docker-compose.yml` の `user: "${UID:-0}:${GID:-0}"`）。これにより、`build/` などの生成物が root 所有になりません。

`UID` と `GID` は、シェルの変数であって環境変数ではないので、`docker compose` に渡すには、シェルの設定ファイルでエクスポートします。未設定の場合は root（0:0）で実行され、`build/` などの生成物が root 所有になります。

zsh の場合（`~/.zshrc`）:

```bash
export UID
export GID
```

bash の場合（`~/.bashrc`）:

```bash
export UID
export GID=$(id -g)
```

bash の `UID` は読み取り専用のため、`export UID=$(id -u)` とは書けません（`export UID` だけで足ります）。bash には `GID` 変数がないので、`id -g` で設定します。

設定後、新しいシェルを開くか、`source ~/.zshrc`（または `source ~/.bashrc`）を実行してください。確認は次のコマンドです。

```bash
docker compose run --rm dev id
```

自分のユーザー ID とグループ ID が表示されれば成功です。

### 3. Docker を使用したビルドと開発

Docker と Docker Compose がインストールされている場合、以下のコマンドで開発環境のビルドやシェルを実行できます（Docker Compose V2 の `docker compose` コマンドを使用します）。

初回は、Zephyr のソースと依存ツールを含むイメージをビルドします（時間がかかります）。`west.yml` を変更した場合も、再実行してください。

```bash
docker compose build
```

```bash
# Thermo Node をビルド（ESP32C3用）
docker compose run --rm build-thermo-node

# Thermo Gateway をビルド（ESP32C3用）
docker compose run --rm build-thermo-gateway

# Thermo Node をビルド（シミュレーション用）
docker compose run --rm build-thermo-node-sim

# Thermo Gateway をビルド（シミュレーション用）
docker compose run --rm build-thermo-gateway-sim

# インタラクティブ開発シェル
docker compose run --rm dev

# 生成物 (build/ と docs/) を全て消す (次のビルドは、最初からやり直す)
docker compose run --rm clean
```

### Docker セットアップの利点

- ✅ Zephyr SDK の自動インストール
- ✅ 依存関係をすべて含む
- ✅ クロスプラットフォーム対応（Linux/macOS/Windows）
- ✅ ホストシステムを汚さない

詳細は `Dockerfile` と `docker-compose.yml` を参照してください。

---

## ローカル開発環境セットアップ

Docker を使用しない場合のセットアップ手順です。

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
# Zephyr SDK をダウンロード（Linux x86_64 の場合）
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.17.4/zephyr-sdk-0.17.4_linux-x86_64.tar.xz

# インストール
tar xf zephyr-sdk-0.17.4_linux-x86_64.tar.xz -C ~/
~/zephyr-sdk-0.17.4/setup.sh

# インストール後、ホストツールをセットアップ
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
   ls /dev/ttyACM*  # Linux (XIAO ESP32C3 は、USB 内蔵なので ttyACM)
   ls /dev/tty.usbmodem*  # macOS
   ```

   シリアルポートを使うには、`dialout` グループに入る (入れたあとは、ログインし直す)。
   ```bash
   sudo usermod -aG dialout $USER
   ```

3. ファームウェアをフラッシング
   ```bash
   # Thermo Node
   esptool -p /dev/ttyACM0 write-flash 0x0 build/thermo-node/zephyr/zephyr.bin

   # Thermo Gateway
   esptool -p /dev/ttyACM0 write-flash 0x0 build/thermo-gateway/zephyr/zephyr.bin

   # スクリプト (TARGET は node / gateway。2 台つなぐときは、台ごとに PORT を指定する)
   scripts/flash.sh gateway /dev/ttyACM0
   ```

## Thermo ノードのセンサ

Thermo ノードは、温度と湿度を、センサから読みます。どのセンサを使うかは、**Devicetree** で決めます (コードは、変えません)。

| 優先 | Devicetree の設定 | センサ |
|:---|:---|:---|
| 1 | alias `thermo-sensor` のセンサのノードがある | Zephyr のセンサ API で読む。DHT11 など。温度と湿度 |
| 2 | `zephyr,user` に `io-channels` がある | ADC を読んで、温度に換算する (アナログの温度センサ。湿度はなし) |
| 3 | どちらもない (`native_sim` など) | 乱数のシミュレーション値 |

### DHT11 の配線 (既定)

3 ピンの DHT11 モジュール (基板の印字: `+` `out` `-`。ピンを下にして見て、左から) を、XIAO ESP32C3 の、次のピンにつなぎます。

| DHT11 モジュール | 役割 | XIAO ESP32C3 |
|:---|:---|:---|
| `+` | 電源 | `3V3` (3.3 V) |
| `out` | データ (信号線) | `D7` (GPIO20) |
| `-` | グランド | `GND` |

- 基板に、プルアップ抵抗が、付いているので、追加の部品は、要りません。
- 設定は、`app/thermo-node/boards/xiao_esp32c3.overlay` です (`dio-gpios = <&xiao_d 7 ...>`)。別のピンにつなぐときは、この `7` (D7) を、変えます。
- データのピンに `D7` を選んだ理由: `D6` (GPIO21) は、起動のときに、ROM のログが出るので、センサの信号に、ノイズが乗ります。`D8` (GPIO8) と `D9` (GPIO9) は、起動のときの、設定 (ストラッピング) に、使うピンです。
- DHT11 は、温度 0〜50 ℃ (1 ℃ きざみ、誤差 ±2 ℃)、湿度 20〜95 % (1 % きざみ、誤差 ±5 %) です。読み取りは、1 秒以上、あけます (ノードは、5 秒ごと)。
- DHT22 (AM2303) を使うときは、overlay の `dht11` のノードに、`dht22;` を足します。

#### Seeed Studio XIAO 拡張ボード (103030356) を使う場合

1. XIAO ESP32C3 を、拡張ボードの、上側のソケットに、差します。**USB-C のコネクタが、ボードの上の辺 (OLED の反対側。右のソケットの印字 `VBUS GND 3V3 10 9 8 7` の `VBUS` が、ある側) に、向く**ようにします。向きを、間違えると、故障の原因になります。XIAO の裏の印字 (`5V` `GND` `3V3` `D10` ...) が、ボードの印字と、合っているか、確認してください。
2. ジャンパワイヤ (両端がメス) で、DHT11 の足と、ボードの**下側の、オスのピンのヘッダ** (黒い 2 列 × 4 本) を、つなぎます。上の列は、左から `GND` `3V3` `SWDIO` `SWCLK`、下の列は、左から `GND` `5V` `TX'6` `RX'7` と、印字されています。

   | DHT11 | ボードの下側のヘッダ |
   |:---|:---|
   | `+` | 上の列の `3V3` (左から 2 番目) |
   | `out` | 下の列の `RX'7` (右端。D7 = GPIO20) |
   | `-` | 上の列の `GND` (左端)。下の列の `GND` (左端) でも、同じ |

3. 上側のソケット (メス) には、メスのワイヤは、つなげません。メスのワイヤしかないときは、下側のオスのヘッダを、使います。
4. `5V` には、つながないでください (DHT11 は、3.3 V でも動きます。XIAO ESP32C3 の信号は、3.3 V なので、5 V につなぐと、信号が、3.3 V を超えるおそれがあります)。

### ADC のアナログセンサ (LM35 など) に替える

1. `app/thermo-node/boards/xiao_esp32c3.overlay` の、`aliases` の `thermo-sensor` と、`dht11` のノードを、消します。
2. `zephyr,user` に、`io-channels` と、ADC のチャンネルの設定を、書きます (例は、`app/thermo-node/tests/sensor/adc.overlay`)。
3. `prj.conf` に、温度への換算を、書きます。
   - `CONFIG_THERMO_ADC_MV_PER_DEG`: 1 ℃ あたりの電圧 [mV] (既定 10。LM35 の値)
   - `CONFIG_THERMO_ADC_OFFSET_MV`: 0 ℃ のときの電圧 [mV] (既定 0。LM35 の値)
4. ビルドディレクトリを、消して、ビルドし直します (`rm -rf build/thermo-node`。overlay の追加は、既存のビルドでは、反映されません)。

### OLED の表示と時刻 (拡張ボード)

拡張ボードの OLED (SSD1306、128 x 64 ドット) に、温度、湿度、日付、時刻と、温度と湿度のグラフを表示します。ノードの、XIAO ESP32C3 を、拡張ボードに差せば、配線は、要りません (OLED と RTC は、ボードの I2C。D4 が SDA、D5 が SCL。OLED のアドレスは `0x3C`、RTC のアドレスは `0x51`)。

```
2026-10-08
14:05:09
Temp 23.5 C
Humi 45.0 %
```

- 画面は 3 ページあり、拡張ボードの**ユーザボタン** (D1 = GPIO3) を押すたびに、順に切り替わります (最後の次は、最初に戻ります)。ボタンを押すと、1 秒を待たずに、すぐに表示し直します。
  1. 現在値 (上の 4 行。日付、時刻、温度、湿度の順)
  2. 温度のグラフ: 1 行目に、履歴の最小と最大 (`19.7~25.1 C`)、その下に、折れ線を描きます。右端が、最新です。縦軸は、最小から最大で、変化が 1.0 ℃ 未満のときは、幅を 1.0 ℃ にします (小さな変化を、拡大しすぎないため)。
  3. 湿度のグラフ (`45.0~52.0 %`)。湿度を測れないセンサ (ADC) のときは、`No data` です。
- グラフの 1 点は、`CONFIG_THERMO_GRAPH_STEP_S` 秒 (既定は 30 秒) の測定値の平均です。128 点を覚えるので、既定では、直近の約 64 分です (60 にすると、約 2 時間 8 分)。電源を入れ直すと、履歴は消えます。
- 温度と湿度は、測定のたびに (5 秒ごと) 更新します。湿度を測れないとき (ADC のセンサ) は、`Humi --.- %` です。測定値がまだないときは、`Temp --.- C` と `Humi --.- %` です。
- 日付と時刻は、1 秒ごとに、更新します。RTC (PCF8563) の時刻を読んで、ローカルタイムにします。RTC は UTC を保持します。ローカルタイムは、UTC に `CONFIG_THERMO_UTC_OFFSET_MIN` (既定は 540 分 = UTC+9。日本標準時) を足した値です。
- **RTC の時刻が、まだ設定されていないとき** (最初の電源投入、または、電池が切れたあと) は、`----/--/--` と `--:--:--` を表示します。時刻は、ゲートウェイから受け取ります (次の「ゲートウェイからの時刻の同期」)。
- 電源を切っても、時刻を保つには、拡張ボードの RTC 用の電池 (ボタン電池) が、必要です。電池の型は、ボードの資料で、確認してください。
- OLED の初期化に失敗しても (OLED がない、接触不良など)、ログに `Failed to initialize the OLED` を出して、表示だけを諦めます。RTC の初期化に失敗したときも、`Failed to initialize the RTC` を出して、時刻の行を出しません。ボタンも、同じです。どれも、測定と BLE の通知は、続きます。
- 初期化に失敗した RTC、OLED、ボタンは、**60 秒ごとに、初期化をやり直します** (起動のときの、一時的な失敗 (電源の立ち上がりや、接触の瞬断) から、リセットなしで、復帰するため)。使えるようになったログは、`RTC initialized`、`OLED initialized`、`Button initialized` です。
- OLED と RTC の設定は、Zephyr の `seeed_xiao_expansion_board` のシールドと、同じです (`app/thermo-node/boards/xiao_esp32c3.overlay`)。シールドは、SD カードの SPI も有効にするので、使わずに、OLED と RTC のノードだけを、書いています。
- RTC は、Zephyr の RTC ドライバ (v4.3.0 の `nxp,pcf8563`) を使わずに、このアプリのドライバ (`app/thermo-node/drivers/rtc_pcf8563.c`。Devicetree の compatible は `thermo,pcf8563`。binding は `app/thermo-node/dts/bindings/rtc/thermo,pcf8563.yaml`) を使います。Zephyr のドライバは、月 (0 から 11 の検証で、12 月を設定できない) と年 (1900 年からの年数を、そのまま BCD にする) の扱いが、チップと合っていないためです。このドライバは、Zephyr の RTC API (`rtc_set_time()` と `rtc_get_time()`) だけを実装します (アラームと割り込みは、ありません)。`node_time.c` は、この API で時刻を読み書きして、UNIX 時刻の換算と、ローカルタイムへの変換をします。
- **実機での確認は、まだです** (表示の向き、コントラスト、I2C のアドレス、RTC の読み書きは、実機で確認します)。

#### 起動画面と起動音

電源を入れたとき (リセットしたときも) に、OLED に、約 3 秒の起動画面を出して、ブザーで、短いメロディを鳴らします。

- **画面**: 画面の上に、題名 (`THERMO NODE`) を出して、その下を、口を開け閉めする丸い絵が、ドットを食べながら、左から右へ走ります。少し後ろから、敵の絵が追います。50 フレームを、60 ミリ秒ごとに、表示します。
- **音**: 拡張ボードの**パッシブブザー** (D3 = GPIO5) を、PWM (LEDC のチャンネル 0) で鳴らします。周波数が、音の高さで、デューティ比は 50% です。メロディ (約 2.6 秒) は、システムのワークキューで、順に鳴らすので、BLE の開始や、測定は、待たされません。
- 画面は、約 3 秒の間、測定のループの前で、動きます (BLE のアドバタイズは、すでに始まっています)。起動のときに、OLED かブザーが、使えなかったときは、その画面や音は、出しません (あとで使えるようになっても、出しません)。失敗しても、測定と通知は、続きます。
- ビルドのオプションで、別々に外せます。`CONFIG_THERMO_BOOT_SCREEN=n` で画面 (`THERMO_DISPLAY` が `n` のときも、なくなる)、`CONFIG_THERMO_BUZZER=n` で音 (Docker は、`THERMO_BUZZER=n docker compose run --rm build-thermo-node`)。

```bash
# West
west build -p always -b xiao_esp32c3 app/thermo-node -- -DCONFIG_THERMO_BOOT_SCREEN=n -DCONFIG_THERMO_BUZZER=n
```

**素材 (絵、題名、メロディ) の差し替え**

素材は、`app/thermo-node/src/boot_assets.c` が、定義します (このアプリのオリジナルの絵と曲です。形式は、`boot_assets.h`)。**ビルドのときに、`app/thermo-node/private/src/boot_assets.c` があれば、そちらを使います。** 別のプライベートリポジトリを、サブモジュールとして、`app/thermo-node/private/` に置けば、公開のリポジトリに、素材を入れずに、好きな絵と曲に、差し替えられます。

```bash
# 例: プライベートリポジトリを, サブモジュールとして置く (リポジトリの名前は, 例)
git submodule add git@github.com:<ユーザー>/thermo-private-assets.git app/thermo-node/private
git submodule update --init            # 別の場所で, 取り込むとき (権限のある人だけ)
```

- 差し替えるファイルは、`boot_assets.h` で宣言した全て (`boot_chomper` (3 つ)、`boot_chaser`、`boot_title` (12 文字まで。配列の大きさが合わないと、コンパイルエラー)、`boot_melody`、`boot_melody_count`) を、同じ名前と形で、定義します。サブモジュールが、なければ (権限のない人、CI)、オリジナルの素材を使うので、**サブモジュールなしで、そのままビルドできます。**
- メロディは、周波数の数値ではなく、音名で書けます (`notes.h`。`boot_assets.h` が、読み込みます)。`{NOTE_C5, 100}` のように、音名 (`NOTE_C5` は、オクターブ 5 のド。半音は `NOTE_CS5` など。フラットは、同じ高さのシャープの名前) と、長さ [ms] を並べます。休符は `NOTE_REST` です。平均律で、A4 を 440 Hz としています。
- **著作権と商標に、注意してください。** 既存のゲームの絵や曲を使うときは、その権利者の著作物と商標です。**このリポジトリ (公開) には、入れません。** サブモジュールの URL (`.gitmodules`) は、公開されても、中身は、権限のある人しか、見えません。
- **素材を入れてビルドした、ファームウェアの実行ファイル (`zephyr.elf`、`zephyr.bin`) を、公開しないでください** (GitHub Actions の成果物、リリースなど)。素材が、実行ファイルの中に入るためです。GitHub Actions の CI は、サブモジュールを取り込まない (`actions/checkout` の既定) ので、オリジナルの素材で、ビルドします。
- 一度でも、公開のリポジトリの履歴に、コミットした素材は、あとで消しても、履歴に残ります。公開のリポジトリに、コミットしないでください。
- 自分の機器に書き込んで、個人で使う範囲かどうかは、権利者と、各国の法律によります (私は、法律の専門家ではありません)。

**確認の範囲**: 単体テストで、メロディの順序と周波数から周期への換算、休符、再生の中断、PWM の失敗、起動画面の各フレーム (題名、ドット、絵の位置、はみ出しの切り取り、口の開き方、失敗)、起動の処理 (`main`) を、確認しています。**実機では、まだ確認していません**: 画面の見え方 (アニメーションの速さ)、音の大きさと音程 (パッシブ型は、小さいことがあります)、ブザーが D3 につながっているか。

### ゲートウェイからの時刻の同期

ノードは、ふだんは WiFi を使わずに、ゲートウェイが SNTP で得た時刻を、BLE で受け取って、RTC に設定します。BLE で時刻が届かないときだけ、WiFi で取ります (次の「WiFi での時刻の取得」)。

1. ゲートウェイは、ノードに接続して、Thermo サービスの中から、時刻の特性 (UUID の末尾が `9F3C1A02-...`。書き込み専用) を探します。
2. 見つけたら、UTC の UNIX 時刻 (uint32、リトルエンディアン、4 バイト) を、書き込みます。ノードは、RTC に設定します (範囲は 2000 年から 2099 年)。
3. ゲートウェイが、まだ SNTP で同期していないときは、書き込みを見送って、10 秒ごとに確かめます。同期できたら、書き込みます。そのため、ゲートウェイが、WiFi につながるまで、ノードの時刻は、未設定のままです。
4. その後も、1 時間ごとに、書き込み直して、RTC のずれを直します。ノードが拒否したときは、60 秒後に、やり直します。
5. ノードに時刻の特性がないとき (古いファームウェア、または `CONFIG_THERMO_RTC=n`) は、ゲートウェイのログに警告を出すだけで、温度の受信は、続きます。ゲートウェイが、クラウドなし (`native_sim`) のビルドのときは、時刻を書き込みません。
6. 時刻の特性は、認証をしないので、近くの BLE 機器は、誰でも書き込めます (表示する時刻だけに、影響します)。書き込みの時刻の誤差は、1 秒ほどです (BLE の通信の遅れ)。

### WiFi での時刻の取得 (BLE の予備)

起動の 1 分後から、1 分ごとに RTC の時刻を確かめて、時刻がなければ (ゲートウェイから時刻が届かなかったとき)、ノードが、自分で WiFi に接続して、SNTP で時刻を取り、RTC に設定して、すぐに WiFi を切ります。WiFi での取得は、最大 5 回まで試します (`CONFIG_THERMO_WIFI_TIME`。既定は `y`。`CONFIG_THERMO_RTC=y` が前提)。

1. ノードは、起動の 1 分後から、1 分ごとに、RTC に時刻があるかを確かめます。その間に、BLE でゲートウェイから時刻が届けば、RTC に時刻があるので、**BLE でも WiFi でも取る必要がなく、何もしないで、確認をやめます。**
2. 時刻がなければ、WiFi につないで、SNTP (`CONFIG_THERMO_NTP_SERVER`。既定は `pool.ntp.org`) で UTC の時刻を取り、RTC に設定して、WiFi を切ります。取れたら、確認をやめます。
3. 失敗したとき (SSID の未設定、接続の失敗、SNTP の応答なし) は、ログに出して、次の確認 (1 分後) でやり直します。**5 回試して取れなかったら、やめます** (その後に BLE で時刻が届けば、RTC に設定されます)。WiFi は、つなぎっぱなしにしません。
4. 取っている間 (接続に最大 20 秒、SNTP に最大 5 秒) は、メインのループが止まるので、温度の測定と OLED の更新も止まります。RTC が使えないとき、または、WiFi の初期化に失敗したときは、使えるようになるまで、試みに数えずに待ちます。

SSID とパスワードは、フラッシュ (settings の NVS) に保存します。シェルに直接入力したり、貼り付けたりすると、シェルの受信バッファ (64 バイト) があふれて、文字が欠けたり、固まったりするので、**スクリプト `scripts/send-wifi.sh` で、少しずつ送ります** (ゲートウェイの `scripts/send-cred.sh` と同じ考え方です。ポートを開いたままの `picocom` などは、先に終わらせます)。

```bash
scripts/send-wifi.sh <SSID> <パスワード> [ポート]
scripts/send-wifi.sh "my home ap" - /dev/ttyACM0      # パスワードを - にすると、画面に出さずに入力を求める
scripts/send-wifi.sh home-ap "" /dev/ttyACM0          # パスワードが空: オープンネットワーク
SYNC=1 scripts/send-wifi.sh home-ap - /dev/ttyACM0    # 送ったあと、時刻の取得 (thermo sync) も試す (約 30 秒)
```

スクリプトは、`thermo set ssid`、`thermo set psk`、`thermo show` を順に送ります。終わりに `ready to get the time: yes` と表示されれば、設定できています (ポートの既定は `/dev/ttyACM0`)。ノードのシェルには、`cred` コマンドは、ありません。ゲートウェイ用の `send-cred.sh` は、使いません (ノードは、証明書を使いません)。シェルに、短いコマンドを手で入力するときは、次のコマンドを使います。

```text
thermo set ssid <SSID>
thermo set psk <パスワード>    (オープンネットワークでは、設定しない)
thermo show                    (SSID と、設定が揃っているかを表示する。パスワードは隠す)
thermo sync                    (WiFi で時刻をすぐに取って、RTC に設定する。1 分待たずに試せる。試みの回数には数えない)
thermo reset                   (設定を消す)
```

- `thermo sync` で、1 分待たずに、WiFi での取得を確かめられます。2.4 GHz の WiFi だけに対応します (ESP32-C3)。
- 無効にするとき: `-DCONFIG_THERMO_WIFI_TIME=n` (このアプリの WiFi のコードだけが外れます。FLASH は 745 KB で、約 68 KB 減るだけです。`prj.conf` が WiFi とネットワークを有効にしているためで、完全に外すには、`prj.conf` のその部分も外します)。
- **実機での確認**: WiFi の接続と SNTP で、時刻を取って RTC に設定できることは、実機で確認しました。**未確認**: BLE の広告と DHT11 の読み取りが、WiFi と同時に動き続けること、スタックとヒープの余裕 (長時間の動作)。FLASH は 813 KB (従来は 445 KB)。メインスレッドのスタックを 6144 バイトにしました (WiFi と SNTP をメインスレッドで呼ぶため)。足りないときは、`prj.conf` で増やします。

OLED と RTC は、ビルドのオプションで、別々に外せます。Devicetree の alias (`thermo-display`、`thermo-rtc`) があるとき (`xiao_esp32c3`) の既定は、どちらも `y` です。

- `CONFIG_THERMO_DISPLAY=n`: OLED、CFB、表示のコード、グラフの履歴、ボタンが、ビルドから外れます。
- `CONFIG_THERMO_RTC=n`: RTC と時刻のコードが、ビルドから外れます (OLED には、日付と時刻の行が、出ません)。

```bash
# Docker: 環境変数 THERMO_DISPLAY と THERMO_RTC を n にして、ビルドする (既定は y)
THERMO_DISPLAY=n docker compose run --rm build-thermo-node
THERMO_RTC=n docker compose run --rm build-thermo-node

# West
west build -p always -b xiao_esp32c3 app/thermo-node -- -DCONFIG_THERMO_DISPLAY=n -DCONFIG_THERMO_RTC=n
```

### DHT11 の読み取りの失敗と、割り込みを止める設定

DHT11 の読み取りは、マイクロ秒単位のタイミングで、1 本の線を読みます。BLE などの割り込みが、この間に入ると、`Could not fetch the sensor (-5)` が、ときどき出ます。Zephyr のドライバは、1 つの信号が 100 マイクロ秒を超えると、何も表示せずに、`-EIO` を返します (デバッグビルドでも、`Invalid checksum` は、データが乱れたときだけ出ます)。

そのため、**読み取りの間 (約 22 ミリ秒)、割り込みを止める設定 (`CONFIG_DHT_LOCK_IRQS=y`) を、既定で有効にしています** (`app/thermo-node/prj.conf`)。実機で、この設定にすると、エラーが出なくなることを、確認しました。

- 割り込みを止めている間 (5 秒に 1 回、約 22 ミリ秒) は、BLE の処理も止まります。ゲートウェイとの接続が、切れやすくなっていないか (`Disconnected` が、増えていないか) を、実機で、確認してください。
- 無効にして試すときは、次のようにビルドします。

```bash
# Docker: 環境変数 THERMO_DHT_LOCK_IRQS を n にして、ビルドする (既定は y)
THERMO_DHT_LOCK_IRQS=n docker compose run --rm build-thermo-node

# West
west build -p always -b xiao_esp32c3 app/thermo-node -- -DCONFIG_DHT_LOCK_IRQS=n
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

XIAO ESP32C3 の USB は、リセットや書き込みのたびに、一度切れて、また、つながります。そのため、picocom は、リセットのときに、終了してしまい、起動のログ (初期化の成功と失敗) を、見逃します。次のように、つなぎ直しを、自動で繰り返すと、起動のログが、見えます。

```bash
while true; do picocom -b 115200 /dev/ttyACM0 --noinit --noreset; sleep 0.3; done
```

- `--noinit` と `--noreset` は、picocom が、接続のときに、モデムの初期化と、リセットを、しないための指定です。
- 終了するときは、Ctrl-A、Ctrl-X で、picocom を終了したあと、すぐに Ctrl-C で、ループを止めます。
- 2 台 (ノードとゲートウェイ) を、同時につなぐときは、`/dev/ttyACM0` と `/dev/ttyACM1` を、別の端末で開きます。

### ファイルシステムのシェル (Thermo ノード)

Thermo ノードのシェルには、ファイルシステムのコマンド `fs` が、あります (`CONFIG_FILE_SYSTEM_SHELL=y`。フラッシュが約 1.7 KB、RAM が約 160 B 増えます)。SD カードを使う機能は、まだ実装していないので、今は、マウントするものがありません (`fs` だけで、コマンドの一覧を表示します)。SD カードに対応したあとに、picocom で、ファイルの一覧や中身を確認できるようにするための設定です。

- 主なコマンド: `fs ls`、`fs cat <ファイル>`、`fs read <ファイル>`、`fs cd`、`fs pwd`、`fs statvfs`、`fs mount`。
- **`fs rm` と `fs write` も使えます。** USB のシリアルにつなげた人は、ファイルを消したり、書き換えたりできます。ログを書いている最中には、実行しないでください (FAT が壊れるおそれがあります)。
- 大きなファイルに `fs cat` を実行すると、画面に大量に流れます。

## CI/CD（GitHub Actions）

このリポジトリは GitHub Actions で、ビルド・単体テスト・静的解析が自動実行されます。

### ワークフロー

`.github/workflows/build.yml` は、main ブランチ、`ccr-*` ブランチへの Push と、main への PR で実行されます。ローカルと同じ Docker イメージ (Zephyr SDK 0.17.4) で、`docker compose` のサービスを実行します。

| ジョブ | 実行するサービス | 内容 |
|:---|:---|:---|
| `build` | `build-thermo-node` / `build-thermo-gateway` / `build-thermo-node-sim` / `build-thermo-gateway-sim` | ビルド。警告が出たら失敗 |
| `test` | `test-thermo-node` / `test-thermo-gateway` | 単体テスト (Ztest + FFF, `native_sim`) |
| `coverage` | `coverage-thermo-node` / `coverage-thermo-gateway` | 単体テストのカバレッジ (行と分岐。100% 未満なら失敗) |
| `docs` | `docs-thermo` | ドキュメント (Doxygen)。警告があれば失敗。生成物 (`docs/`) は、アーティファクトに保存 |
| `analyze` | `analyze-thermo-node` / `analyze-thermo-gateway` | 静的解析 (gcc `-fanalyzer`)。指摘があれば失敗 |
| `lint` | `format-thermo` / `lint-thermo` / `whitespace-thermo` | 整形の確認 (clang-format)、Zephyr の `checkpatch.pl`、行末の空白の確認。指摘があれば失敗 |

Docker イメージは、レイヤーを GitHub Actions のキャッシュに保存します (`.github/actions/docker-image`)。`Dockerfile` か `west.yml` を変えたときだけ、イメージが作り直されます。

CI と同じ確認は、ローカルでも実行できます。短い名前のサービス (`build` `test` `coverage` `analyze`) は、node と gateway の両方を、まとめて実行します (`format` `format-fix` `lint` `whitespace` `docs` は、`*-thermo` と同じ。`clean` は、`build/` と `docs/` を消す)。CI は、個別のサービス (`*-thermo-node` など) を使います。

```bash
# node と gateway の両方
docker compose run --rm test
docker compose run --rm coverage
docker compose run --rm analyze

# 個別
docker compose run --rm test-thermo-node
docker compose run --rm test-thermo-gateway
# カバレッジ (行と分岐が 100% でなければ失敗する)
docker compose run --rm coverage-thermo-node
docker compose run --rm coverage-thermo-gateway
# ドキュメント (Doxygen。docs/index.html を開く。警告があれば失敗する)
docker compose run --rm docs-thermo
docker compose run --rm analyze-thermo-node
docker compose run --rm analyze-thermo-gateway
docker compose run --rm format-thermo
docker compose run --rm lint-thermo
docker compose run --rm whitespace-thermo
```

コーディングスタイルは Zephyr の規約に合わせています。違う点は、[CODING_STYLE.md](CODING_STYLE.md) を参照してください。

### ビルドアーティファクト

`build` ジョブの成功・失敗にかかわらず、以下のファイルがアップロードされます：
- `zephyr.elf` - デバッグ情報付き実行ファイル
- `zephyr.bin` - バイナリ (実機用)
- `zephyr.exe` - シミュレーション用の実行ファイル (`native_sim`)

アーティファクトの名前は、小文字のスネークケースです (サービス名の `-` を `_` にします)。

| 名前 | 内容 |
|:---|:---|
| `build_thermo_node` `build_thermo_gateway` `build_thermo_node_sim` `build_thermo_gateway_sim` | 上のビルド成果物と `build.log` |
| `test_thermo_node_log` `test_thermo_gateway_log` | 単体テストのログ |
| `coverage_thermo_node_log` `coverage_thermo_gateway_log` | カバレッジのログ |
| `analyze_thermo_node_log` `analyze_thermo_gateway_log` | 静的解析のログ |
| `format_thermo_log` `lint_thermo_log` `whitespace_thermo_log` | 整形、lint、行末の空白の確認のログ |
| `docs` `docs_log` | Doxygen のドキュメントと、生成のログ |

## トラブルシューティング

### Docker コマンド実行時に permission denied エラーが発生する

Linux 環境で `docker` コマンドを実行した際に `Got permission denied while trying to connect to the Docker daemon socket` が表示される場合：

```bash
# ユーザーを docker グループに追加
sudo usermod -aG docker $USER

# グループの変更を即時反映（または一度ログアウトして再ログイン）
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
# Linux の場合、udev ルールを設定
sudo usermod -a -G dialout $USER
sudo usermod -a -G uucp $USER
# ログアウト後、再度ログイン
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

問題が発生した場合は、GitHub Issues で報告してください。
