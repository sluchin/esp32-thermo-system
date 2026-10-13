# ログ

ログ (`zephyr/logging/log.h` `log_backend.h` `log_output.h`) の、使っている API。ログは、`LOG_*` で書く (`printk` や `printf` は使わない)。ノードは、独自のバックエンドで、SD カードにログを保存する (`app/thermo-node/src/sdlog.c`)。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

## ログの出力

### LOG_MODULE_REGISTER

```c
#define LOG_MODULE_REGISTER(...)
```

ソースファイルを、ログのモジュールとして登録する。引数は 1 つか 2 つ。1 つ目はモジュール名、2 つ目はログのレベル (省略すると `CONFIG_LOG_DEFAULT_LEVEL`)。各ソースファイルで、1 回だけ書く。

- `CONFIG_LOG_DEFAULT_LEVEL` が 0 でないとき、モジュールの状態が定義され、登録される。

使用箇所: `app/common/cfg.c:23`, `app/common/cloud.c:30`, `app/common/ntp.c:23`, `app/common/wifi_link.c:23` ほか 15 ファイル

### LOG_ERR

```c
#define LOG_ERR(...)
```

エラーのレベルで、ログを書く。書式は `printf()` と同じ。

使用箇所: `app/common/cfg.c:231` (ほか 5 行), `app/common/cloud.c:188` (ほか 7 行), `app/common/ntp.c:70` (ほか 2 行), `app/common/wifi_link.c:103` (ほか 2 行) ほか 14 ファイル

### LOG_WRN

```c
#define LOG_WRN(...)
```

警告のレベルで、ログを書く。

使用箇所: `app/common/cfg.c:484` (ほか 9 行), `app/common/cloud.c:175` (ほか 3 行), `app/common/wifi_link.c:146` (ほか 1 行), `app/thermo-gateway/src/ble.c:362` (ほか 4 行) ほか 9 ファイル

### LOG_INF

```c
#define LOG_INF(...)
```

情報のレベルで、ログを書く。

使用箇所: `app/common/cloud.c:202`, `app/common/ntp.c:89`, `app/common/wifi_link.c:174`, `app/thermo-gateway/src/ble.c:178` (ほか 8 行) ほか 12 ファイル

### LOG_DBG

```c
#define LOG_DBG(...)
```

デバッグのレベルで、ログを書く。

使用箇所: `app/common/cloud.c:433` (ほか 2 行), `app/common/wifi_link.c:179`, `app/thermo-gateway/src/ble.c:330`, `app/thermo-node/src/ble.c:216`

### LOG_HEXDUMP_DBG

```c
#define LOG_HEXDUMP_DBG(_data, _length, _str)
```

デバッグのレベルで、データを 16 進数でダンプする。

- `_data`: データへのポインタ。`_length`: データの長さ [バイト]。`_str`: 見出しの文字列 (永続する文字列)。

使用箇所: `app/common/cloud.c:621`, `app/thermo-gateway/src/ble.c:139` (ほか 2 行), `app/thermo-node/src/ble.c:212` (ほか 1 行)

## ログのバックエンド (SD カードへの保存)

### LOG_BACKEND_DEFINE

```c
#define LOG_BACKEND_DEFINE(_name, _api, _autostart, ...)
```

ログのバックエンドのインスタンスを作る。

- `_name`: インスタンスの名前。`_api`: バックエンドの API (`struct log_backend_api`)。`_autostart`: `true` なら、ログのシステムの初期化と一緒に、バックエンドを初期化して有効にする。`...`: 任意のコンテキスト。

使用箇所: `app/thermo-node/src/sdlog.c:97`

### LOG_OUTPUT_DEFINE

```c
#define LOG_OUTPUT_DEFINE(_name, _func, _buf, _size)
```

`log_output` のインスタンスを作る。メッセージを、文字列にして、バッファに溜め、満杯のときなどに、出力の関数を呼ぶ。

- `_name`: インスタンスの名前。`_func`: 出力のデータを処理する関数。`_buf`: 出力のバッファ。`_size`: バッファの大きさ。

使用箇所: `app/thermo-node/src/sdlog.c:86` (ほか 1 行)

### log_format_func_t_get

```c
log_format_func_t log_format_func_t_get(uint32_t log_type);
```

出力の形式 (`LOG_OUTPUT_TEXT` など) から、メッセージを整形する関数を取り出す。

使用箇所: `app/thermo-node/src/sdlog.c:161`

### log_backend_api

```c
struct log_backend_api
```

ログのバックエンドの API。メッセージを処理する `process`、初期化の `init`、パニックのときの `panic`、取りこぼしの通知 `dropped` などの関数を持つ。

使用箇所: `app/thermo-node/src/sdlog.c:91`
