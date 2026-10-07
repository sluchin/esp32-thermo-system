/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の温湿度センサの実装 (センサ API, ADC, シミュレーション値)
 */

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <errno.h>  /* ENOTSUP */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "sensor.h"
#include "thermo_ble_uuid.h"

/* 実機のセンサを使うかを, Devicetree で決める (シミュレータは, 常に, シミュレーション値) */
#if !defined(CONFIG_SIMULATOR) && DT_HAS_ALIAS(thermo_sensor)
#include <zephyr/drivers/sensor.h>
/** センサ API (DHT11 など) を使うビルドであることを示す */
#define HAVE_SENSOR 1
#elif !defined(CONFIG_SIMULATOR) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#include <zephyr/drivers/adc.h>
/** 実機の ADC を使うビルドであることを示す (ADC のコードを有効にする) */
#define HAVE_ADC 1
#endif
#include "thermo_log.h"

LOG_MODULE_REGISTER(sensor_thermo_node, THERMO_LOG_LEVEL);

#if defined(HAVE_SENSOR)
/** 温度と湿度の, ミリ単位 (sensor_value_to_milli) から, 10 分の 1 の単位への割り算の値 */
#define MILLI_PER_X10 100

/** Devicetree (alias thermo-sensor) から取得したセンサ */
static const struct device *const sensor_dev = DEVICE_DT_GET(DT_ALIAS(thermo_sensor));
#elif defined(HAVE_ADC)
/** ADC の分解能 [bit] */
#define ADC_RESOLUTION_BITS 12U

/** Devicetree (zephyr,user の io-channels) から取得した ADC チャンネル仕様 */
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#else
/** シミュレーションの温度の最小値 [℃ の 10 倍] (10.0 ℃) */
#define SIM_TEMP_MIN_X10       100U
/** シミュレーションの温度の幅 [℃ の 10 倍] (10.0 ℃ から 49.9 ℃) */
#define SIM_TEMP_RANGE_X10     400U
/** シミュレーションの湿度の最小値 [% の 10 倍] (30.0 %) */
#define SIM_HUMIDITY_MIN_X10   300U
/** シミュレーションの湿度の幅 [% の 10 倍] (30.0 % から 89.9 %) */
#define SIM_HUMIDITY_RANGE_X10 600U
#endif

/**
 * @brief センサを初期化する
 *
 * Devicetree にセンサも ADC チャンネルもない場合 (シミュレータなど) は, 警告をログに出すだけで,
 * 成功とする.
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      センサ, または ADC コントローラが利用可能でない
 * @retval negative     ADC チャンネル設定の失敗 (負の errno)
 */
int sensor_init(void)
{
#if defined(HAVE_SENSOR)
    if (!device_is_ready(sensor_dev)) {
        LOG_ERR("Sensor is not ready");
        return -ENODEV;
    }

    LOG_INF("Sensor initialized");
#elif defined(HAVE_ADC)
    int err = EXIT_SUCCESS; /* エラーコード */

    /* ADC のドライバが初期化されていること */
    if (!adc_is_ready_dt(&adc_channel)) {
        LOG_ERR("ADC controller not ready");
        return -ENODEV;
    }

    /* Devicetree のチャンネルの設定 (ゲイン, 基準電圧, 分解能など) を ADC に反映する */
    err = adc_channel_setup_dt(&adc_channel);
    if (err < 0) {
        LOG_ERR("Could not setup ADC channel (%d)", err);
        return err;
    }

    LOG_INF("ADC sensor initialized");
#else
    LOG_WRN("No sensor in device tree, using simulated values");
#endif

    return EXIT_SUCCESS;
}

/**
 * @brief 温度と湿度を読み取る
 *
 * センサがない場合は, 乱数によるシミュレーション値を返す. ADC は, 湿度を測れないので,
 * 湿度に THERMO_HUMIDITY_NONE を返す.
 *
 * @param[out] temp_x10     温度 [℃ の 10 倍] の格納先 (NULL 不可)
 * @param[out] humidity_x10 湿度 [% の 10 倍] の格納先 (NULL 不可. 測れなければ
 * THERMO_HUMIDITY_NONE)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     センサ, または ADC の読み取りの失敗 (負の errno)
 */
int sensor_read(int16_t *temp_x10, uint16_t *humidity_x10)
{
#if defined(HAVE_SENSOR)
    struct sensor_value temp = {0};     /* 温度 */
    struct sensor_value humidity = {0}; /* 湿度 */
    int err = EXIT_SUCCESS;             /* エラーコード */

    err = sensor_sample_fetch(sensor_dev);
    if (err < 0) {
        LOG_ERR("Could not fetch the sensor (%d)", err);
        return err;
    }

    err = sensor_channel_get(sensor_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    if (err < 0) {
        LOG_ERR("Could not get the temperature (%d)", err);
        return err;
    }
    *temp_x10 = (int16_t)(sensor_value_to_milli(&temp) / MILLI_PER_X10);

    /* 湿度を測れないセンサ (-ENOTSUP) は, 湿度なしとして, 成功にする */
    err = sensor_channel_get(sensor_dev, SENSOR_CHAN_HUMIDITY, &humidity);
    if (err == -ENOTSUP) {
        *humidity_x10 = THERMO_HUMIDITY_NONE;
    } else if (err < 0) {
        LOG_ERR("Could not get the humidity (%d)", err);
        return err;
    } else {
        *humidity_x10 = (uint16_t)(sensor_value_to_milli(&humidity) / MILLI_PER_X10);
    }
#elif defined(HAVE_ADC)
    int16_t raw = 0;        /* ADC の生値 */
    int32_t mv = 0;         /* 電圧 [mV] */
    int err = EXIT_SUCCESS; /* エラーコード */

    /* 1 回だけ, 1 チャンネルを読む */
    struct adc_sequence sequence = {
        .buffer = &raw,
        .buffer_size = sizeof(raw),
        .channels = (uint32_t)BIT(adc_channel.channel_id),
        .resolution = ADC_RESOLUTION_BITS,
    };

    err = adc_read_dt(&adc_channel, &sequence);
    if (err < 0) {
        LOG_ERR("Could not read ADC (%d)", err);
        return err;
    }

    /* 生値を電圧に直して, 温度に換算する (温度センサの特性: CONFIG_THERMO_ADC_*) */
    mv = raw;
    err = adc_raw_to_millivolts_dt(&adc_channel, &mv);
    if (err < 0) {
        LOG_ERR("Could not convert the ADC value to millivolts (%d)", err);
        return err;
    }
    *temp_x10 = (int16_t)(((mv - CONFIG_THERMO_ADC_OFFSET_MV) * 10) / CONFIG_THERMO_ADC_MV_PER_DEG);
    *humidity_x10 = THERMO_HUMIDITY_NONE;
#else
    *temp_x10 = (int16_t)(SIM_TEMP_MIN_X10 + (sys_rand32_get() % SIM_TEMP_RANGE_X10));
    *humidity_x10 = (uint16_t)(SIM_HUMIDITY_MIN_X10 + (sys_rand32_get() % SIM_HUMIDITY_RANGE_X10));
#endif

    return EXIT_SUCCESS;
}
