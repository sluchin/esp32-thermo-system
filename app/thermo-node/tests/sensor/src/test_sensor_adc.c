/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief sensor.c の単体テスト (ADC エミュレータ)
 *
 * zephyr,user の io-channels に, ADC エミュレータ (adc0) のチャンネル 0 を接続して,
 * 実機用の経路 (adc_read_dt) をテストする. エミュレータの入力電圧と, エラーはテストから設定する.
 */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <errno.h>  /* EIO ENODEV */
#include <stdint.h> /* uint32_t int16_t uint16_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "sensor.h"
#include "thermo_ble_uuid.h"

/** エミュレータの基準電圧 [mV] (adc.overlay の ref-internal-mv) */
#define REF_MV  3300U
/** チャンネル番号 */
#define CHANNEL 0U

/** エミュレートする ADC */
static const struct device *const adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));

/**
 * 常にエラーを返す入力 (ADC の読み取りを失敗させる)
 *
 * @param[in] dev ADC
 * @param[in] chan チャンネル
 * @param[in] data 使用しない
 * @param[out] result 使用しない
 * @return 常に -EIO
 */
static int failing_input(const struct device *dev, unsigned int chan, void *data, uint32_t *result)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);
    ARG_UNUSED(data);
    ARG_UNUSED(result);

    return -EIO;
}

/**
 * 各テストの前にエミュレータの入力を 0 mV に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    zassert_true(device_is_ready(adc_dev));
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, 0U), 0);
}

/** sensor_init() は ADC が準備できていれば, 成功する */
ZTEST(sensor_adc, test_init)
{
    /* 期待: 準備できていれば, 成功する */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
}

/** ADC が準備できていなければ (初期化されていない), -ENODEV を返す */
ZTEST(sensor_adc, test_init_not_ready)
{
    struct device_state *state = adc_dev->state; /* ADC デバイスの状態 */

    state->initialized = false;
    /* 期待: ADC が初期化されていなければ -ENODEV (終わったら, 元に戻す) */
    zassert_equal(sensor_init(), -ENODEV);
    state->initialized = true;
}

/** 入力が 0 mV のとき, 温度は 0 ℃, 湿度はなし */
ZTEST(sensor_adc, test_read_zero)
{
    int16_t temp = 1;      /* 温度 (初期値の 1 が上書きされる) */
    uint16_t humidity = 0; /* 湿度 */

    /* 期待: 入力 0 mV は 0 ℃. ADC は湿度を測れない */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
    zassert_equal(sensor_read(&temp, &humidity), EXIT_SUCCESS);
    zassert_equal(temp, 0);
    zassert_equal(humidity, THERMO_HUMIDITY_NONE);
}

/** 入力が基準電圧のとき, 電圧に直して, 温度 (10 mV で 1 ℃) にする */
ZTEST(sensor_adc, test_read_full_scale)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */

    /* 期待: 3300 mV は 330.0 ℃ (3300. 換算で切り捨てるので, 誤差は 2 まで) */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, REF_MV), 0);
    zassert_equal(sensor_read(&temp, &humidity), EXIT_SUCCESS);
    zassert_within(temp, (int16_t)REF_MV, 2, "temp=%d", temp);
}

/** 入力が基準電圧の半分のとき, 温度はほぼ半分 */
ZTEST(sensor_adc, test_read_half_scale)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */

    /* 期待: 基準電圧の半分 (1650 mV) の入力で, 1650 のほぼ半分 (誤差は 2 まで) */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, REF_MV / 2U), 0);
    zassert_equal(sensor_read(&temp, &humidity), EXIT_SUCCESS);
    zassert_within(temp, (int16_t)(REF_MV / 2U), 2, "temp=%d", temp);
}

/** 電圧が高いほど, 温度も高い */
ZTEST(sensor_adc, test_read_monotonic)
{
    int16_t low = 0;       /* 低い側の温度 */
    int16_t high = 0;      /* 高い側の温度 */
    uint16_t humidity = 0; /* 湿度 */

    zassert_equal(sensor_init(), EXIT_SUCCESS);
    /* 低い電圧 (500 mV) と, 高い電圧 (2500 mV) を順に読む */
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, 500U), 0);
    zassert_equal(sensor_read(&low, &humidity), EXIT_SUCCESS);
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, 2500U), 0);
    zassert_equal(sensor_read(&high, &humidity), EXIT_SUCCESS);
    zassert_true(low < high, "low=%d high=%d", low, high);
}

/** ADC の読み取りに失敗したら, そのエラーコードを返す */
ZTEST(sensor_adc, test_read_failure)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */

    /* 期待: ADC の読み取りの失敗 (-EIO) をそのまま返す */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
    zassert_equal(adc_emul_value_func_set(adc_dev, CHANNEL, failing_input, NULL), 0);
    zassert_equal(sensor_read(&temp, &humidity), -EIO);
}

ZTEST_SUITE(sensor_adc, NULL, NULL, before, NULL, NULL);
