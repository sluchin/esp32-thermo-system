# sensor.c の解析

対象: `app/thermo-node/src/sensor.c` (258 行) と `sensor.h` (27 行)

温度と湿度と気圧を読む部分です。センサの種類は、**ビルドのときに Devicetree で決まり**、1 つのソースが 3 通りに切り替わります。

| 構成 | 選ばれる条件 | 読み方 |
|:---|:---|:---|
| センサ API (`HAVE_SENSOR`) | `CONFIG_SIMULATOR` ではなく、alias `thermo-sensor` がある (AHT20、DHT11 など) | Zephyr のセンサ API。alias `thermo-pressure` もあれば (`HAVE_PRESSURE`。BMP280 など)、気圧も読む |
| ADC (`HAVE_ADC`) | 上がなく、`zephyr,user` に `io-channels` がある | ADC を読んで、電圧から温度に換算 |
| シミュレーション | どちらもない (native_sim など)、または `CONFIG_SIMULATOR` | 乱数 |

実機 (`xiao_esp32c3`) は、`boards/xiao_esp32c3.overlay` で `thermo-sensor = &aht20;` (`aosong,aht20`、I2C の 0x38) と `thermo-pressure = &bmp280;` (`bosch,bme280`、I2C の 0x77) を定義するので、気圧つきのセンサ API の構成です。`THERMO_SENSOR=dht11` のビルドは、`sensors/dht11.overlay` で `thermo-sensor = &dht11;` (`aosong,dht`、D7) にして、`thermo-pressure` を消すので、気圧なしのセンサ API の構成です。native_sim の overlay は、`io-channels` を定義しないので、シミュレーションになります。

## 公開する関数

| 関数 | 役割 |
|:---|:---|
| `sensor_init()` | センサか ADC のチャンネルを使えるようにする |
| `sensor_read()` | 温度 (℃ の 10 倍) と湿度 (% の 10 倍) と気圧 (hPa の 10 倍) を読む |

どちらも、成功は `EXIT_SUCCESS`、失敗は負の errno です。湿度を測れないセンサ (ADC) は、湿度に `THERMO_HUMIDITY_NONE` (0xFFFF) を返します。気圧を測れないとき (気圧のセンサがない、使えない、読めない) は、気圧に `THERMO_PRESSURE_NONE` (0xFFFF) を返します。

## センサ API の構成

### sensor_init

`device_is_ready()` でセンサを確認します。使えなければ `-ENODEV`。

気圧のセンサ (`HAVE_PRESSURE`) は、`device_is_ready()` の結果を `pressure_ready` に残すだけです。使えなくても、警告 (`Pressure sensor is not ready, measuring without the pressure`) を出して、成功にします (温度と湿度は測る)。BMP280 の I2C のアドレスが違うとき (0x76) などです。

### sensor_read

```text
sensor_sample_fetch()                               失敗 → 返る
sensor_channel_get(SENSOR_CHAN_AMBIENT_TEMP)         失敗 → 返る
    温度 [x10] = sensor_value_to_milli(...) / 100
sensor_channel_get(SENSOR_CHAN_HUMIDITY)
    -ENOTSUP → 湿度なし (THERMO_HUMIDITY_NONE) として成功
    その他の失敗 → 返る
    成功 → 湿度 [x10] = sensor_value_to_milli(...) / 100
気圧 = read_pressure()  (HAVE_PRESSURE のとき。ないときは THERMO_PRESSURE_NONE)
```

### read_pressure

```text
pressure_ready でない                            → 気圧なし
sensor_sample_fetch(pressure_dev)                 失敗 → 気圧なし (LOG_ERR)
sensor_channel_get(SENSOR_CHAN_PRESS)             失敗 → 気圧なし (LOG_ERR)
Pa = sensor_value_to_milli(...)                   ← SENSOR_CHAN_PRESS の単位は kPa。ミリ単位で Pa
Pa < 0、または Pa >= 655350 (6553.5 hPa)          → 気圧なし (uint16 の 10 分の 1 hPa に入らない)
気圧 [x10] = Pa / 10                              例: 101320 Pa → 10132 (1013.2 hPa)
```

気圧の失敗は、`sensor_read()` の失敗にしません (温度と湿度は送る)。温度と湿度のセンサの失敗は、これまでどおり、そのエラーを返します (気圧は読みません)。

BMP280 は、温度も測りますが、使いません (温度は AHT20 の値)。BMP280 は、`bosch,bme280` のドライバで動きます (チップの ID の 0x56、0x57、0x58 で、BMP280 として扱い、湿度のチャンネルはありません)。

`sensor_value_to_milli()` は、ミリ単位 (1/1000) の値を返すので、100 で割ると、10 倍の値になります。DHT11 の値は、整数の ℃ と % なので、小数点以下は 0 です。

湿度を測れないセンサは、`-ENOTSUP` を返すので、湿度なしとして、成功にします。

## ADC の構成

### sensor_init

`adc_is_ready_dt()` で、ADC のコントローラを確認し (`-ENODEV`)、`adc_channel_setup_dt()` で Devicetree のチャンネルの設定 (ゲイン、基準電圧、分解能) を反映します。

### sensor_read

```text
adc_read_dt()                      12 ビットの生の値を 1 回読む
adc_raw_to_millivolts_dt()         生の値 → 電圧 [mV]
温度 [x10] = (mV - CONFIG_THERMO_ADC_OFFSET_MV) * 10 / CONFIG_THERMO_ADC_MV_PER_DEG
湿度 = THERMO_HUMIDITY_NONE
気圧 = THERMO_PRESSURE_NONE
```

既定の換算は、LM35 (0 ℃ で 0 mV、1 ℃ あたり 10 mV) に合っています (`THERMO_ADC_MV_PER_DEG` = 10、`THERMO_ADC_OFFSET_MV` = 0)。別のセンサは、`Kconfig` の値で合わせます。

## シミュレーションの構成

```text
温度 = 100 + 乱数 % 400       → 10.0 ℃ から 49.9 ℃
湿度 = 300 + 乱数 % 600       → 30.0 % から 89.9 %
気圧 = 9900 + 乱数 % 400      → 990.0 hPa から 1029.9 hPa
```

`sys_rand32_get()` の値です。`sensor_init()` は、警告を出して成功します。ビルドのコードは、`#if`、`#elif`、`#else` で選ぶので、使わない構成の関数や変数は、ビルドに入りません。

## 気づいた点

- 温度は `int16_t` に切り詰めます。AHT20 (-40 から 85 ℃) と DHT11 (0 から 50 ℃) の範囲では、問題ありません。
- 気圧のセンサは、起動のときに 1 回だけ確かめます。起動のあとに BMP280 をつないでも、再起動するまで、気圧なしです。
- DHT11 は、1 秒に 1 回より速く読めません。ノードは 5 秒ごとに読むので、問題ありません。
- 読み取りの失敗 (`-EIO` など) は、`main.c` が、その回の値を捨てて、次の周期でやり直します。

## 関連

- [main.md](main.md): `measure()` が `sensor_read()` を呼ぶ
- [index.md](index.md): このディレクトリの文書の一覧
