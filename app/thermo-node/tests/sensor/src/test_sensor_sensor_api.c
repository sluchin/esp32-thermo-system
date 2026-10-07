/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief sensor.c の単体テスト (センサ API)
 *
 * alias thermo-sensor に, 偽のセンサ (vnd,test-sensor) を接続して, DHT11 などの, センサ API の
 * 経路をテストする. センサが返す値と, エラーは, テストから設定する.
 */

/** 偽のセンサのドライバが使う compatible (vnd,test-sensor) */
#define DT_DRV_COMPAT vnd_test_sensor

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <errno.h>  /* EIO ENODEV ENOTSUP */
#include <stdint.h> /* int16_t uint16_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "sensor.h"
#include "thermo_ble_uuid.h"

/** 偽のセンサの sample_fetch の戻り値 */
static int fetch_ret;
/** 偽のセンサの温度の channel_get の戻り値 */
static int temp_ret;
/** 偽のセンサの湿度の channel_get の戻り値 */
static int humidity_ret;
/** 偽のセンサが返す温度 */
static struct sensor_value temp_value;
/** 偽のセンサが返す湿度 */
static struct sensor_value humidity_value;

/**
 * 偽のセンサの sample_fetch (設定した戻り値を返す)
 *
 * @param[in] dev  センサ (使用しない)
 * @param[in] chan チャンネル (使用しない)
 * @return fetch_ret
 */
static int fake_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);

    return fetch_ret;
}

/**
 * 偽のセンサの channel_get (チャンネルごとに, 設定した値と戻り値を返す)
 *
 * @param[in]  dev  センサ (使用しない)
 * @param[in]  chan チャンネル (温度, 湿度)
 * @param[out] val  値
 * @return チャンネルごとの戻り値. 知らないチャンネルは -ENOTSUP
 */
static int fake_channel_get(const struct device *dev, enum sensor_channel chan,
                            struct sensor_value *val)
{
    ARG_UNUSED(dev);

    if (chan == SENSOR_CHAN_AMBIENT_TEMP) {
        *val = temp_value;
        return temp_ret;
    }
    if (chan == SENSOR_CHAN_HUMIDITY) {
        *val = humidity_value;
        return humidity_ret;
    }

    return -ENOTSUP;
}

/** 偽のセンサのドライバ API */
static DEVICE_API(sensor, fake_api) = {
    .sample_fetch = fake_sample_fetch,
    .channel_get = fake_channel_get,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,
                      &fake_api)

/** 偽のセンサ */
static const struct device *const fake_dev = DEVICE_DT_GET(DT_ALIAS(thermo_sensor));

/**
 * 各テストの前に, 偽のセンサを, 23.5 ℃, 45 % の, 正常な状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    fetch_ret = 0;
    temp_ret = 0;
    humidity_ret = 0;
    temp_value = (struct sensor_value){.val1 = 23, .val2 = 500000};
    humidity_value = (struct sensor_value){.val1 = 45, .val2 = 0};
}

/** センサが準備できていれば, sensor_init() は成功する */
ZTEST(sensor_api, test_init)
{
    /* 期待: 準備できていれば, 成功する */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
}

/** センサが準備できていなければ (初期化されていない), -ENODEV を返す */
ZTEST(sensor_api, test_init_not_ready)
{
    struct device_state *state = fake_dev->state; /* センサの状態 */

    state->initialized = false;
    /* 期待: センサが初期化されていなければ -ENODEV (終わったら, 元に戻す) */
    zassert_equal(sensor_init(), -ENODEV);
    state->initialized = true;
}

/** 温度と湿度を, 10 倍の整数にして返す */
ZTEST(sensor_api, test_read)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */

    /* 期待: 23.5 ℃ は 235, 45 % は 450 */
    zassert_equal(sensor_read(&temp, &humidity), EXIT_SUCCESS);
    zassert_equal(temp, 235);
    zassert_equal(humidity, 450U);
}

/** 0 ℃ 未満の温度は, 負の値で返す */
ZTEST(sensor_api, test_read_negative_temperature)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */

    temp_value = (struct sensor_value){.val1 = -3, .val2 = -500000};

    /* 期待: -3.5 ℃ は -35 */
    zassert_equal(sensor_read(&temp, &humidity), EXIT_SUCCESS);
    zassert_equal(temp, -35);
}

/** 湿度を測れないセンサ (-ENOTSUP) は, 湿度なしとして, 成功する */
ZTEST(sensor_api, test_read_without_humidity)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */

    humidity_ret = -ENOTSUP;

    /* 期待: 温度は読めて, 湿度は THERMO_HUMIDITY_NONE */
    zassert_equal(sensor_read(&temp, &humidity), EXIT_SUCCESS);
    zassert_equal(temp, 235);
    zassert_equal(humidity, THERMO_HUMIDITY_NONE);
}

/** 湿度の取得のそのほかのエラーは, そのまま返す */
ZTEST(sensor_api, test_read_humidity_failure)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */

    humidity_ret = -EIO;

    /* 期待: -ENOTSUP 以外の失敗は, そのエラーを返す */
    zassert_equal(sensor_read(&temp, &humidity), -EIO);
}

/** 温度の取得に失敗したら, そのエラーコードを返す */
ZTEST(sensor_api, test_read_temperature_failure)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */

    temp_ret = -EIO;

    /* 期待: 温度の取得の失敗 (-EIO) をそのまま返す */
    zassert_equal(sensor_read(&temp, &humidity), -EIO);
}

/** センサの測定 (sample_fetch) に失敗したら, そのエラーコードを返す */
ZTEST(sensor_api, test_read_fetch_failure)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */

    fetch_ret = -EIO;

    /* 期待: 測定の失敗 (-EIO) をそのまま返す */
    zassert_equal(sensor_read(&temp, &humidity), -EIO);
}

ZTEST_SUITE(sensor_api, NULL, NULL, before, NULL, NULL);
