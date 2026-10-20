/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief sensor.c の単体テスト (センサ API と気圧のセンサ)
 *
 * alias thermo-sensor と thermo-pressure に, 偽のセンサ (vnd,test-sensor) を 1 つずつ接続して,
 * AHT20 と BMP280 のような, 温度と湿度のセンサと, 気圧のセンサが別の経路をテストする.
 * センサが返す値とエラーは, センサごとに, テストから設定する.
 */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <errno.h>  /* EIO ENOTSUP */
#include <stdint.h> /* int16_t uint16_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "sensor.h"
#include "thermo_ble_uuid.h"

/** 偽のセンサ 1 つの状態 (戻り値と値は, テストから設定する) */
struct fake_state {
    int fetch_ret;             /**< sample_fetch の戻り値 */
    int channel_ret;           /**< channel_get の戻り値 */
    struct sensor_value temp;  /**< 温度 */
    struct sensor_value humi;  /**< 湿度 */
    struct sensor_value press; /**< 気圧 [kPa] */
};

/** 温度と湿度のセンサ (alias thermo-sensor) の状態 */
static struct fake_state sensor_state;
/** 気圧のセンサ (alias thermo-pressure) の状態 */
static struct fake_state pressure_state;

/**
 * 偽のセンサの sample_fetch (設定した戻り値を返す)
 *
 * @param[in] dev  センサ (config が状態)
 * @param[in] chan チャンネル (使用しない)
 * @return 状態の fetch_ret
 */
static int fake_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
    const struct fake_state *state = (const struct fake_state *)dev->config; /* センサの状態 */

    ARG_UNUSED(chan);

    return state->fetch_ret;
}

/**
 * 偽のセンサの channel_get (チャンネルごとに, 設定した値を返す)
 *
 * @param[in]  dev  センサ (config が状態)
 * @param[in]  chan チャンネル (温度, 湿度, 気圧)
 * @param[out] val  値
 * @return 状態の channel_ret. 知らないチャンネルは -ENOTSUP
 */
static int fake_channel_get(const struct device *dev, enum sensor_channel chan,
                            struct sensor_value *val)
{
    const struct fake_state *state = (const struct fake_state *)dev->config; /* センサの状態 */

    if (chan == SENSOR_CHAN_AMBIENT_TEMP) {
        *val = state->temp;
    } else if (chan == SENSOR_CHAN_HUMIDITY) {
        *val = state->humi;
    } else if (chan == SENSOR_CHAN_PRESS) {
        *val = state->press;
    } else {
        return -ENOTSUP;
    }

    return state->channel_ret;
}

/** 偽のセンサのドライバ API */
static DEVICE_API(sensor, fake_api) = {
    .sample_fetch = fake_sample_fetch,
    .channel_get = fake_channel_get,
};

DEVICE_DT_DEFINE(DT_NODELABEL(test_sensor), NULL, NULL, NULL, &sensor_state, POST_KERNEL,
                 CONFIG_SENSOR_INIT_PRIORITY, &fake_api)
DEVICE_DT_DEFINE(DT_NODELABEL(test_pressure), NULL, NULL, NULL, &pressure_state, POST_KERNEL,
                 CONFIG_SENSOR_INIT_PRIORITY, &fake_api)

/** 偽の気圧のセンサ */
static const struct device *const pressure_dev = DEVICE_DT_GET(DT_ALIAS(thermo_pressure));

/**
 * 各テストの前に, 偽のセンサを 23.5 ℃, 45 %, 1013.2 hPa の正常な状態に戻して, 初期化する
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    sensor_state = (struct fake_state){
        .temp = {.val1 = 23, .val2 = 500000},
        .humi = {.val1 = 45, .val2 = 0},
    };
    pressure_state = (struct fake_state){
        .press = {.val1 = 101, .val2 = 320000},
    };
    zassert_equal(sensor_init(), EXIT_SUCCESS);
}

/** 気圧は気圧のセンサから読んで, 10 分の 1 hPa の整数にする */
ZTEST(pressure_api, test_read)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    uint16_t pressure = 0; /* 気圧 */

    /* 期待: 23.5 ℃ は 235, 45 % は 450, 101.32 kPa (1013.2 hPa) は 10132 */
    zassert_equal(sensor_read(&temp, &humidity, &pressure), EXIT_SUCCESS);
    zassert_equal(temp, 235);
    zassert_equal(humidity, 450U);
    zassert_equal(pressure, 10132U);
}

