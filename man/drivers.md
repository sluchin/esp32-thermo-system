# デバイスとドライバ

デバイスの確認 (`zephyr/device.h`) と、ドライバ API (`zephyr/drivers/adc.h` `gpio.h` `i2c.h` `pwm.h` `rtc.h` `sensor.h`)、Devicetree のマクロ (`zephyr/devicetree.h`) の、使っている API。ハードウェアは、Devicetree から得て、使う前に `device_is_ready()` か、`*_is_ready_dt()` で確かめる。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

## デバイス

### device_is_ready

```c
bool device_is_ready(const struct device *dev);
```

デバイスが使える状態かを調べる。`DEVICE_DT_GET()` で得たポインタは、`device_get_binding()` の準備の確認を含まないので、これで確かめる。

- 少なくとも、デバイスの初期化が成功していることを確かめる。
- 戻り値: 使えれば `true`。使えない、または `NULL` を渡したら `false`。

使用箇所: `app/thermo-node/src/node_time.c:84`, `app/thermo-node/src/oled.c:137`, `app/thermo-node/src/sensor.c:73`

## ADC

### adc_is_ready_dt

```c
static inline bool adc_is_ready_dt(const struct adc_dt_spec *spec);
```

Devicetree から得た ADC のデバイスが使えるかを調べる。

- 戻り値: 使えれば `true`、使えなければ `false`。

使用箇所: `app/thermo-node/src/sensor.c:83`

### adc_channel_setup_dt

```c
static inline int adc_channel_setup_dt(const struct adc_dt_spec *spec);
```

`struct adc_dt_spec` から、ADC のチャンネルを設定する。

- 戻り値: `adc_channel_setup()` の値。Devicetree の情報が正しくなければ `-ENOTSUP`。

使用箇所: `app/thermo-node/src/sensor.c:89`

### adc_read_dt

```c
static inline int adc_read_dt(const struct adc_dt_spec *spec, const struct adc_sequence *sequence);
```

`struct adc_dt_spec` から、読み取りを要求する。

- `sequence`: 要求するサンプリングの手順。
- 戻り値: `adc_read()` の値。

使用箇所: `app/thermo-node/src/sensor.c:159`

### adc_raw_to_millivolts_dt

```c
static inline int adc_raw_to_millivolts_dt(const struct adc_dt_spec *spec, int32_t *valp);
```

ADC の生の値を、`struct adc_dt_spec` の情報を使って、ミリボルトにする。

- `valp` (入出力): 入力は生の値。成功すると、ミリボルトの値になる。失敗したら、値は変わらない。
- 戻り値: `adc_raw_to_millivolts()` の値。Devicetree の情報が正しくなければ `-ENOTSUP`。

使用箇所: `app/thermo-node/src/sensor.c:167`

## GPIO

### gpio_is_ready_dt

```c
static inline bool gpio_is_ready_dt(const struct gpio_dt_spec *spec);
```

GPIO のポートが使えるかを調べる。

- 戻り値: 使えれば `true`、使えなければ `false`。

使用箇所: `app/thermo-node/src/button.c:41`

### gpio_pin_configure_dt

```c
static inline int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t extra_flags);
```

`struct gpio_dt_spec` のピンを、Devicetree のフラグに追加のフラグを足して、設定する。`gpio_pin_configure(spec->port, spec->pin, spec->dt_flags | extra_flags)` と同じ。

- `extra_flags`: 追加のフラグ (入力は `GPIO_INPUT`)。
- 戻り値: `gpio_pin_configure()` の値。

使用箇所: `app/thermo-node/src/button.c:46`

### gpio_pin_get_dt

```c
static inline int gpio_pin_get_dt(const struct gpio_dt_spec *spec);
```

入力ピンの論理レベルを返す。`gpio_pin_get(spec->port, spec->pin)` と同じ。

- 戻り値: `gpio_pin_get()` の値 (論理レベルが 1 なら正、0 なら 0、失敗なら負)。

使用箇所: `app/thermo-node/src/button.c:53 (ほか 1 行)`

## I2C

### i2c_is_ready_dt

```c
static inline bool i2c_is_ready_dt(const struct i2c_dt_spec *spec);
```

I2C のバスが使えるかを調べる。

- 戻り値: 使えれば `true`、使えなければ `false`。

使用箇所: `app/thermo-node/drivers/rtc_pcf8563.c:233`

### i2c_burst_read_dt

```c
static inline int i2c_burst_read_dt(const struct i2c_dt_spec *spec, uint8_t start_addr, uint8_t *buf, uint32_t num_bytes);
```

