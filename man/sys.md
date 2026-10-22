# システムのユーティリティ

システムのライブラリ (`zephyr/sys/atomic.h` `byteorder.h` `crc.h` `ring_buffer.h` `clock.h` `util.h`、`zephyr/random/random.h`) の、使っている API。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## アトミック操作

### atomic_add

```c
atomic_val_t atomic_add(atomic_t *target, atomic_val_t value);
```

アトミックに足し算をする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `target` | in,out | アトミック変数のアドレス |
| `value` | in | 足す値 |

戻り値: `target` の、足す前の値。

使用箇所: [`app/thermo-node/src/sdlog.c:226`](../app/thermo-node/src/sdlog.c#L226)

### atomic_set

```c
atomic_val_t atomic_set(atomic_t *target, atomic_val_t value);
```

アトミックに値を設定して、前の値を返す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `target` | in,out | アトミック変数のアドレス |
| `value` | in | 設定する値 |

戻り値: `target` の、設定する前の値。

使用箇所: [`app/thermo-node/src/sdlog.c:330`](../app/thermo-node/src/sdlog.c#L330)

## バイトオーダー

### sys_get_le16

```c
static inline uint16_t sys_get_le16(const uint8_t src[2]);
```

リトルエンディアンで置かれた 16 ビットの整数を、ホストのバイトオーダーで取り出す。アラインメントされていないメモリでもよい。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `src` | in | リトルエンディアンの 16 ビットの整数の場所 |

戻り値: ホストのバイトオーダーの 16 ビットの整数。

使用箇所: [`app/thermo-gateway/src/ble.c:374`](../app/thermo-gateway/src/ble.c#L374) (ほか 1 行), [`app/thermo-node/src/backlog.c:452`](../app/thermo-node/src/backlog.c#L452) (ほか 1 行)

### sys_get_le32

```c
static inline uint32_t sys_get_le32(const uint8_t src[4]);
```

リトルエンディアンで置かれた 32 ビットの整数を、ホストのバイトオーダーで取り出す。アラインメントされていないメモリでもよい。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `src` | in | リトルエンディアンの 32 ビットの整数の場所 |

戻り値: ホストのバイトオーダーの 32 ビットの整数。

使用箇所: [`app/thermo-node/src/backlog.c:450`](../app/thermo-node/src/backlog.c#L450) (ほか 1 行), [`app/thermo-node/src/ble.c:299`](../app/thermo-node/src/ble.c#L299)

### sys_put_le16

```c
static inline void sys_put_le16(uint16_t val, uint8_t dst[2]);
```

ホストのバイトオーダーの 16 ビットの整数を、リトルエンディアンで置く。アラインメントされていないメモリでもよい。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `val` | in | ホストのバイトオーダーの 16 ビットの整数 |
| `dst` | out | 結果を置く場所 |

使用箇所: [`app/thermo-node/src/backlog.c:494`](../app/thermo-node/src/backlog.c#L494) (ほか 1 行), [`app/thermo-node/src/ble.c:328`](../app/thermo-node/src/ble.c#L328) (ほか 1 行)

### sys_put_le32

```c
static inline void sys_put_le32(uint32_t val, uint8_t dst[4]);
```

ホストのバイトオーダーの 32 ビットの整数を、リトルエンディアンで置く。アラインメントされていないメモリでもよい。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `val` | in | ホストのバイトオーダーの 32 ビットの整数 |
| `dst` | out | 結果を置く場所 |

使用箇所: [`app/thermo-gateway/src/ble.c:522`](../app/thermo-gateway/src/ble.c#L522), [`app/thermo-node/src/backlog.c:497`](../app/thermo-node/src/backlog.c#L497) (ほか 1 行)

## CRC

### crc32_ieee

```c
uint32_t crc32_ieee(const uint8_t *data, size_t len);
```

IEEE 準拠の CRC32 を計算する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `data` | in | 計算するデータ |
| `len` | in | データの長さ |

戻り値: CRC32 の値。

使用箇所: [`app/thermo-node/src/backlog.c:470`](../app/thermo-node/src/backlog.c#L470)


### crc32_ieee_update

```c
uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t len);
```

IEEE 準拠の CRC32 を、前の結果に続けて計算する。離れた場所にあるデータを、続けて 1 つの CRC にするときに使う。`crc32_ieee(data, len)` は、`crc32_ieee_update(0, data, len)` と同じ (zlib の `crc32(data, crc)` と同じ値)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `crc` | in | 前のデータの CRC32 (最初は 0) |
| `data` | in | 続けて計算するデータ |
| `len` | in | データの長さ |

戻り値: `crc` のデータに `data` を続けたものの CRC32 の値。

使用箇所: [`app/thermo-node/src/backlog.c:473`](../app/thermo-node/src/backlog.c#L473)
## リングバッファ

### RING_BUF_INIT

```c
#define RING_BUF_INIT(buf, size8)
```

バイト単位のリングバッファ (`struct ring_buf`) の、初期化子 (静的な変数の初期値) を作る。領域は、呼び出し側が用意する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `buf` | in | データを入れる領域 (`uint8_t` の配列) |
| `size8` | in | 領域の大きさ [バイト] |

注意:

- 同じ働きの `RING_BUF_DECLARE(name, size8)` は、リングバッファを外部結合 (`static` なし) で定義するので、GCC 14 以降の `-Wmissing-variable-declarations` に掛かる。このプロジェクトでは、領域とリングバッファを `static` で定義して、`RING_BUF_INIT` で初期化する。

使用箇所: [`app/thermo-node/src/sdlog.c:65`](../app/thermo-node/src/sdlog.c#L65) (ほか 1 行)

### ring_buf_put

```c
uint32_t ring_buf_put(struct ring_buf *buf, const uint8_t *data, uint32_t size);
```

リングバッファに、データを書く (コピーする)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `buf` | in,out | リングバッファ |
| `data` | in | 書くデータ |
| `size` | in | 書くバイト数 |

戻り値: 書いたバイト数 (空きが足りなければ、`size` より少ない)。

注意:

- 複数の書き手が、同時に書かないようにする (プリエンプトを防ぐか、ミューテックスで守る)。
- バイト単位の操作と、アイテム単位 (`ring_buf_item_*`) の操作は混ぜない。

使用箇所: [`app/thermo-node/src/sdlog.c:223`](../app/thermo-node/src/sdlog.c#L223)

### ring_buf_get

```c
uint32_t ring_buf_get(struct ring_buf *buf, uint8_t *data, uint32_t size);
```

リングバッファから、データを読む。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `buf` | in,out | リングバッファ |
| `data` | out | 読んだデータを入れるバッファ。`NULL` なら、データを捨てる |
| `size` | in | 読むバイト数 |

戻り値: 出力のバッファに書いたバイト数。

注意:

- 複数の読み手が、同時に読まないようにする。

使用箇所: [`app/thermo-node/src/sdlog.c:258`](../app/thermo-node/src/sdlog.c#L258)

### ring_buf_is_empty

```c
static inline bool ring_buf_is_empty(const struct ring_buf *buf);
```

リングバッファが空かを調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `buf` | in | リングバッファ |

戻り値: 空なら `true`、そうでなければ `false`。

使用箇所: [`app/thermo-node/src/sdlog.c:257`](../app/thermo-node/src/sdlog.c#L257) (ほか 2 行)

## 時刻と乱数

### sys_clock_gettime

```c
int sys_clock_gettime(int clock_id, struct timespec *tp);
```

指定の時計から、今の時刻を取得する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `clock_id` | in | 時計 (`SYS_CLOCK_REALTIME` など) |
| `tp` | out | 時刻を入れる領域 |

戻り値:

- 成功は 0。
- `-EINVAL`: `clock_id` が不正。

使用箇所: [`app/common/ntp.c:119`](../app/common/ntp.c#L119)

### sys_clock_settime

```c
int sys_clock_settime(int clock_id, const struct timespec *tp);
```

指定の時計の時刻を設定する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `clock_id` | in | 時計 |
| `tp` | in | 設定する時刻 |

戻り値:

- 成功は 0。
- `-EINVAL`: `clock_id` が不正、または `tp` のナノ秒が `[0, 999999999]` の外。

使用箇所: [`app/common/ntp.c:85`](../app/common/ntp.c#L85)

### sys_rand32_get

```c
static inline uint32_t sys_rand32_get(void);
```

一般的な乱数のテストに通る、32 ビットの乱数を返す。

戻り値: 32 ビットの乱数。

注意:

- 暗号に使える乱数ではない。

使用箇所: [`app/thermo-node/src/sensor.c:212`](../app/thermo-node/src/sensor.c#L212) (ほか 1 行)

## ユーティリティ

### bcd2bin

```c
static inline uint8_t bcd2bin(uint8_t bcd);
```

BCD (8421) の値を、2 進数にする。PCF8563 のレジスタの読み出しに使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `bcd` | in | BCD の値 |

戻り値: 2 進数の値。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:191`](../app/thermo-node/drivers/rtc_pcf8563.c#L191) (ほか 5 行)

### bin2bcd

```c
static inline uint8_t bin2bcd(uint8_t bin);
```

2 進数の値を、BCD (8421) にする。PCF8563 のレジスタへの書き込みに使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `bin` | in | 2 進数の値 |

戻り値: BCD の値。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:138`](../app/thermo-node/drivers/rtc_pcf8563.c#L138) (ほか 5 行)

### ARRAY_SIZE

```c
#define ARRAY_SIZE(array)
```

配列の要素の数を返す。配列でない引数 (ポインタなど) は、コンパイルエラーにする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `array` | in | 配列 |

戻り値: 要素の数。

使用箇所: [`app/common/cfg.c:103`](../app/common/cfg.c#L103) (ほか 5 行), [`app/common/cloud.c:552`](../app/common/cloud.c#L552), [`app/thermo-gateway/src/ble.c:272`](../app/thermo-gateway/src/ble.c#L272) (ほか 1 行), [`app/thermo-gateway/src/switchbot.c:207`](../app/thermo-gateway/src/switchbot.c#L207) ほか 3 ファイル

### MIN

```c
#define MIN(a, b)
```

2 つの値の小さい方を返す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `a` | in | 比べる値 |
| `b` | in | 比べる値 |

戻り値: 小さい方の値。

使用箇所: [`app/common/cloud.c:206`](../app/common/cloud.c#L206), [`app/thermo-node/src/backlog.c:141`](../app/thermo-node/src/backlog.c#L141), [`app/thermo-node/src/history.c:65`](../app/thermo-node/src/history.c#L65) (ほか 1 行)

### BIT

```c
#define BIT(n)
```

ビット `n` だけが立った値を返す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `n` | in | ビットの位置 (0 から) |

戻り値: ビット `n` だけが 1 の値。

使用箇所: [`app/thermo-node/src/sensor.c:190`](../app/thermo-node/src/sensor.c#L190)

### CONTAINER_OF

```c
#define CONTAINER_OF(ptr, type, field)
```

構造体のメンバーへのポインタから、構造体の先頭へのポインタを得る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `ptr` | in | メンバーへのポインタ |
| `type` | in | 構造体の型 |
| `field` | in | 構造体の中のメンバーの名前 |

戻り値: 構造体の先頭へのポインタ。

使用箇所: [`app/thermo-gateway/src/ble.c:399`](../app/thermo-gateway/src/ble.c#L399) (ほか 1 行)

### ARG_UNUSED

```c
#define ARG_UNUSED(x)
```

使わない引数や変数の、未使用の警告を出さないようにする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `x` | in | 使わない引数や変数 |

使用箇所: [`app/common/cfg_shell.c:56`](../app/common/cfg_shell.c#L56) (ほか 10 行), [`app/common/cloud.c:442`](../app/common/cloud.c#L442) (ほか 3 行), [`app/common/wifi_link.c:161`](../app/common/wifi_link.c#L161), [`app/thermo-gateway/src/ble.c:548`](../app/thermo-gateway/src/ble.c#L548) (ほか 3 行) ほか 7 ファイル

### BUILD_ASSERT

```c
#define BUILD_ASSERT(EXPR, MSG...)
```

コンパイル時の表明。`EXPR` が偽なら、コンパイルエラーにする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `EXPR` | in | コンパイル時に決まる条件 |
| `MSG` | in | エラーのメッセージ (省略できる) |

使用箇所: [`app/common/cloud.c:60`](../app/common/cloud.c#L60)
