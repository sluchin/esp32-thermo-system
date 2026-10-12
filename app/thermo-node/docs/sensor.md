# sensor.c の解析

対象: `app/thermo-node/src/sensor.c` (180 行) と `sensor.h` (26 行)

温度と湿度を読む部分です。センサの種類は、**ビルドのときに Devicetree で決まり**、1 つのソースが 3 通りに切り替わります。

| 構成 | 選ばれる条件 | 読み方 |
|:---|:---|:---|
| センサ API (`HAVE_SENSOR`) | `CONFIG_SIMULATOR` ではなく、alias `thermo-sensor` がある (DHT11 など) | Zephyr のセンサ API |
| ADC (`HAVE_ADC`) | 上がなく、`zephyr,user` に `io-channels` がある | ADC を読んで、電圧から温度に換算 |
| シミュレーション | どちらもない (native_sim など)、または `CONFIG_SIMULATOR` | 乱数 |

実機 (`xiao_esp32c3`) は、`boards/xiao_esp32c3.overlay` で `thermo-sensor = &dht11;` (`aosong,dht`、D7) を定義するので、センサ API の構成です。native_sim の overlay は、`io-channels` を定義しないので、シミュレーションになります。

## 公開する関数

| 関数 | 役割 |
|:---|:---|
| `sensor_init()` | センサか ADC のチャンネルを使えるようにする |
| `sensor_read()` | 温度 (℃ の 10 倍) と湿度 (% の 10 倍) を読む |

どちらも、成功は `EXIT_SUCCESS`、失敗は負の errno です。湿度を測れないセンサ (ADC) は、湿度に `THERMO_HUMIDITY_NONE` (0xFFFF) を返します。

## センサ API の構成

### sensor_init

`device_is_ready()` でセンサを確認します。使えなければ `-ENODEV`。

### sensor_read

```text
sensor_sample_fetch()                               失敗 → 返る
sensor_channel_get(SENSOR_CHAN_AMBIENT_TEMP)         失敗 → 返る
    温度 [x10] = sensor_value_to_milli(...) / 100
sensor_channel_get(SENSOR_CHAN_HUMIDITY)
    -ENOTSUP → 湿度なし (THERMO_HUMIDITY_NONE) として成功
    その他の失敗 → 返る
    成功 → 湿度 [x10] = sensor_value_to_milli(...) / 100
```

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
```

既定の換算は、LM35 (0 ℃ で 0 mV、1 ℃ あたり 10 mV) に合っています (`THERMO_ADC_MV_PER_DEG` = 10、`THERMO_ADC_OFFSET_MV` = 0)。別のセンサは、`Kconfig` の値で合わせます。

## シミュレーションの構成

```text
温度 = 100 + 乱数 % 400       → 10.0 ℃ から 49.9 ℃
湿度 = 300 + 乱数 % 600       → 30.0 % から 89.9 %
```

`sys_rand32_get()` の値です。`sensor_init()` は、警告を出して成功します。ビルドのコードは、`#if`、`#elif`、`#else` で選ぶので、使わない構成の関数や変数は、ビルドに入りません。

## 気づいた点

- 温度は `int16_t` に切り詰めます。DHT11 の範囲 (0 から 50 ℃) では、問題ありません。
- DHT11 は、1 秒に 1 回より速く読めません。ノードは 5 秒ごとに読むので、問題ありません。
- 読み取りの失敗 (`-EIO` など) は、`main.c` が、その回の値を捨てて、次の周期でやり直します。

## 関連

- [main.md](main.md): `measure()` が `sensor_read()` を呼ぶ
- [index.md](index.md): このディレクトリの文書の一覧
