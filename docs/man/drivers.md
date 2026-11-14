# デバイスとドライバ

デバイスの確認 (`zephyr/device.h`)、ドライバ API (`zephyr/drivers/adc.h` `gpio.h` `i2c.h` `pwm.h` `rtc.h` `sensor.h`)、Devicetree のマクロ (`zephyr/devicetree.h`) のうち、使っている API。ハードウェアは Devicetree から得て、使う前に `device_is_ready()` か `*_is_ready_dt()` で確かめる。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## デバイス

### device_is_ready

```c
bool device_is_ready(const struct device *dev);
```

デバイスが使える状態かを調べる。`DEVICE_DT_GET()` で得たポインタでは、`device_get_binding()` が行う準備の確認が済んでいないので、これで確かめる。少なくとも、デバイスの初期化が成功していることを確かめる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | 確かめるデバイス |

戻り値: 使えれば `true`。使えないときと、`NULL` を渡したときは `false`。エラー番号は返さない。

使用箇所: [`app/thermo-node/src/node_time.c:84`](../../app/thermo-node/src/node_time.c#L84), [`app/thermo-node/src/oled.c:137`](../../app/thermo-node/src/oled.c#L137), [`app/thermo-node/src/sensor.c:94`](../../app/thermo-node/src/sensor.c#L94) (ほか 1 行)

## ADC

### adc_is_ready_dt

```c
static inline bool adc_is_ready_dt(const struct adc_dt_spec *spec);
```

Devicetree から得た ADC のデバイスが使えるかを調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た ADC の仕様 |

戻り値: 使えれば `true`、使えなければ `false`。エラー番号は返さない。

使用箇所: [`app/thermo-node/src/sensor.c:110`](../../app/thermo-node/src/sensor.c#L110)

### adc_channel_setup_dt

```c
static inline int adc_channel_setup_dt(const struct adc_dt_spec *spec);
```

`struct adc_dt_spec` の情報で、ADC のチャンネルを設定する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た ADC の仕様 |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は、ドライバが返す負の errno。

エラー:

- `EINVAL`: 値が不正な引数を渡した (`adc_channel_setup()` の値)。
- `ENOTSUP`: Devicetree の情報が正しくない。

使用箇所: [`app/thermo-node/src/sensor.c:116`](../../app/thermo-node/src/sensor.c#L116)

### adc_read_dt

```c
static inline int adc_read_dt(const struct adc_dt_spec *spec, const struct adc_sequence *sequence);
```

`struct adc_dt_spec` の情報で、読み取りを要求する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た ADC の仕様 |
| `sequence` | in | 要求するサンプリングの手順 (結果を入れるバッファの指定を含む) |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は、ドライバが返す負の errno。

エラー:

- `EINVAL`: 値が不正な引数を渡した。
- `ENOMEM`: 要求した全ての標本を入れるには、バッファが小さい。
- `ENOTSUP`: 要求した動作のモードに対応していない。
- `EBUSY`: 前の標本の取得が終わらないうちに、次を始めた (間隔を空けて測るときだけ起きる。間隔が短すぎる)。全ての標本はバッファに入るが、一部は予定より遅れて取ったもの。

使用箇所: [`app/thermo-node/src/sensor.c:194`](../../app/thermo-node/src/sensor.c#L194)

### adc_raw_to_millivolts_dt

```c
static inline int adc_raw_to_millivolts_dt(const struct adc_dt_spec *spec, int32_t *valp);
```

`struct adc_dt_spec` の情報を使って、ADC の生の値をミリボルトにする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た ADC の仕様 |
| `valp` | in,out | 入力は生の値。成功すると、ミリボルトの値になる。失敗したら、値は変わらない |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。

エラー:

- `ENOTSUP`: Devicetree の情報が正しくない。または、利得や基準電圧の設定のために換算できない。

使用箇所: [`app/thermo-node/src/sensor.c:202`](../../app/thermo-node/src/sensor.c#L202)

## GPIO

### gpio_is_ready_dt

```c
static inline bool gpio_is_ready_dt(const struct gpio_dt_spec *spec);
```

GPIO のポートが使えるかを調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た GPIO の仕様 |

戻り値: 使えれば `true`、使えなければ `false`。エラー番号は返さない。

使用箇所: [`app/thermo-node/src/button.c:41`](../../app/thermo-node/src/button.c#L41)

### gpio_pin_configure_dt

```c
static inline int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t extra_flags);
```

`struct gpio_dt_spec` のピンを、Devicetree のフラグに追加のフラグを足して設定する。`gpio_pin_configure(spec->port, spec->pin, spec->dt_flags | extra_flags)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た GPIO の仕様 |
| `extra_flags` | in | 追加のフラグ (入力は `GPIO_INPUT`) |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。

エラー:

- `ENOTSUP`: 設定のオプションのどれかに対応していない (フラグの説明に別の指示があるときを除く)。
- `EINVAL`: 引数が不正。
- `EIO`: 外付けの GPIO チップへのアクセスで I/O エラーが起きた。
- `EWOULDBLOCK`: 操作がブロックする。

使用箇所: [`app/thermo-node/src/button.c:46`](../../app/thermo-node/src/button.c#L46)

### gpio_pin_get_dt

```c
static inline int gpio_pin_get_dt(const struct gpio_dt_spec *spec);
```

入力ピンの論理レベルを返す。`gpio_pin_get(spec->port, spec->pin)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た GPIO の仕様 |

戻り値:

- ピンの論理レベルが 1 (アクティブ) なら 1。
- 論理レベルが 0 (非アクティブ) なら 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。

エラー:

- `EIO`: 外付けの GPIO チップへのアクセスで I/O エラーが起きた。
- `EWOULDBLOCK`: 操作がブロックする。

使用箇所: [`app/thermo-node/src/button.c:53`](../../app/thermo-node/src/button.c#L53) (ほか 1 行)

## I2C

### i2c_is_ready_dt

```c
static inline bool i2c_is_ready_dt(const struct i2c_dt_spec *spec);
```

I2C のバスが使えるかを調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た I2C の仕様 |

戻り値: 使えれば `true`、使えなければ `false`。エラー番号は返さない。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:230`](../../app/thermo-node/drivers/rtc_pcf8563.c#L230)

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

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほか、ドライバごとの負の errno (`-ETIMEDOUT`、`-EBUSY` など)。

エラー:

- `EIO`: 通信の I/O エラー (相手が ACK を返さない、バスの失敗など)。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:178`](../../app/thermo-node/drivers/rtc_pcf8563.c#L178)

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

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほか、ドライバごとの負の errno (`-ETIMEDOUT`、`-EBUSY` など)。

エラー:

- `EIO`: 通信の I/O エラー (相手が ACK を返さない、バスの失敗など)。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:151`](../../app/thermo-node/drivers/rtc_pcf8563.c#L151)

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

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほか、ドライバごとの負の errno (`-ETIMEDOUT`、`-EBUSY` など)。

エラー:

- `EIO`: 通信の I/O エラー (相手が ACK を返さない、バスの失敗など)。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:145`](../../app/thermo-node/drivers/rtc_pcf8563.c#L145)

## PWM

### pwm_is_ready_dt

```c
static inline bool pwm_is_ready_dt(const struct pwm_dt_spec *spec);
```

PWM のデバイスが使えるかを調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た PWM の仕様 |

戻り値: 使えれば `true`、使えなければ `false`。エラー番号は返さない。

使用箇所: [`app/thermo-node/src/buzzer.c:49`](../../app/thermo-node/src/buzzer.c#L49)

### pwm_set_dt

```c
static inline int pwm_set_dt(const struct pwm_dt_spec *spec, uint32_t period, uint32_t pulse);
```

周期とパルス幅を、ナノ秒で設定する。`spec` に書いてある周期は無視される。Devicetree の周期を実行中に変えたいとき (ブザーの音階) に使う。`pwm_set(spec->dev, spec->channel, period, pulse, spec->flags)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た PWM の仕様 |
| `period` | in | 周期 [ns] |
| `pulse` | in | パルス幅 [ns]。0 にすると、出力が止まる |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は、負の errno。

エラー:

- `ENOTSUP`: 要求した周期かパルスのサイクルに対応していない。

使用箇所: [`app/thermo-node/src/buzzer.c:134`](../../app/thermo-node/src/buzzer.c#L134)

### pwm_set_pulse_dt

```c
static inline int pwm_set_pulse_dt(const struct pwm_dt_spec *spec, uint32_t pulse);
```

パルス幅をナノ秒で設定する。周期には `spec` の値を使う。`pwm_set(spec->dev, spec->channel, spec->period, pulse, spec->flags)` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `spec` | in | Devicetree から得た PWM の仕様 |
| `pulse` | in | パルス幅 [ns] |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は、負の errno。

エラー:

- `ENOTSUP`: 要求した周期かパルスのサイクルに対応していない。

使用箇所: [`app/thermo-node/src/buzzer.c:129`](../../app/thermo-node/src/buzzer.c#L129)

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
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は負の errno。

エラー:

- `ENODATA`: RTC に時刻が設定されていない。

使用箇所: [`app/thermo-node/src/node_time.c:221`](../../app/thermo-node/src/node_time.c#L221)

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
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は負の errno。

エラー:

- `EINVAL`: 時刻が不正か、ハードウェアの範囲を超えている。

使用箇所: [`app/thermo-node/src/node_time.c:197`](../../app/thermo-node/src/node_time.c#L197)

## センサ

### sensor_sample_fetch

```c
int sensor_sample_fetch(const struct device *dev);
```

センサからサンプルを取得して、ドライバの内部のバッファに入れる。有効な全てのチャンネルを読み、必要なら値を使える形にする。取得が終わるまで待つ。値は、続けて `sensor_channel_get()` で取り出す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | センサのデバイス |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は、ドライバが返す負の errno。

エラー:

- `EIO`: センサとの通信 (I2C、SPI など) の失敗。
- `ENOTSUP`: センサがその操作に対応していない。

注意:

- I2C や SPI で接続したセンサは通信をするので、割り込みから呼ばない。

使用箇所: [`app/thermo-node/src/sensor.c:153`](../../app/thermo-node/src/sensor.c#L153) (ほか 1 行)

### sensor_channel_get

```c
int sensor_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val);
```

ドライバの内部のデータから、指定のチャンネルの値を取り出す。先に `sensor_sample_fetch()` (または `sensor_sample_fetch_chan()`) でサンプルを取得しておく。間に取得し直さなければ、続けて呼んでも同じ値になる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | センサのデバイス |
| `chan` | in | チャンネル (`SENSOR_CHAN_AMBIENT_TEMP` `SENSOR_CHAN_HUMIDITY` `SENSOR_CHAN_PRESS`) |
| `val` | out | 値を入れる領域 (`val1` が整数部、`val2` が小数部 [百万分の 1]) |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は、ドライバが返す負の errno。

エラー:

- `ENOTSUP`: センサにないチャンネル (ドライバによる)。
- `EINVAL`: 引数が不正。

チャンネルの単位 (このプロジェクトで使うもの):

| チャンネル | 単位 | 例 |
|:---|:---|:---|
| `SENSOR_CHAN_AMBIENT_TEMP` | ℃ | 23.5 ℃ は `val1 = 23`、`val2 = 500000` |
| `SENSOR_CHAN_HUMIDITY` | % (相対湿度) | 45 % は `val1 = 45`、`val2 = 0` |
| `SENSOR_CHAN_PRESS` | **kPa** (hPa ではない) | 1013.2 hPa (101.32 kPa) は `val1 = 101`、`val2 = 320000`。`sensor_value_to_milli()` で Pa (101320) になる |

使用箇所: [`app/thermo-node/src/sensor.c:159`](../../app/thermo-node/src/sensor.c#L159) (ほか 1 行), [`app/thermo-node/src/sensor.c:244`](../../app/thermo-node/src/sensor.c#L244) (気圧)

### sensor_value_to_milli

```c
static inline int64_t sensor_value_to_milli(const struct sensor_value *val);
```

`struct sensor_value` を 1000 倍の整数 (ミリ単位) にする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `val` | in | 変換する値 |

戻り値: 変換した値。エラー番号は返さない。

使用箇所: [`app/thermo-node/src/sensor.c:164`](../../app/thermo-node/src/sensor.c#L164) (ほか 2 行)

## Devicetree

### DEVICE_DT_GET

```c
#define DEVICE_DT_GET(node_id)
```

Devicetree のノードから、デバイスへのポインタを得る (コンパイル時に決まる)。使う前に `device_is_ready()` で確かめる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |

戻り値: デバイスへのポインタ。

使用箇所: [`app/thermo-node/src/node_time.c:61`](../../app/thermo-node/src/node_time.c#L61), [`app/thermo-node/src/oled.c:98`](../../app/thermo-node/src/oled.c#L98), [`app/thermo-node/src/sensor.c:47`](../../app/thermo-node/src/sensor.c#L47) (ほか 1 行)

### DT_ALIAS

```c
#define DT_ALIAS(alias)
```

Devicetree の alias から、ノードの識別子を得る (例: `DT_ALIAS(rtc0)`)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `alias` | in | alias の名前 (小文字、アンダースコア区切り) |

戻り値: ノードの識別子。

使用箇所: [`app/thermo-node/src/button.c:25`](../../app/thermo-node/src/button.c#L25), [`app/thermo-node/src/buzzer.c:25`](../../app/thermo-node/src/buzzer.c#L25), [`app/thermo-node/src/node_time.c:61`](../../app/thermo-node/src/node_time.c#L61), [`app/thermo-node/src/oled.c:98`](../../app/thermo-node/src/oled.c#L98) ほか 1 ファイル

### DT_HAS_ALIAS

```c
#define DT_HAS_ALIAS(alias)
```

alias があって、ノードが有効なら 1、そうでなければ 0 になる。`#if` や `COND_CODE_1` で、オプションの部品の有無を切り替えるのに使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `alias` | in | alias の名前 |

戻り値: 1 または 0。

使用箇所: [`app/thermo-node/src/sensor.c:25`](../../app/thermo-node/src/sensor.c#L25) (ほか 1 行)

### DT_PATH

```c
#define DT_PATH(...)
```

ノードのパスから、ノードの識別子を得る (例: `DT_PATH(zephyr_user)`)。ルート `/` から、ノードの名前を順に引数に並べる。得た識別子は、`DT_PROP()` `DT_NODE_HAS_PROP()` `ADC_DT_SPEC_GET()` などのほかの `DT_*` のマクロに渡して使う。コンパイルのときに決まる値なので、実行時の処理はない。

ノードの名前は、小文字にして、C の識別子に使えない文字 (`,` `-` `@`) を `_` に置き換えて書く。ユニットアドレス (`@` の後ろ) も名前に含める。

| パス | 書き方 |
|:---|:---|
| `/zephyr,user` | `DT_PATH(zephyr_user)` |
| `/soc/i2c@12340000` | `DT_PATH(soc, i2c_12340000)` |
| `/aliases` | `DT_PATH(aliases)` |

`,` と `_` はどちらも `_` になるので、`zephyr,user` と `zephyr_user` という名前のノードは、同じ書き方になる。Devicetree の上では別のノードなので、overlay ではノードの名前を `zephyr,user` にそろえる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `...` | in | ルートからのノード名の並び (小文字にして、`,` `-` `@` を `_` に置き換えたもの) |

戻り値: ノードの識別子。パスにノードがなければ、ノードの識別子としては使えない値になり、ほかの `DT_*` のマクロに渡すとコンパイルのエラーになる (`DT_NODE_EXISTS()` と `DT_NODE_HAS_PROP()` は 0 になる)。

#### zephyr,user ノード

`/zephyr,user` は、アプリのための特別なノード。binding (プロパティの書式の定義) を書かなくても、任意のプロパティを置ける (プロパティの型は値から推定される)。ボードごとに変えたい値 (ADC のチャンネル、GPIO のピン、数値など) を overlay に書いて、アプリから読むのに使う。Zephyr のドライバやサブシステムのためではなく、アプリとサンプルのためのもの。

このプロジェクトでは、ノードの温度センサの ADC のチャンネル (`io-channels`) を置く。`io-channels` があれば ADC を読み、なければ、ほかの方法 (センサ API か、シミュレーション) にする ([`app/thermo-node/src/sensor.c:33`](../../app/thermo-node/src/sensor.c#L33))。

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

使用箇所: [`app/thermo-node/src/sensor.c:33`](../../app/thermo-node/src/sensor.c#L33) (ほか 1 行)

### DT_NODE_HAS_PROP

```c
#define DT_NODE_HAS_PROP(node_id, prop)
```

ノードがそのプロパティを持っていれば 1、なければ 0 になる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |
| `prop` | in | プロパティの名前 (小文字、アンダースコア区切り) |

戻り値: 1 または 0。

使用箇所: [`app/thermo-node/src/sensor.c:33`](../../app/thermo-node/src/sensor.c#L33)

### ADC_DT_SPEC_GET

```c
#define ADC_DT_SPEC_GET(node_id)
```

ノードの `io-channels` の最初のチャンネルから、`struct adc_dt_spec` の初期化子を作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |

使用箇所: [`app/thermo-node/src/sensor.c:64`](../../app/thermo-node/src/sensor.c#L64)

### GPIO_DT_SPEC_GET

```c
#define GPIO_DT_SPEC_GET(node_id, prop)
```

ノードのプロパティ (例: `gpios`) から、`struct gpio_dt_spec` の初期化子を作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |
| `prop` | in | GPIO を指定するプロパティの名前 |

使用箇所: [`app/thermo-node/src/button.c:25`](../../app/thermo-node/src/button.c#L25)

### PWM_DT_SPEC_GET

```c
#define PWM_DT_SPEC_GET(node_id)
```

ノードの `pwms` の最初の要素から、`struct pwm_dt_spec` の初期化子を作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `node_id` | in | ノードの識別子 |

使用箇所: [`app/thermo-node/src/buzzer.c:25`](../../app/thermo-node/src/buzzer.c#L25)

### I2C_DT_SPEC_INST_GET

```c
#define I2C_DT_SPEC_INST_GET(inst)
```

ドライバのインスタンスの番号から、`struct i2c_dt_spec` (バスとアドレス) の初期化子を作る。`drivers/rtc_pcf8563.c` で使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `inst` | in | インスタンスの番号 |

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:259`](../../app/thermo-node/drivers/rtc_pcf8563.c#L259)

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

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:261`](../../app/thermo-node/drivers/rtc_pcf8563.c#L261)

### POST_KERNEL

```c
POST_KERNEL  /* 初期化のレベル */
```

デバイスの初期化のレベル。カーネルが動き出したあとに初期化するので、カーネルのサービス (スリープ、セマフォなど) と、ほかのドライバ (I2C など) を使える。`DEVICE_DT_INST_DEFINE()` の `level` に渡す。同じレベルの中の順番は、`prio` (優先度。小さいほど先) で決まる。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:261`](../../app/thermo-node/drivers/rtc_pcf8563.c#L261)

### DT_INST_FOREACH_STATUS_OKAY

```c
#define DT_INST_FOREACH_STATUS_OKAY(fn)
```

`status = "okay"` の全てのインスタンスについて、`fn(inst)` を展開する。ドライバの末尾で、`DEVICE_DT_INST_DEFINE()` を呼ぶマクロを渡す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `fn` | in | インスタンスの番号を引数に取るマクロ |

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:271`](../../app/thermo-node/drivers/rtc_pcf8563.c#L271)

### DEVICE_API

```c
#define DEVICE_API(_class, _name)
```

デバイスの API の構造体を、`static const struct <_class>_driver_api _name` として定義する。ドライバ (`drivers/rtc_pcf8563.c`) の API の表に使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_class` | in | デバイスの種類 (例: `rtc`) |
| `_name` | in | 構造体の名前 |

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:102`](../../app/thermo-node/drivers/rtc_pcf8563.c#L102)

## 構造体

メンバーを持つ構造体と、その説明。各 API の引数に、この構造体が出てくる。

### device

```c
struct device
```

デバイスのインスタンスを表す構造体 (ROM に置く)。ドライバのインスタンスごとに 1 つある。ポインタは、Devicetree のノードから `DEVICE_DT_GET()` などで得る。

| メンバー | 型 | 説明 |
|:---|:---|:---|
| `name` | `const char *` | デバイスの名前 |
| `config` | `const void *` | デバイスの設定 (ROM) |
| `api` | `const void *` | デバイスが公開する API の表 (`struct sensor_driver_api` など) |
| `state` | `struct device_state *` | デバイスの状態 (初期化の結果など。RAM) |
| `data` | `void *` | デバイスごとの、書き換えるデータ (RAM) |
| `ops` | `struct device_ops` | デバイスの操作 |
| `flags` | `device_flags_t` | デバイスのフラグ |

注意:

- アプリは、ポインタを持ち回して、`device_is_ready()` や各サブシステムの API に渡す。メンバーはドライバが使うもので、アプリは直接読み書きしない。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:95`](../../app/thermo-node/drivers/rtc_pcf8563.c#L95) (ほか 5 行), [`app/thermo-node/src/oled.c:98`](../../app/thermo-node/src/oled.c#L98), [`app/thermo-node/src/node_time.c:61`](../../app/thermo-node/src/node_time.c#L61), [`app/thermo-node/src/sensor.c:47`](../../app/thermo-node/src/sensor.c#L47) (ほか 1 行)

### gpio_dt_spec

```c
struct gpio_dt_spec
```

Devicetree で指定した GPIO のピンの情報。`GPIO_DT_SPEC_GET()` で作る。

| メンバー | 型 | 説明 |
|:---|:---|:---|
| `port` | `const struct device *` | ピンを持つ GPIO のデバイス |
| `pin` | `gpio_pin_t` | ポートの中の、ピンの番号 |
| `dt_flags` | `gpio_dt_flags_t` | Devicetree で指定した、ピンの設定のフラグ (`GPIO_ACTIVE_LOW` など) |

使用箇所: [`app/thermo-node/src/button.c:25`](../../app/thermo-node/src/button.c#L25)

### i2c_dt_spec

```c
struct i2c_dt_spec
```

Devicetree で指定した I2C のデバイスの情報 (バスとアドレス)。`I2C_DT_SPEC_INST_GET()` などで作る。

| メンバー | 型 | 説明 |
|:---|:---|:---|
| `bus` | `const struct device *` | I2C のバス |
| `addr` | `uint16_t` | 相手のアドレス |

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:90`](../../app/thermo-node/drivers/rtc_pcf8563.c#L90)

### pwm_dt_spec

```c
struct pwm_dt_spec
```

Devicetree で指定した PWM の情報。`PWM_DT_SPEC_GET()` で作る。

| メンバー | 型 | 説明 |
|:---|:---|:---|
| `dev` | `const struct device *` | PWM のデバイス |
| `channel` | `uint32_t` | デバイスの中のチャンネルの番号 |
| `period` | `uint32_t` | 信号の周期 (ナノ秒) |
| `flags` | `pwm_flags_t` | チャンネルのフラグ。対応しないドライバでは 0 |

使用箇所: [`app/thermo-node/src/buzzer.c:25`](../../app/thermo-node/src/buzzer.c#L25)

### adc_dt_spec

```c
struct adc_dt_spec
```

Devicetree で指定した ADC のチャンネルの情報。`ADC_DT_SPEC_GET()` で作る。

| メンバー | 型 | 説明 |
|:---|:---|:---|
| `dev` | `const struct device *` | ADC のデバイス |
| `channel_id` | `uint8_t` | チャンネルの番号 |
| `channel_cfg_dt_node_exists` | `bool` | チャンネルの設定が、Devicetree の子ノードにあるか |
| `channel_cfg` | `struct adc_channel_cfg` | Devicetree で指定した、チャンネルの設定 (ゲイン、基準、取り込みの時間など)。上のフラグが true のときだけ有効 |
| `vref_mv` | `uint16_t` | 基準電圧 (mV)。指定がなければ 0 |
| `resolution` | `uint8_t` | 分解能 (ビット) |
| `oversampling` | `uint8_t` | オーバーサンプリングの設定 |

注意:

- `channel_cfg` 以降のメンバーは、`channel_cfg_dt_node_exists` が true のときだけ有効。

使用箇所: [`app/thermo-node/src/sensor.c:64`](../../app/thermo-node/src/sensor.c#L64)

### adc_sequence

```c
struct adc_sequence
```

ADC の 1 回の読み取りの設定。`adc_read()` に渡す。

| メンバー | 型 | 説明 |
|:---|:---|:---|
| `options` | `const struct adc_sequence_options *` | 追加のオプション。NULL なら、1 回だけ読む |
| `channels` | `uint32_t` | 読むチャンネルのビットマスク (事前に `adc_channel_setup()` で設定しておく) |
| `buffer` | `void *` | 値を書くバッファ |
| `buffer_size` | `size_t` | バッファの大きさ (バイト)。小さければ、ドライバがエラーを返す |
| `resolution` | `uint8_t` | 分解能 (ビット) |
| `oversampling` | `uint8_t` | オーバーサンプリング。値は、2 の `oversampling` 乗回の変換の平均になる |
| `calibrate` | `bool` | true なら、読む前にキャリブレーションする |

注意:

- `adc_sequence_init_dt()` が、`channels` `resolution` `oversampling` などを `struct adc_dt_spec` から設定する。

使用箇所: [`app/thermo-node/src/sensor.c:187`](../../app/thermo-node/src/sensor.c#L187)

### rtc_time

```c
struct rtc_time
```

日時 (サブ秒の精度つき)。`tm_sec` から `tm_isdst` までは、標準ライブラリの `struct tm` と同じ並びになっている。`rtc_time_to_tm()` で、`struct tm *` に安全に変えられる。

| メンバー | 型 | 説明 |
|:---|:---|:---|
| `tm_sec` | `int` | 秒 [0, 59] |
| `tm_min` | `int` | 分 [0, 59] |
| `tm_hour` | `int` | 時 [0, 23] |
| `tm_mday` | `int` | 日 [1, 31] |
| `tm_mon` | `int` | 月 [0, 11] (1 月が 0) |
| `tm_year` | `int` | 年 − 1900 |
| `tm_wday` | `int` | 曜日 [0, 6] (日曜が 0。不明なら −1) |
| `tm_yday` | `int` | 年の通算日 [0, 365] (不明なら −1) |
| `tm_isdst` | `int` | 夏時間のフラグ (不明なら −1) |
| `tm_nsec` | `int` | ナノ秒 [0, 999999999] (不明なら 0) |

注意:

- `tm_mon` と `tm_year` は、人の見る値とずれている (月は 0 から始まり、年は 1900 を引いた値)。

使用箇所: [`app/thermo-node/drivers/rtc_pcf8563.c:95`](../../app/thermo-node/drivers/rtc_pcf8563.c#L95) (ほか 3 行), [`app/thermo-node/src/node_time.c:172`](../../app/thermo-node/src/node_time.c#L172) (ほか 1 行)

### sensor_value

```c
struct sensor_value
```

センサの読み取り値。整数部と小数部を持ち、値は `val1 + val2 × 10^-6` になる。

| メンバー | 型 | 説明 |
|:---|:---|:---|
| `val1` | `int32_t` | 整数部 |
| `val2` | `int32_t` | 小数部 (100 万分の 1 の単位) |

注意:

- 負の値も同じ式になる。例: 0.5 は `val1 = 0, val2 = 500000`、−0.5 は `val1 = 0, val2 = −500000`、−1.5 は `val1 = −1, val2 = −500000`。
- `sensor_value_to_double()` や `sensor_value_to_milli()` で数に変える。

使用箇所: [`app/thermo-node/src/sensor.c:149`](../../app/thermo-node/src/sensor.c#L149) (ほか 2 行)
