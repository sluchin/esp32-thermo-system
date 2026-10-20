# 共通のヘッダの解析

対象: `app/common/thermo_ble_uuid.h` (73 行)、`thermo_log.h` (25 行)、`thermo_version.h` (28 行)、`switchbot_sample.h` (29 行)

処理を持たない、定義だけのヘッダです。ノードとゲートウェイが、同じ値を使うために、共通の場所に置いています。

## thermo_ble_uuid.h: BLE の UUID と値の形式

Thermo サービスの UUID (128 ビット) は、`9F3C1A00-7B6E-4C3A-9D5E-2A6F0B1C8D01` を基準にして、先頭の 4 バイトの下位だけを変えて、特性を区別します。

| UUID | 内容 | 属性 |
|:---|:---|:---|
| `9F3C1A00-...` | Thermo サービス | |
| `9F3C1A01-...` | 温度の特性 | 読み取りと通知 (ノードが通知) |
| `9F3C1A02-...` | 時刻の特性 | 書き込みだけ (ゲートウェイが書く) |

各 UUID を、2 つの形で定義します。`*_VAL` は、`BT_UUID_128_ENCODE()` の値 (スキャン応答のバイト列や、`BT_UUID_INIT_128()` で使う)。そうでない名前 (`THERMO_UUID_SERVICE` など) は、`BT_UUID_DECLARE_128()` (GATT の属性の定義で使う)。

| 定数 | 値 | 意味 |
|:---|:---|:---|
| `THERMO_TEMPERATURE_SIZE` | 6 | 温度の特性の値の大きさ。温度 `int16_t` と湿度 `uint16_t` と気圧 `uint16_t` (hPa の 10 倍)、リトルエンディアン |
| `THERMO_TEMPERATURE_SIZE_NO_PRESSURE` | 4 | 気圧を足す前のノードの、温度の特性の値の大きさ (温度と湿度だけ)。ゲートウェイは、この大きさも受け取り、気圧なしにする |
| `THERMO_TIME_SIZE` | 4 | 時刻の特性の値の大きさ。UTC の UNIX 時刻 `uint32_t`、リトルエンディアン |
| `THERMO_HUMIDITY_NONE` | 0xFFFF | 湿度なし (温度だけのセンサ) |
| `THERMO_PRESSURE_NONE` | 0xFFFF | 気圧なし (気圧センサがない、または使えない) |

ノード (`thermo-node/src/ble.c`) とゲートウェイ (`thermo-gateway/src/ble.c`) の双方が、この値でパックと解析を行います。

## thermo_log.h: ログのレベル

```c
#ifndef THERMO_LOG_LEVEL
#define THERMO_LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#endif
```

各ソースの `LOG_MODULE_REGISTER(名前, THERMO_LOG_LEVEL)` が使います。通常は、Zephyr の既定のレベルです。CMake の `THERMO_DEBUG_LOG` を ON にすると、`THERMO_LOG_LEVEL=4` (デバッグ) を定義して、このアプリのモジュールだけのレベルを、まとめて上げます (`DEBUG_LOG.md` を参照)。

## thermo_version.h: バージョンと git のハッシュ

`THERMO_VERSION` (リポジトリ直下の `VERSION` の値) と `THERMO_GIT_HASH` (`git describe --always --dirty --abbrev=12` の出力) を定義します。値は、ビルドのときに、`app/version.cmake` が、マクロとして渡します (CMake を実行したときの値。git の `HEAD` と `index` が変わると、CMake がやり直されます)。渡されなかったとき (単体テスト、git が使えないビルド) は、`unknown` です。コミットしていない変更があると、ハッシュの末尾に `-dirty` が付きます。両方のアプリの `main()` が、起動のログに出します (`Version 0.1.0 (git 584daa5c1f2e)`)。ログは、SD カードの `boot.log` にも残ります。

## switchbot_sample.h: SwitchBot の値

`struct switchbot_sample` (温度 `temp_x10`、湿度 `humidity`、電池残量 `battery`) を定義します。`switchbot.c` が作り、`cloud.c` と `payload.c` が使います。電池残量がわからないときは、`SWITCHBOT_BATTERY_UNKNOWN` (-1) です。

## 関連

- [cloud.md](cloud.md)、[payload.md](payload.md): `struct switchbot_sample` の利用
- [index.md](index.md): このディレクトリの文書の一覧
