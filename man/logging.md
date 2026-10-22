# ログ

ログ (`zephyr/logging/log.h` `log_backend.h` `log_output.h`) の、使っている API。ログは、`LOG_*` で書く (`printk` や `printf` は使わない)。ノードは、独自のバックエンドで、SD カードにログを保存する (`app/thermo-node/src/sdlog.c`)。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## ログの出力

### LOG_MODULE_REGISTER

```c
#define LOG_MODULE_REGISTER(...)
```

ソースファイルを、ログのモジュールとして登録する。各ソースファイルで、1 回だけ書く。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `モジュール名` | in | 1 つ目の引数 |
| `ログのレベル` | in | 2 つ目の引数。省略すると `CONFIG_LOG_DEFAULT_LEVEL` |

注意:

- `CONFIG_LOG_DEFAULT_LEVEL` が 0 でないとき、モジュールの状態が定義され、登録される。

使用箇所: [`app/common/cfg.c:23`](../app/common/cfg.c#L23), [`app/common/cloud.c:30`](../app/common/cloud.c#L30), [`app/common/ntp.c:23`](../app/common/ntp.c#L23), [`app/common/wifi_link.c:23`](../app/common/wifi_link.c#L23) ほか 15 ファイル

### LOG_ERR

```c
#define LOG_ERR(...)
```

エラーのレベルで、ログを書く。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `...` | in | `printf()` と同じ書式文字列と引数 |

使用箇所: [`app/common/cfg.c:231`](../app/common/cfg.c#L231) (ほか 5 行), [`app/common/cloud.c:198`](../app/common/cloud.c#L198) (ほか 7 行), [`app/common/ntp.c:70`](../app/common/ntp.c#L70) (ほか 2 行), [`app/common/wifi_link.c:103`](../app/common/wifi_link.c#L103) (ほか 2 行) ほか 14 ファイル

### LOG_WRN

```c
#define LOG_WRN(...)
```

警告のレベルで、ログを書く。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `...` | in | 書式文字列と引数 |

使用箇所: [`app/common/cfg.c:484`](../app/common/cfg.c#L484) (ほか 9 行), [`app/common/cloud.c:185`](../app/common/cloud.c#L185) (ほか 3 行), [`app/common/wifi_link.c:146`](../app/common/wifi_link.c#L146) (ほか 1 行), [`app/thermo-gateway/src/ble.c:366`](../app/thermo-gateway/src/ble.c#L366) (ほか 4 行) ほか 9 ファイル

### LOG_INF

```c
#define LOG_INF(...)
```

情報のレベルで、ログを書く。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `...` | in | 書式文字列と引数 |

使用箇所: [`app/common/cloud.c:212`](../app/common/cloud.c#L212), [`app/common/ntp.c:89`](../app/common/ntp.c#L89), [`app/common/wifi_link.c:174`](../app/common/wifi_link.c#L174), [`app/thermo-gateway/src/ble.c:178`](../app/thermo-gateway/src/ble.c#L178) (ほか 8 行) ほか 12 ファイル

### LOG_DBG

```c
#define LOG_DBG(...)
```

デバッグのレベルで、ログを書く。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `...` | in | 書式文字列と引数 |

使用箇所: [`app/common/cloud.c:455`](../app/common/cloud.c#L455) (ほか 2 行), [`app/common/wifi_link.c:179`](../app/common/wifi_link.c#L179), [`app/thermo-gateway/src/ble.c:330`](../app/thermo-gateway/src/ble.c#L330), [`app/thermo-node/src/ble.c:220`](../app/thermo-node/src/ble.c#L220)

### LOG_HEXDUMP_DBG

```c
#define LOG_HEXDUMP_DBG(_data, _length, _str)
```

デバッグのレベルで、データを 16 進数でダンプする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_data` | in | データへのポインタ |
| `_length` | in | データの長さ [バイト] |
| `_str` | in | 見出しの文字列 (永続する文字列) |

使用箇所: [`app/common/cloud.c:644`](../app/common/cloud.c#L644), [`app/thermo-gateway/src/ble.c:139`](../app/thermo-gateway/src/ble.c#L139) (ほか 2 行), [`app/thermo-node/src/ble.c:216`](../app/thermo-node/src/ble.c#L216) (ほか 1 行)

## ログのバックエンド (SD カードへの保存)

### LOG_BACKEND_DEFINE

```c
#define LOG_BACKEND_DEFINE(_name, _api, _autostart, ...)
```

ログのバックエンドのインスタンスを作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_name` | in | インスタンスの名前 |
| `_api` | in | バックエンドの API (`struct log_backend_api`) |
| `_autostart` | in | `true` なら、ログのシステムの初期化と一緒に、バックエンドを初期化して有効にする |
| `...` | in | 任意のコンテキスト |

使用箇所: [`app/thermo-node/src/sdlog.c:105`](../app/thermo-node/src/sdlog.c#L105)

### LOG_OUTPUT_DEFINE

```c
#define LOG_OUTPUT_DEFINE(_name, _func, _buf, _size)
```

`log_output` のインスタンスを作る。メッセージを、文字列にして、バッファに溜め、満杯のときなどに、出力の関数を呼ぶ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_name` | in | インスタンスの名前 |
| `_func` | in | 出力のデータを処理する関数 |
| `_buf` | in | 出力のバッファ |
| `_size` | in | バッファの大きさ |

使用箇所: [`app/thermo-node/src/sdlog.c:94`](../app/thermo-node/src/sdlog.c#L94) (ほか 1 行)

### LOG_OUTPUT_FLAG_*

```c
#define LOG_OUTPUT_FLAG_TIMESTAMP         BIT(1)
#define LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP  BIT(2)
#define LOG_OUTPUT_FLAG_LEVEL             BIT(3)
#define LOG_OUTPUT_FLAG_CRLF_LFONLY       BIT(5)
```

ログのメッセージを文字列にするときの書式のフラグ (`zephyr/logging/log_output.h`)。`log_format_func_t_get()` で得た関数の `flags` に、ビットの OR で渡す。

| 定数 | 説明 |
|:---|:---|
| `LOG_OUTPUT_FLAG_TIMESTAMP` | 時刻 (起動からの時間) を付ける |
| `LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP` | 時刻を、`時:分:秒.ミリ秒,マイクロ秒` の形にする |
| `LOG_OUTPUT_FLAG_LEVEL` | レベル (`<err>` など) を付ける |
| `LOG_OUTPUT_FLAG_CRLF_LFONLY` | 改行を LF だけにする (CR を付けない) |

使用箇所: [`app/thermo-node/src/sdlog.c:55`](../app/thermo-node/src/sdlog.c#L55) (ほか 1 行)

### log_format_func_t_get

```c
log_format_func_t log_format_func_t_get(uint32_t log_type);
```

出力の形式 (`LOG_OUTPUT_TEXT` など) から、メッセージを整形する関数を取り出す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `log_type` | in | 出力の形式 |

戻り値: メッセージを整形する関数。

使用箇所: [`app/thermo-node/src/sdlog.c:169`](../app/thermo-node/src/sdlog.c#L169)

### log_msg_get_level

```c
static inline uint8_t log_msg_get_level(struct log_msg *msg);
```

ログのメッセージの、レベルを返す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `msg` | in | ログのメッセージ |

戻り値: ログのレベル (`LOG_LEVEL_ERR` など)。

使用箇所: [`app/thermo-node/src/sdlog.c:176`](../app/thermo-node/src/sdlog.c#L176)

### log_backend_api

```c
struct log_backend_api
```

ログのバックエンドの API。メッセージを処理する `process`、初期化の `init`、パニックのときの `panic`、取りこぼしの通知 `dropped` などの関数を持つ。

使用箇所: [`app/thermo-node/src/sdlog.c:99`](../app/thermo-node/src/sdlog.c#L99)
