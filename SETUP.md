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

- esptool.py のインストール

```bash
pip install esptool
```

### フラッシング手順

1. ESP32C3 を USB で接続

2. デバイスポート確認
   ```bash
   ls /dev/ttyUSB*  # Linux
   ls /dev/tty.usbserial*  # macOS
   ```

3. ファームウェアをフラッシング
   ```bash
   # Thermo Node
   esptool.py -p /dev/ttyUSB0 write_flash 0x0 build/zephyr/zephyr.bin

   # Thermo Gateway
   esptool.py -p /dev/ttyUSB0 write_flash 0x0 build/zephyr/zephyr.bin
   ```

## シリアルモニター

```bash
# picocom (推奨)
picocom -b 115200 /dev/ttyUSB0

# screen
screen /dev/ttyUSB0 115200

# minicom
minicom -D /dev/ttyUSB0 -b 115200
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
