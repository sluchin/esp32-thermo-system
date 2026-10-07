/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief sensor.c の単体テスト (ADC の電圧への換算の失敗)
 *
 * ADC のチャンネルのノードが Devicetree にないと, sensor_init() は失敗する. テストの中で,
 * チャンネルを設定すれば, 読み取りはできるが, 電圧への換算 (adc_raw_to_millivolts_dt) は,
 * 設定が Devicetree にないので, -ENOTSUP で失敗する.
 */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <errno.h>  /* ENOTSUP */
#include <stdint.h> /* int16_t uint16_t */

#include "sensor.h"

/** エミュレートする ADC */
static const struct device *const adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));

/** チャンネルの設定がないので, sensor_init() は -ENOTSUP を返す */
ZTEST(sensor_adc_convert_error, test_init_without_channel)
{
    /* 期待: チャンネルのノードがない (設定を反映できない) ので, -ENOTSUP */
    zassert_equal(sensor_init(), -ENOTSUP);
}

/** 電圧への換算ができなければ, そのエラーコードを返す */
ZTEST(sensor_adc_convert_error, test_read_convert_failure)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    int ret = 0;           /* 戻り値 */
    struct adc_channel_cfg cfg = {
        .gain = ADC_GAIN_1,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id = 0U,
    }; /* テストの中で設定するチャンネル (Devicetree には, ない) */

    /* 期待: チャンネルを設定すれば, 生値は読めるが, 換算に必要な設定がないので, -ENOTSUP */
    zassert_equal(adc_channel_setup(adc_dev, &cfg), 0);
    ret = sensor_read(&temp, &humidity);
    zassert_equal(ret, -ENOTSUP, "ret=%d", ret);
}

ZTEST_SUITE(sensor_adc_convert_error, NULL, NULL, NULL, NULL, NULL);
