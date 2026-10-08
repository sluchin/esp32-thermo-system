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

### OLED の表示 (拡張ボード)

拡張ボードの OLED (SSD1306、128 x 64 ドット) に、温度と湿度を表示します。ノードの、XIAO ESP32C3 を、拡張ボードに差せば、配線は、要りません (OLED は、ボードの I2C。D4 が SDA、D5 が SCL。アドレスは `0x3C`)。

- 表示は、1 行目が温度 (`Temp 23.5 C`)、2 行目が湿度 (`Humi 45.0 %`) です。湿度を測れないとき (ADC のセンサ) は、`Humi --.- %` と表示します。測定のたびに (5 秒ごと)、更新します。
- OLED の初期化に失敗しても (OLED がない、接触不良など)、ログに `Failed to initialize the OLED` を出して、表示だけを諦めます。測定と BLE の通知は、続きます。
- 設定は、Zephyr の `seeed_xiao_expansion_board` のシールドと、同じです (`app/thermo-node/boards/xiao_esp32c3.overlay`)。シールドは、SD カードの SPI も有効にするので、使わずに、OLED のノードだけを、書いています。
- **実機での確認は、まだです** (表示の向きや、コントラストは、実機で確認します)。

OLED を使わないときは、`CONFIG_THERMO_DISPLAY=n` で、ビルドします (OLED、CFB、I2C のコードが、ビルドから外れます)。Devicetree の alias `thermo-display` があるとき (`xiao_esp32c3`) の既定は、`y` です。

```bash
# Docker: 環境変数 THERMO_DISPLAY を n にして、ビルドする (既定は y)
THERMO_DISPLAY=n docker compose run --rm build-thermo-node

# West
west build -p always -b xiao_esp32c3 app/thermo-node -- -DCONFIG_THERMO_DISPLAY=n
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

CI と同じ確認は、ローカルでも実行できます:

```bash
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
