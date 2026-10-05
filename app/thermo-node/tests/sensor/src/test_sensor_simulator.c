/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief sensor.c の単体テスト (シミュレーション値)
 *
 * CONFIG_SIMULATOR=y のとき, ADC は使わず, 乱数で温度の生値を作る.
 */

#include <zephyr/ztest.h>
#include <stdint.h> /* uint16_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "sensor.h"

/** 12 bit ADC の生値の上限 (この値未満) */
#define RAW_LIMIT 4096u
/** 繰り返し回数 */
#define REPEAT    200

/** sensor_init() は, ADC がなくても成功する */
ZTEST(sensor_simulator, test_init)
{
    /* 期待: 準備できていれば、成功する */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
}

/** sensor_read_temperature() は, 12 bit の範囲 (0 .. 4095) の値を返す */
ZTEST(sensor_simulator, test_read_range)
{
    uint16_t value = 0u; /* 値 */
    int i = 0;           /* ループ用の添字 */

    for (i = 0; i < REPEAT; i++) {
        /* 期待: 生値は、常に 12 bit の範囲 (0 .. 4095) に収まる */
        zassert_equal(sensor_read_temperature(&value), EXIT_SUCCESS);
        zassert_true(value < RAW_LIMIT, "value=%u", value);
    }
}

/** 値は, 毎回同じにならない (乱数で作る) */
ZTEST(sensor_simulator, test_read_varies)
{
    uint16_t first = 0u;  /* 最初の値 */
    uint16_t value = 0u;  /* 値 */
    bool changed = false; /* 値が変わったか */
    int i = 0;            /* ループ用の添字 */

    /* 繰り返しの間に、最初と違う値が 1 回でも出れば、値は変化している */
    zassert_equal(sensor_read_temperature(&first), EXIT_SUCCESS);
    for (i = 0; i < REPEAT; i++) {
        zassert_equal(sensor_read_temperature(&value), EXIT_SUCCESS);
        if (value != first) {
            changed = true;
        }
    }
    zassert_true(changed, "value is always %u", first);
}

ZTEST_SUITE(sensor_simulator, NULL, NULL, NULL, NULL, NULL);
