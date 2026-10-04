/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief sensor.c の単体テスト (ADC チャンネルの設定の失敗)
 *
 * エミュレータが対応していない基準電圧を指定して, sensor_init() のエラー経路を確認する.
 */

#include <zephyr/ztest.h>
#include <errno.h>

#include "sensor.h"

/** チャンネルの設定に失敗したら, そのエラーコードを返す */
ZTEST(sensor_adc_setup_error, test_init_setup_failure)
{
    zassert_equal(sensor_init(), -ENOTSUP);
}

ZTEST_SUITE(sensor_adc_setup_error, NULL, NULL, NULL, NULL, NULL);
