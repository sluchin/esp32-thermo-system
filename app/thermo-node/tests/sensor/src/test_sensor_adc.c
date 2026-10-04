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
 * 実機用の経路 (adc_read_dt) をテストする. エミュレータの入力電圧と, エラーは, テストから設定する.
 */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "sensor.h"

/** エミュレータの基準電圧 [mV] (adc.overlay の ref-internal-mv) */
#define REF_MV  3300u
/** 12 bit ADC の生値の最大 */
#define RAW_MAX 4095u
/** チャンネル番号 */
#define CHANNEL 0u

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
 * 各テストの前に, エミュレータの入力を 0 mV に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    zassert_true(device_is_ready(adc_dev));
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, 0u), 0);
}

/** sensor_init() は, ADC が準備できていれば, 成功する */
ZTEST(sensor_adc, test_init)
{
    /* 期待: 準備できていれば、成功する */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
}

/** ADC が準備できていなければ (初期化されていない), -ENODEV を返す */
ZTEST(sensor_adc, test_init_not_ready)
{
    struct device_state *state = adc_dev->state;

    state->initialized = false;
    /* 期待: ADC が初期化されていなければ -ENODEV (終わったら、元に戻す) */
    zassert_equal(sensor_init(), -ENODEV);
    state->initialized = true;
}

/** 入力が 0 mV のとき, 生値は 0 */
ZTEST(sensor_adc, test_read_zero)
{
    uint16_t value = 1u;

    /* 期待: 入力 0 mV の生値は 0 (初期値の 1 が、上書きされる) */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
    zassert_equal(sensor_read_temperature(&value), EXIT_SUCCESS);
    zassert_equal(value, 0u);
}

/** 入力が基準電圧のとき, 生値は 12 bit の最大 (4095) */
ZTEST(sensor_adc, test_read_full_scale)
{
    uint16_t value = 0u;

    /* 期待: 基準電圧と同じ入力で、12 bit の最大値 */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, REF_MV), 0);
    zassert_equal(sensor_read_temperature(&value), EXIT_SUCCESS);
    zassert_equal(value, RAW_MAX, "value=%u", value);
}

/** 入力が基準電圧の半分のとき, 生値は, 最大のほぼ半分 */
ZTEST(sensor_adc, test_read_half_scale)
{
    uint16_t value = 0u;

    /* 期待: 基準電圧の半分の入力で、最大値のほぼ半分 (誤差は 2 まで) */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, REF_MV / 2u), 0);
    zassert_equal(sensor_read_temperature(&value), EXIT_SUCCESS);
    zassert_within(value, RAW_MAX / 2u, 2u, "value=%u", value);
}

/** 電圧が高いほど, 生値も大きい */
ZTEST(sensor_adc, test_read_monotonic)
{
    uint16_t low = 0u;
    uint16_t high = 0u;

    zassert_equal(sensor_init(), EXIT_SUCCESS);
    /* 低い電圧 (500 mV) と、高い電圧 (2500 mV) を、順に読む */
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, 500u), 0);
    zassert_equal(sensor_read_temperature(&low), EXIT_SUCCESS);
    zassert_equal(adc_emul_const_value_set(adc_dev, CHANNEL, 2500u), 0);
    zassert_equal(sensor_read_temperature(&high), EXIT_SUCCESS);
    zassert_true(low < high, "low=%u high=%u", low, high);
}

/** ADC の読み取りに失敗したら, そのエラーコードを返す */
ZTEST(sensor_adc, test_read_failure)
{
    uint16_t value = 0u;

    /* 期待: ADC の読み取りの失敗 (-EIO) を、そのまま返す */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
    zassert_equal(adc_emul_value_func_set(adc_dev, CHANNEL, failing_input, NULL), 0);
    zassert_equal(sensor_read_temperature(&value), -EIO);
}

ZTEST_SUITE(sensor_adc, NULL, NULL, before, NULL, NULL);