I2C デバイスの内部アドレスから、複数バイトを読む。`i2c_burst_read(spec->bus, spec->addr, start_addr, buf, num_bytes)` と同じ。

- `start_addr`: 読み始める内部アドレス。`buf`: 読んだデータを入れる領域。`num_bytes`: 読むバイト数。
- 戻り値: `i2c_burst_read()` の値。

使用箇所: `app/thermo-node/drivers/rtc_pcf8563.c:180`

### i2c_burst_write_dt

```c
static inline int i2c_burst_write_dt(const struct i2c_dt_spec *spec, uint8_t start_addr, const uint8_t *buf, uint32_t num_bytes);
```

I2C デバイスの内部アドレスへ、複数バイトを書く。`i2c_burst_write(spec->bus, spec->addr, start_addr, buf, num_bytes)` と同じ。

- `start_addr`: 書き始める内部アドレス。`buf`: 書くデータ。`num_bytes`: 書くバイト数。
- 戻り値: `i2c_burst_write()` の値。

使用箇所: `app/thermo-node/drivers/rtc_pcf8563.c:152`

### i2c_reg_write_byte_dt

```c
static inline int i2c_reg_write_byte_dt(const struct i2c_dt_spec *spec, uint8_t reg_addr, uint8_t value);
```

I2C デバイスの内部レジスタに、1 バイトを書く。`i2c_reg_write_byte(spec->bus, spec->addr, reg_addr, value)` と同じ。

- 戻り値: `i2c_reg_write_byte()` の値。

使用箇所: `app/thermo-node/drivers/rtc_pcf8563.c:146`

## PWM

### pwm_is_ready_dt

```c
static inline bool pwm_is_ready_dt(const struct pwm_dt_spec *spec);
```

PWM のデバイスが使えるかを調べる。

- 戻り値: 使えれば `true`、使えなければ `false`。

使用箇所: `app/thermo-node/src/buzzer.c:49`

### pwm_set_dt

```c
static inline int pwm_set_dt(const struct pwm_dt_spec *spec, uint32_t period, uint32_t pulse);
```

周期とパルス幅を、ナノ秒で設定する。`spec` に書いてある周期は無視される。Devicetree の周期を、実行中に変えたいときに使う (ブザーの音階)。`pwm_set(spec->dev, spec->channel, period, pulse, spec->flags)` と同じ。

- `period`: 周期 [ns]。`pulse`: パルス幅 [ns]。0 にすると、出力が止まる。
- 戻り値: `pwm_set()` の値。

使用箇所: `app/thermo-node/src/buzzer.c:136`

### pwm_set_pulse_dt

```c
static inline int pwm_set_pulse_dt(const struct pwm_dt_spec *spec, uint32_t pulse);
```

パルス幅をナノ秒で設定する。周期は、`spec` の値を使う。`pwm_set(spec->dev, spec->channel, spec->period, pulse, spec->flags)` と同じ。

- 戻り値: `pwm_set()` の値。

使用箇所: `app/thermo-node/src/buzzer.c:131`

## RTC

### rtc_get_time

```c
int rtc_get_time(const struct device *dev, struct rtc_time *timeptr);
```

RTC の時刻を取得する。

- `timeptr`: 時刻を入れる領域。`struct rtc_time` の月は 0 から 11、年は 1900 年からの年数。
- 戻り値: 成功は 0。`-ENODATA`: RTC に時刻が設定されていない。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/node_time.c:225`

### rtc_set_time

```c
int rtc_set_time(const struct device *dev, const struct rtc_time *timeptr);
```

RTC の時刻を設定する。

- 戻り値: 成功は 0。`-EINVAL`: 時刻が不正、または、ハードウェアの範囲を超えている。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/node_time.c:200`

## センサ

### sensor_sample_fetch

```c
int sensor_sample_fetch(const struct device *dev);
```

センサから、サンプルを取得して、ドライバの内部のバッファに入れる。有効な全てのチャンネルを読み、必要なら、値を使える形にする。取得が終わるまで待つ。

- I2C や SPI で接続したセンサは、通信をするので、割り込みから呼ばない。
- 値は、続けて `sensor_channel_get()` で取り出す。
- 戻り値: 成功は 0、失敗は負の errno。

使用箇所: `app/thermo-node/src/sensor.c:123`

### sensor_channel_get

```c
int sensor_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val);
```

指定のチャンネルの値を、ドライバの内部のデータから取り出す。先に `sensor_sample_fetch()` (または `sensor_sample_fetch_chan()`) でサンプルを取得しておく。間に取得し直さなければ、続けて呼んでも、同じ値になる。

