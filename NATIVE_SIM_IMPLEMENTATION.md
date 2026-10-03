# native_sim サポート実装サマリー

## 概要

Zephyr の `native_sim` ボードを使用したシミュレーション環境サポートを実装しました。同じコードベースで ESP32C3 ハードウェアと Linux シミュレーション環境の両方をサポートします。

## 実装内容

### 1. センサー実装の条件付きコンパイル

**ファイル**: `app/thermo-node/src/sensor.c`

- `CONFIG_SIMULATOR` フラグで実装を切り替え
- **実機モード**: 実際のADCハードウェアから温度データを読取
- **シミュレーションモード**: `sys_rand32_get()` でランダムな温度値（0-4095）を生成
- デバイスツリーの存在確認で ADC デバイスの可用性を判定

### 2. Thermo Node メインアプリケーション

**ファイル**: `app/thermo-node/src/main.c`

```c
// 追加実装：
- ble_init() による Bluetooth 初期化
- ble_advertise() による BLE アドバタイジング開始
- sensor_init() によるセンサ初期化
- sensor_read_temperature() による定期的な温度読取
- CONFIG_SIMULATOR フラグに応じたログメッセージ
```

### 3. Thermo Gateway メインアプリケーション

**ファイル**: `app/thermo-gateway/src/main.c`

```c
// 追加実装：
- ble_init() による Bluetooth 初期化
- ble_scan() による BLE スキャン開始
- CONFIG_SIMULATOR フラグに応じたログメッセージ
```

### 4. プロジェクト設定ファイル

#### Thermo Node

**ファイル**: `app/thermo-node/prj-native_sim.conf`

```conf
CONFIG_SIMULATOR=y              # シミュレーターモード有効化
CONFIG_NATIVE_LIBRARY=y         # ネイティブライブラリサポート
CONFIG_MAIN_STACK_SIZE=8192     # スタックサイズ設定
CONFIG_RANDOM=y                 # ランダム数生成
CONFIG_NATIVE_POSIX_RAND=y      # POSIX ベースのランダム生成
```

#### Thermo Gateway

**ファイル**: `app/thermo-gateway/prj-native_sim.conf`

```conf
# ADC 不要の最小構成
CONFIG_NATIVE_LIBRARY=y
CONFIG_MAIN_STACK_SIZE=8192
```

### 5. Device Tree Overlay

**ファイル**: `app/thermo-node/boards/native_sim.overlay`

- native_sim ボード用の zephyr_user デバイスノード定義
- ADC が不要な環境での互換性を確保

### 6. ビルドスクリプト拡張

**ファイル**: `build.sh`

新オプション追加：
```bash
-s, --sim    Build for native_sim instead of ESP32C3
```

動作：
- `--sim` フラグで自動的に以下を設定：
  - ボード: `native_sim`
  - ビルドディレクトリ: `build/{app}-sim`
  - 設定ファイル: `prj-native_sim.conf`

使用例：
```bash
./build.sh node --sim        # Node をシミュレーション用にビルド
./build.sh all --sim         # 全アプリをシミュレーション用にビルド
```

### 7. Docker Compose サポート

**ファイル**: `docker-compose.yml`

新サービス追加：
- `build-thermo-node-sim` - Node シミュレーション用ビルド
- `build-thermo-gateway-sim` - Gateway シミュレーション用ビルド

使用例：
```bash
docker compose run build-thermo-node-sim
docker compose run build-thermo-gateway-sim
```

### 8. ドキュメント

#### SIMULATION.md（新規作成）

包括的なシミュレーション手引き：
- ビルド方法（3つの方法を解説）
- 実行方法
- シミュレーション機能説明
- 開発ワークフロー例
- トラブルシューティング

#### README.md（更新）

- シミュレーション実行を「推奨・最速」の方法として最初に記載
- Docker Compose コマンド更新
- クイックスタートセクション拡張

## 動作流れ

### ビルドプロセス

```
build.sh node --sim
  ↓
west build -b native_sim app/thermo-node -d build/thermo-node-sim -DCONF_FILE=prj-native_sim.conf
  ↓
CONFIG_SIMULATOR が定義される
  ↓
sensor.c: ADC コード無視、ランダム値生成に切り替え
main.c: CONFIG_SIMULATOR ログ出力
  ↓
build/thermo-node-sim/zephyr/zephyr.exe 生成
```

### 実行時

```
./build/thermo-node-sim/zephyr/zephyr.exe
  ↓
"SIMULATOR MODE" ログ出力
  ↓
sensor_init() → シミュレーテッドセンサ初期化
  ↓
ble_init() → Bluetooth スタック初期化
  ↓
ble_advertise() → BLE アドバタイジング開始
  ↓
5秒ごとに温度読取: sys_rand32_get() % 4096
```

## 利点

1. **開発の高速化**
   - ハードウェアなしで即座にテスト可能
   - ビルド～実行のサイクルが短い

2. **CI/CD 統合**
   - クラウドテスト環境で native_sim ビルドが可能
   - 実ハードウェアなしで機能検証

3. **デバッグの容易さ
   - Linux デバッガ（GDB）でコード実行可能
   - ログ出力が明確

4. **コード品質向上**
   - シミュレーション層でテスト
   - 本番環境への不具合混入を削減

## 後方互換性

- 既存の `./build.sh node` や `./build.sh gateway` は変わらず ESP32C3 用にビルド
- `prj.conf` は ESP32C3 用のまま（`prj-native_sim.conf` は別ファイル）
- 完全な後方互換性を保証

## 今後の拡張案

- TCP/IP シミュレーション
- 複数ノード同時シミュレーション
- パフォーマンスプロファイリング
- CI ワークフローへの組み込み
