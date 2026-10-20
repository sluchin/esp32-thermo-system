# デバイスとドライバ

デバイスの確認 (`zephyr/device.h`) と、ドライバ API (`zephyr/drivers/adc.h` `gpio.h` `i2c.h` `pwm.h` `rtc.h` `sensor.h`)、Devicetree のマクロ (`zephyr/devicetree.h`) の、使っている API。ハードウェアは、Devicetree から得て、使う前に `device_is_ready()` か、`*_is_ready_dt()` で確かめる。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## デバイス

### device_is_ready

```c
bool device_is_ready(const struct device *dev);
```

デバイスが使える状態かを調べる。`DEVICE_DT_GET()` で得たポインタは、`device_get_binding()` の準備の確認を含まないので、これで確かめる。少なくとも、デバイスの初期化が成功していることを確かめる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | 確かめるデバイス |

戻り値: 使えれば `true`。使えない、または `NULL` を渡したら `false`。

使用箇所: [`app/thermo-node/src/node_time.c:84`](../app/thermo-node/src/node_time.c#L84), [`app/thermo-node/src/oled.c:137`](../app/thermo-node/src/oled.c#L137), [`app/thermo-node/src/sensor.c:94`](../app/thermo-node/src/sensor.c#L94) (ほか 1 行)

## ADC

### adc_is_ready_dt

```c
static inline bool adc_is_ready_dt(const struct adc_dt_spec *spec);
```

Devicetree から得た ADC のデバイスが使えるかを調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た ADC の仕様 |

戻り値: 使えれば `true`、使えなければ `false`。

使用箇所: [`app/thermo-node/src/sensor.c:110`](../app/thermo-node/src/sensor.c#L110)

### adc_channel_setup_dt

```c
static inline int adc_channel_setup_dt(const struct adc_dt_spec *spec);
```

`struct adc_dt_spec` から、ADC のチャンネルを設定する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た ADC の仕様 |

戻り値: `adc_channel_setup()` の値。Devicetree の情報が正しくなければ `-ENOTSUP`。

使用箇所: [`app/thermo-node/src/sensor.c:116`](../app/thermo-node/src/sensor.c#L116)

### adc_read_dt

```c
static inline int adc_read_dt(const struct adc_dt_spec *spec, const struct adc_sequence *sequence);
```

`struct adc_dt_spec` から、読み取りを要求する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た ADC の仕様 |
| `sequence` | in | 要求するサンプリングの手順 (結果を入れるバッファの指定を含む) |

戻り値: `adc_read()` の値。

使用箇所: [`app/thermo-node/src/sensor.c:194`](../app/thermo-node/src/sensor.c#L194)

### adc_raw_to_millivolts_dt

```c
static inline int adc_raw_to_millivolts_dt(const struct adc_dt_spec *spec, int32_t *valp);
```

ADC の生の値を、`struct adc_dt_spec` の情報を使って、ミリボルトにする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た ADC の仕様 |
| `valp` | in,out | 入力は生の値。成功すると、ミリボルトの値になる。失敗したら、値は変わらない |

戻り値: `adc_raw_to_millivolts()` の値。Devicetree の情報が正しくなければ `-ENOTSUP`。

使用箇所: [`app/thermo-node/src/sensor.c:202`](../app/thermo-node/src/sensor.c#L202)

## GPIO

### gpio_is_ready_dt

```c
static inline bool gpio_is_ready_dt(const struct gpio_dt_spec *spec);
```

GPIO のポートが使えるかを調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た GPIO の仕様 |

戻り値: 使えれば `true`、使えなければ `false`。

使用箇所: [`app/thermo-node/src/button.c:41`](../app/thermo-node/src/button.c#L41)

### gpio_pin_configure_dt

```c
static inline int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t extra_flags);
```

`struct gpio_dt_spec` のピンを、Devicetree のフラグに追加のフラグを足して、設定する。`gpio_pin_configure(spec->port, spec->pin, spec->dt_flags | extra_flags)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た GPIO の仕様 |
| `extra_flags` | in | 追加のフラグ (入力は `GPIO_INPUT`) |

戻り値: `gpio_pin_configure()` の値。

使用箇所: [`app/thermo-node/src/button.c:46`](../app/thermo-node/src/button.c#L46)

### gpio_pin_get_dt

```c
static inline int gpio_pin_get_dt(const struct gpio_dt_spec *spec);
```

入力ピンの論理レベルを返す。`gpio_pin_get(spec->port, spec->pin)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た GPIO の仕様 |

戻り値: `gpio_pin_get()` の値 (論理レベルが 1 なら正、0 なら 0、失敗なら負)。

使用箇所: [`app/thermo-node/src/button.c:53`](../app/thermo-node/src/button.c#L53) (ほか 1 行)

## I2C

### i2c_is_ready_dt

```c
static inline bool i2c_is_ready_dt(const struct i2c_dt_spec *spec);
```

I2C のバスが使えるかを調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た I2C の仕様 |

戻り値: 使えれば `true`、使えなければ `false`。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:233`](../app/thermo-node/drivers/rtc_pcf8563.c#L233)

### i2c_burst_read_dt

```c
static inline int i2c_burst_read_dt(const struct i2c_dt_spec *spec, uint8_t start_addr, uint8_t *buf, uint32_t num_bytes);
```

I2C デバイスの内部アドレスから、複数バイトを読む。`i2c_burst_read(spec->bus, spec->addr, start_addr, buf, num_bytes)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た I2C の仕様 |
| `start_addr` | in | 読み始める内部アドレス |
| `buf` | out | 読んだデータを入れる領域 |
| `num_bytes` | in | 読むバイト数 |

戻り値: `i2c_burst_read()` の値。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:180`](../app/thermo-node/drivers/rtc_pcf8563.c#L180)

### i2c_burst_write_dt

```c
static inline int i2c_burst_write_dt(const struct i2c_dt_spec *spec, uint8_t start_addr, const uint8_t *buf, uint32_t num_bytes);
```

I2C デバイスの内部アドレスへ、複数バイトを書く。`i2c_burst_write(spec->bus, spec->addr, start_addr, buf, num_bytes)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た I2C の仕様 |
| `start_addr` | in | 書き始める内部アドレス |
| `buf` | in | 書くデータ |
| `num_bytes` | in | 書くバイト数 |

戻り値: `i2c_burst_write()` の値。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:152`](../app/thermo-node/drivers/rtc_pcf8563.c#L152)

### i2c_reg_write_byte_dt

```c
static inline int i2c_reg_write_byte_dt(const struct i2c_dt_spec *spec, uint8_t reg_addr, uint8_t value);
```

I2C デバイスの内部レジスタに、1 バイトを書く。`i2c_reg_write_byte(spec->bus, spec->addr, reg_addr, value)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た I2C の仕様 |
| `reg_addr` | in | 書き込むレジスタの内部アドレス |
| `value` | in | 書き込む値 |

戻り値: `i2c_reg_write_byte()` の値。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:146`](../app/thermo-node/drivers/rtc_pcf8563.c#L146)

## PWM

### pwm_is_ready_dt

```c
static inline bool pwm_is_ready_dt(const struct pwm_dt_spec *spec);
```

PWM のデバイスが使えるかを調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た PWM の仕様 |

戻り値: 使えれば `true`、使えなければ `false`。

使用箇所: [`app/thermo-node/src/buzzer.c:49`](../app/thermo-node/src/buzzer.c#L49)

### pwm_set_dt

```c
static inline int pwm_set_dt(const struct pwm_dt_spec *spec, uint32_t period, uint32_t pulse);
```

周期とパルス幅を、ナノ秒で設定する。`spec` に書いてある周期は無視される。Devicetree の周期を、実行中に変えたいときに使う (ブザーの音階)。`pwm_set(spec->dev, spec->channel, period, pulse, spec->flags)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た PWM の仕様 |
| `period` | in | 周期 [ns] |
| `pulse` | in | パルス幅 [ns]。0 にすると、出力が止まる |

戻り値: `pwm_set()` の値。

使用箇所: [`app/thermo-node/src/buzzer.c:136`](../app/thermo-node/src/buzzer.c#L136)

### pwm_set_pulse_dt

```c
static inline int pwm_set_pulse_dt(const struct pwm_dt_spec *spec, uint32_t pulse);
```

パルス幅をナノ秒で設定する。周期は、`spec` の値を使う。`pwm_set(spec->dev, spec->channel, spec->period, pulse, spec->flags)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た PWM の仕様 |
| `pulse` | in | パルス幅 [ns] |

戻り値: `pwm_set()` の値。

使用箇所: [`app/thermo-node/src/buzzer.c:131`](../app/thermo-node/src/buzzer.c#L131)

## RTC

### rtc_get_time

```c
int rtc_get_time(const struct device *dev, struct rtc_time *timeptr);
```

RTC の時刻を取得する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | RTC のデバイス |
| `timeptr` | out | 時刻を入れる領域。`struct rtc_time` の月は 0 から 11、年は 1900 年からの年数 |

戻り値:

- 成功は 0。
- `-ENODATA`: RTC に時刻が設定されていない。
- ほかの失敗は負の errno。

使用箇所: [`app/thermo-node/src/node_time.c:225`](../app/thermo-node/src/node_time.c#L225)

### rtc_set_time

```c
int rtc_set_time(const struct device *dev, const struct rtc_time *timeptr);
```

RTC の時刻を設定する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | RTC のデバイス |
| `timeptr` | in | 設定する時刻 |

戻り値:

- 成功は 0。
- `-EINVAL`: 時刻が不正、または、ハードウェアの範囲を超えている。
- ほかの失敗は負の errno。

使用箇所: [`app/thermo-node/src/node_time.c:200`](../app/thermo-node/src/node_time.c#L200)

## センサ

### sensor_sample_fetch

```c
int sensor_sample_fetch(const struct device *dev);
```

センサから、サンプルを取得して、ドライバの内部のバッファに入れる。有効な全てのチャンネルを読み、必要なら、値を使える形にする。取得が終わるまで待つ。値は、続けて `sensor_channel_get()` で取り出す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | センサのデバイス |

戻り値: 成功は 0、失敗は負の errno。

注意:

- I2C や SPI で接続したセンサは、通信をするので、割り込みから呼ばない。

使用箇所: [`app/thermo-node/src/sensor.c:153`](../app/thermo-node/src/sensor.c#L153) (ほか 1 行)

### sensor_channel_get

```c
int sensor_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val);
```

指定のチャンネルの値を、ドライバの内部のデータから取り出す。先に `sensor_sample_fetch()` (または `sensor_sample_fetch_chan()`) でサンプルを取得しておく。間に取得し直さなければ、続けて呼んでも、同じ値になる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | センサのデバイス |
| `chan` | in | チャンネル (`SENSOR_CHAN_AMBIENT_TEMP` `SENSOR_CHAN_HUMIDITY` `SENSOR_CHAN_PRESS`) |
| `val` | out | 値を入れる領域 (`val1` が整数部、`val2` が小数部 [百万分の 1]) |

戻り値: 成功は 0、失敗は負の errno。センサにないチャンネルは、`-ENOTSUP` (ドライバによる)。

チャンネルの単位 (このプロジェクトで使うもの):

| チャンネル | 単位 | 例 |
|:---|:---|:---|
| `SENSOR_CHAN_AMBIENT_TEMP` | ℃ | 23.5 ℃ は `val1 = 23`、`val2 = 500000` |
| `SENSOR_CHAN_HUMIDITY` | % (相対湿度) | 45 % は `val1 = 45`、`val2 = 0` |
| `SENSOR_CHAN_PRESS` | **kPa** (hPa ではない) | 1013.2 hPa (101.32 kPa) は `val1 = 101`、`val2 = 320000`。`sensor_value_to_milli()` で Pa (101320) になる |

使用箇所: [`app/thermo-node/src/sensor.c:159`](../app/thermo-node/src/sensor.c#L159) (ほか 1 行), [`app/thermo-node/src/sensor.c:244`](../app/thermo-node/src/sensor.c#L244) (気圧)

### sensor_value_to_milli

```c
static inline int64_t sensor_value_to_milli(const struct sensor_value *val);
```

`struct sensor_value` を、1000 倍の整数 (ミリ単位) にする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `val` | in | 変換する値 |

戻り値: 変換した値。

使用箇所: [`app/thermo-node/src/sensor.c:164`](../app/thermo-node/src/sensor.c#L164) (ほか 2 行)

## Devicetree

### DEVICE_DT_GET

```c
#define DEVICE_DT_GET(node_id)
```

Devicetree のノードから、デバイスへのポインタを得る (コンパイル時に決まる)。使う前に、`device_is_ready()` で確かめる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |

戻り値: デバイスへのポインタ。

使用箇所: [`app/thermo-node/src/node_time.c:61`](../app/thermo-node/src/node_time.c#L61), [`app/thermo-node/src/oled.c:98`](../app/thermo-node/src/oled.c#L98), [`app/thermo-node/src/sensor.c:47`](../app/thermo-node/src/sensor.c#L47) (ほか 1 行)

### DT_ALIAS

```c
#define DT_ALIAS(alias)
```

Devicetree の alias から、ノードの識別子を得る (例: `DT_ALIAS(rtc0)`)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `alias` | in | alias の名前 (小文字、アンダースコア区切り) |

戻り値: ノードの識別子。

使用箇所: [`app/thermo-node/src/button.c:25`](../app/thermo-node/src/button.c#L25), [`app/thermo-node/src/buzzer.c:25`](../app/thermo-node/src/buzzer.c#L25), [`app/thermo-node/src/node_time.c:61`](../app/thermo-node/src/node_time.c#L61), [`app/thermo-node/src/oled.c:98`](../app/thermo-node/src/oled.c#L98) ほか 1 ファイル

### DT_HAS_ALIAS

```c
#define DT_HAS_ALIAS(alias)
```

alias があって、ノードが有効なら 1、なければ 0 になる。`#if` や `COND_CODE_1` で、オプションの部品の有無を切り替えるのに使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `alias` | in | alias の名前 |

戻り値: 1 または 0。

使用箇所: [`app/thermo-node/src/sensor.c:25`](../app/thermo-node/src/sensor.c#L25) (ほか 1 行)

### DT_PATH

```c
#define DT_PATH(...)
```

ノードのパスから、ノードの識別子を得る (例: `DT_PATH(zephyr_user)`)。ルート `/` から、ノードの名前を、順に引数に並べる。得た識別子は、`DT_PROP()` `DT_NODE_HAS_PROP()` `ADC_DT_SPEC_GET()` などの、ほかの `DT_*` のマクロに渡して使う。コンパイルのときに決まる値で、実行時の処理はない。

ノードの名前は、小文字にして、C の識別子に使えない文字 (`,` `-` `@`) を `_` に置き換えて書く。ユニットアドレス (`@` の後ろ) も、名前に含める。

| パス | 書き方 |
|:---|:---|
| `/zephyr,user` | `DT_PATH(zephyr_user)` |
| `/soc/i2c@12340000` | `DT_PATH(soc, i2c_12340000)` |
| `/aliases` | `DT_PATH(aliases)` |

`,` と `_` は、どちらも `_` になるので、`zephyr,user` と `zephyr_user` という名前のノードは、同じ書き方になる。Devicetree の上では別のノードなので、overlay では、ノードの名前を `zephyr,user` にそろえる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `...` | in | ルートからのノード名の並び (小文字にして、`,` `-` `@` を `_` に置き換えたもの) |

戻り値: ノードの識別子。パスにノードがなければ、ノードの識別子として使えない値になり、ほかの `DT_*` のマクロに渡すと、コンパイルのエラーになる (`DT_NODE_EXISTS()` と `DT_NODE_HAS_PROP()` は、0 になる)。

#### zephyr,user ノード

`/zephyr,user` は、アプリのための特別なノード。binding (プロパティの書式の定義) を書かなくても、任意のプロパティを置ける (プロパティの型は、値から推定される)。ボードごとに変えたい値 (ADC のチャンネル、GPIO のピン、数値など) を、overlay に書いて、アプリから読むのに使う。Zephyr のドライバやサブシステムではなく、アプリとサンプルのためのもの。

このプロジェクトでは、ノードの温度センサの ADC のチャンネル (`io-channels`) を置く。`io-channels` があれば ADC を読み、なければ、ほかの方法 (センサ API か、シミュレーション) にする ([`app/thermo-node/src/sensor.c:33`](../app/thermo-node/src/sensor.c#L33))。

```dts
/ {
    zephyr,user {
        io-channels = <&adc0 0>; /* adc0 のチャンネル 0 */
    };
};
```

```c
#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#endif
```

使用箇所: [`app/thermo-node/src/sensor.c:33`](../app/thermo-node/src/sensor.c#L33) (ほか 1 行)

### DT_NODE_HAS_PROP

```c
#define DT_NODE_HAS_PROP(node_id, prop)
```

ノードが、そのプロパティを持っていれば 1、なければ 0 になる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |
| `prop` | in | プロパティの名前 (小文字、アンダースコア区切り) |

戻り値: 1 または 0。

使用箇所: [`app/thermo-node/src/sensor.c:33`](../app/thermo-node/src/sensor.c#L33)

### ADC_DT_SPEC_GET

```c
#define ADC_DT_SPEC_GET(node_id)
```

ノードの `io-channels` の最初のチャンネルから、`struct adc_dt_spec` の初期化子を作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |

使用箇所: [`app/thermo-node/src/sensor.c:64`](../app/thermo-node/src/sensor.c#L64)

### GPIO_DT_SPEC_GET

```c
#define GPIO_DT_SPEC_GET(node_id, prop)
```

ノードのプロパティ (例: `gpios`) から、`struct gpio_dt_spec` の初期化子を作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |
| `prop` | in | GPIO を指定するプロパティの名前 |

使用箇所: [`app/thermo-node/src/button.c:25`](../app/thermo-node/src/button.c#L25)

### PWM_DT_SPEC_GET

```c
#define PWM_DT_SPEC_GET(node_id)
```

ノードの `pwms` の最初の要素から、`struct pwm_dt_spec` の初期化子を作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |

使用箇所: [`app/thermo-node/src/buzzer.c:25`](../app/thermo-node/src/buzzer.c#L25)

### I2C_DT_SPEC_INST_GET

```c
#define I2C_DT_SPEC_INST_GET(inst)
```

ドライバのインスタンスの番号から、`struct i2c_dt_spec` (バスとアドレス) の初期化子を作る。`drivers/rtc_pcf8563.c` で使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `inst` | in | インスタンスの番号 |

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:263`](../app/thermo-node/drivers/rtc_pcf8563.c#L263)

### DEVICE_DT_INST_DEFINE

```c
#define DEVICE_DT_INST_DEFINE(inst, init_fn, pm, data, config, level, prio, api)
```

ドライバのインスタンスの番号から、デバイスを定義する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `inst` | in | インスタンスの番号 |
| `init_fn` | in | 初期化の関数 |
| `pm` | in | 電源管理のデバイス (使わなければ `NULL`) |
| `data` | in | デバイスのデータ |
| `config` | in | デバイスの設定 |
| `level` | in | 初期化の段階 |
| `prio` | in | 段階の中の優先度 |
| `api` | in | API の構造体 |

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:265`](../app/thermo-node/drivers/rtc_pcf8563.c#L265)

### DT_INST_FOREACH_STATUS_OKAY

```c
#define DT_INST_FOREACH_STATUS_OKAY(fn)
```

`status = "okay"` の全てのインスタンスについて、`fn(inst)` を展開する。ドライバの末尾で、`DEVICE_DT_INST_DEFINE()` を呼ぶマクロを渡す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `fn` | in | インスタンスの番号を引数に取るマクロ |

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:275`](../app/thermo-node/drivers/rtc_pcf8563.c#L275)

### DEVICE_API

```c
#define DEVICE_API(_class, _name)
```

デバイスの API の構造体を、`static const struct <_class>_driver_api _name` として定義する。ドライバ (`drivers/rtc_pcf8563.c`) の API の表に使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_class` | in | デバイスの種類 (例: `rtc`) |
| `_name` | in | 構造体の名前 |

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:102`](../app/thermo-node/drivers/rtc_pcf8563.c#L102)
