# Zephyr OS Development Environment Setup

このドキュメントは、ESP32C3上でZephyr RTOSベースのサーモシステムを開発するための環境構築手順です。

## Docker を使用したセットアップ（推奨）

Docker と docker-compose がインストールされている場合、以下のコマンドで開発環境をセットアップできます：

```bash
# Thermo Node をビルド
docker-compose run build-thermo-node

# Thermo Gateway をビルド
docker-compose run build-thermo-gateway

# インタラクティブ開発シェル
docker-compose run dev
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
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.1/zephyr-sdk-0.16.1_linux-x86_64.tar.xz

# インストール
tar xf zephyr-sdk-0.16.1_linux-x86_64.tar.xz -C ~/
~/zephyr-sdk-0.16.1/setup.sh

# インストール後、ホストツールをセットアップ
cd ~/zephyr-sdk-0.16.1 && ./setup.sh
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
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-0.16.1
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

このリポジトリは GitHub Actions で自動ビルドが設定されています。

### ワークフロー

- **build.yml**: main ブランチおよび PR で自動的にビルドが実行されます

### ビルドアーティファクト

ビルド成功時、以下のファイルがアップロードされます：
- `zephyr.elf` - デバッグ情報付き実行ファイル
- `zephyr.hex` - HEX フォーマット

## トラブルシューティング

### West コマンドが見つからない

```bash
source venv/bin/activate
pip install west
```

### Zephyr SDK が見つからない

```bash
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-0.16.1
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
- [ESP32C3 ボード情報](https://docs.zephyrproject.org/latest/boards/riscv/xiao_esp32c3/doc/index.html)
- [Bluetooth Low Energy ガイド](https://docs.zephyrproject.org/latest/connectivity/bluetooth/index.html)

## サポート

問題が発生した場合は、GitHub Issues で報告してください。