- `chan`: チャンネル (`SENSOR_CHAN_AMBIENT_TEMP` `SENSOR_CHAN_HUMIDITY`)。`val`: 値を入れる領域 (`val1` が整数部、`val2` が小数部 [百万分の 1])。
- 戻り値: 成功は 0、失敗は負の errno。

使用箇所: `app/thermo-node/src/sensor.c:129 (ほか 1 行)`

### sensor_value_to_milli

```c
static inline int64_t sensor_value_to_milli(const struct sensor_value *val);
```

`struct sensor_value` を、1000 倍の整数 (ミリ単位) にする。

- 戻り値: 変換した値。

使用箇所: 直接の呼び出しなし (マクロの内側や、別のマクロ経由で使う)

## Devicetree

### DEVICE_DT_GET

```c
#define DEVICE_DT_GET(node_id)
```

Devicetree のノードから、デバイスへのポインタを得る (コンパイル時に決まる)。使う前に、 で確かめる。

使用箇所: `app/thermo-node/src/node_time.c:61`, `app/thermo-node/src/oled.c:98`, `app/thermo-node/src/sensor.c:42`

### DT_ALIAS

```c
#define DT_ALIAS(alias)
```

Devicetree の alias から、ノードの識別子を得る (例: `DT_ALIAS(rtc0)`)。

使用箇所: `app/thermo-node/src/button.c:25`, `app/thermo-node/src/buzzer.c:25`, `app/thermo-node/src/node_time.c:61`, `app/thermo-node/src/oled.c:98` ほか 1 ファイル

### DT_HAS_ALIAS

```c
#define DT_HAS_ALIAS(alias)
```

alias があって、ノードが有効なら 1、なければ 0 になる。`#if` や `COND_CODE_1` で、オプションの部品の有無を切り替えるのに使う。

使用箇所: `app/thermo-node/src/sensor.c:24`

### DT_PATH

```c
#define DT_PATH(...)
```

ノードのパスから、ノードの識別子を得る (例: `DT_PATH(zephyr_user)`)。

使用箇所: `app/thermo-node/src/sensor.c:28 (ほか 1 行)`

### DT_NODE_HAS_PROP

```c
#define DT_NODE_HAS_PROP(node_id, prop)
```

ノードが、そのプロパティを持っていれば 1、なければ 0 になる。

使用箇所: `app/thermo-node/src/sensor.c:28`

### ADC_DT_SPEC_GET

```c
#define ADC_DT_SPEC_GET(node_id)
```

ノードの `io-channels` の最初のチャンネルから、`struct adc_dt_spec` の初期化子を作る。

使用箇所: `app/thermo-node/src/sensor.c:48`

### GPIO_DT_SPEC_GET

```c
#define GPIO_DT_SPEC_GET(node_id, prop)
```

ノードのプロパティ (例: `gpios`) から、`struct gpio_dt_spec` の初期化子を作る。

使用箇所: `app/thermo-node/src/button.c:25`

### PWM_DT_SPEC_GET

```c
#define PWM_DT_SPEC_GET(node_id)
```

ノードの `pwms` の最初の要素から、`struct pwm_dt_spec` の初期化子を作る。

使用箇所: `app/thermo-node/src/buzzer.c:25`

### I2C_DT_SPEC_INST_GET

```c
#define I2C_DT_SPEC_INST_GET(inst)
```

ドライバのインスタンスの番号から、`struct i2c_dt_spec` (バスとアドレス) の初期化子を作る。`drivers/rtc_pcf8563.c` で使う。

使用箇所: `app/thermo-node/drivers/rtc_pcf8563.c:263`

### DEVICE_DT_INST_DEFINE

```c
#define DEVICE_DT_INST_DEFINE(inst, init_fn, pm, data, config, level, prio, api)
```

ドライバのインスタンスの番号から、デバイスを定義する。初期化の関数、データ、設定、初期化の段階と優先度、API の構造体を指定する。

使用箇所: `app/thermo-node/drivers/rtc_pcf8563.c:265`

### DT_INST_FOREACH_STATUS_OKAY

```c
#define DT_INST_FOREACH_STATUS_OKAY(fn)
```

`status = "okay"` の全てのインスタンスについて、`fn(inst)` を展開する。ドライバの末尾で、`DEVICE_DT_INST_DEFINE()` を呼ぶマクロを渡す。

使用箇所: `app/thermo-node/drivers/rtc_pcf8563.c:275`

### DEVICE_API

```c
#define DEVICE_API(_class, _name)
```

デバイスの API の構造体を、`static const struct <_class>_driver_api _name` として定義する。ドライバ (`drivers/rtc_pcf8563.c`) の API の表に使う。

使用箇所: `app/thermo-node/drivers/rtc_pcf8563.c:102`