/** 気圧のセンサが準備できていなければ, 初期化は成功して, 気圧なしで測る */
ZTEST(pressure_api, test_init_pressure_not_ready)
{
    struct device_state *state = pressure_dev->state; /* 気圧のセンサの状態 */
    int16_t temp = 0;                                 /* 温度 */
    uint16_t humidity = 0;                            /* 湿度 */
    uint16_t pressure = 0;                            /* 気圧 */

    /* 期待: 初期化は成功して, 温度と湿度は読めて, 気圧は THERMO_PRESSURE_NONE (終わったら, 戻す) */
    state->initialized = false;
    zassert_equal(sensor_init(), EXIT_SUCCESS);
    zassert_equal(sensor_read(&temp, &humidity, &pressure), EXIT_SUCCESS);
    state->initialized = true;
    zassert_equal(temp, 235);
    zassert_equal(humidity, 450U);
    zassert_equal(pressure, THERMO_PRESSURE_NONE);
}

/** 気圧のセンサの測定 (sample_fetch) に失敗しても, 気圧なしで成功する */
ZTEST(pressure_api, test_read_pressure_fetch_failure)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    uint16_t pressure = 0; /* 気圧 */

    pressure_state.fetch_ret = -EIO;

    /* 期待: 温度と湿度は読めて, 気圧は THERMO_PRESSURE_NONE */
    zassert_equal(sensor_read(&temp, &humidity, &pressure), EXIT_SUCCESS);
    zassert_equal(temp, 235);
    zassert_equal(pressure, THERMO_PRESSURE_NONE);
}

/** 気圧の取得 (channel_get) に失敗しても, 気圧なしで成功する */
ZTEST(pressure_api, test_read_pressure_get_failure)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    uint16_t pressure = 0; /* 気圧 */

    pressure_state.channel_ret = -EIO;

    /* 期待: 温度と湿度は読めて, 気圧は THERMO_PRESSURE_NONE */
    zassert_equal(sensor_read(&temp, &humidity, &pressure), EXIT_SUCCESS);
    zassert_equal(humidity, 450U);
    zassert_equal(pressure, THERMO_PRESSURE_NONE);
}

/** 負の気圧は気圧なしにする */
ZTEST(pressure_api, test_read_pressure_negative)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    uint16_t pressure = 0; /* 気圧 */

    pressure_state.press = (struct sensor_value){.val1 = 0, .val2 = -1000};

    /* 期待: -1 Pa は範囲の外なので, THERMO_PRESSURE_NONE */
    zassert_equal(sensor_read(&temp, &humidity, &pressure), EXIT_SUCCESS);
    zassert_equal(pressure, THERMO_PRESSURE_NONE);
}

/** uint16 の 10 分の 1 hPa に入る最大 (6553.4 hPa) までは, そのまま返す */
ZTEST(pressure_api, test_read_pressure_max)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    uint16_t pressure = 0; /* 気圧 */

    pressure_state.press = (struct sensor_value){.val1 = 655, .val2 = 349999};

    /* 期待: 655.349999 kPa (655349 Pa) は 65534 */
    zassert_equal(sensor_read(&temp, &humidity, &pressure), EXIT_SUCCESS);
    zassert_equal(pressure, 65534U);
}

/** 気圧なしの値 (6553.5 hPa) 以上は, 気圧なしにする */
ZTEST(pressure_api, test_read_pressure_too_high)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    uint16_t pressure = 0; /* 気圧 */

    pressure_state.press = (struct sensor_value){.val1 = 655, .val2 = 350000};

    /* 期待: 655.35 kPa (655350 Pa) は THERMO_PRESSURE_NONE と区別できないので, 気圧なし */
    zassert_equal(sensor_read(&temp, &humidity, &pressure), EXIT_SUCCESS);
    zassert_equal(pressure, THERMO_PRESSURE_NONE);
}

/** 温度と湿度のセンサの失敗は, 気圧のセンサがあっても, そのまま返す */
ZTEST(pressure_api, test_read_sensor_failure)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    uint16_t pressure = 0; /* 気圧 */

    sensor_state.fetch_ret = -EIO;

    /* 期待: 温度と湿度のセンサの測定の失敗 (-EIO) を返す */
    zassert_equal(sensor_read(&temp, &humidity, &pressure), -EIO);
}

ZTEST_SUITE(pressure_api, NULL, NULL, before, NULL, NULL);
