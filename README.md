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

### 開発環境構築

詳細は [SETUP.md](./SETUP.md) を参照してください。

```bash
# West ワークスペース初期化
west init --mr main .
west update

# Thermo Node をビルド
west build -b xiao_esp32c3 app/thermo-node

# Thermo Gateway をビルド
west build -b xiao_esp32c3 app/thermo-gateway
```

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
- BLE スキャンで周辺ノード検出
- 複数ノードからのデータ集約
- デバッグシェル対応

## 対応ハードウェア

- **MCU**: ESP32C3
- **開発ボード**: Seeed Studio XIAO ESP32C3
- **通信**: BLE 5.0

## CI/CD

GitHub Actions により、以下の自動化が設定されています：

- **自動ビルド**: main ブランチおよび PR での自動ビルド検証
- **アーティファクト保存**: ビルド成功時に ELF/HEX ファイルを自動保存

## 開発フロー

1. `ccr-*` ブランチで機能開発
2. Push 時に GitHub Actions で自動ビルド
3. PR をマージする前にビルド成功を確認
4. main ブランチへのマージ

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
