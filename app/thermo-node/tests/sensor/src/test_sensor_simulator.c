/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief sensor.c の単体テスト (シミュレーション値)
 *
 * CONFIG_SIMULATOR=y のとき, センサも ADC も使わず, 乱数で温度と湿度を作る.
 */

#include <zephyr/ztest.h>
#include <stdint.h> /* int16_t uint16_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "sensor.h"

/** シミュレーションの温度の下限 [℃ の 10 倍] (10.0 ℃ 以上) */
#define TEMP_MIN_X10       100
/** シミュレーションの温度の上限 [℃ の 10 倍] (50.0 ℃ 未満) */
#define TEMP_LIMIT_X10     500
/** シミュレーションの湿度の下限 [% の 10 倍] (30.0 % 以上) */
#define HUMIDITY_MIN_X10   300U
/** シミュレーションの湿度の上限 [% の 10 倍] (90.0 % 未満) */
#define HUMIDITY_LIMIT_X10 900U
/** 繰り返し回数 */
#define REPEAT             200

/** sensor_init() はセンサがなくても成功する */
ZTEST(sensor_simulator, test_init)
{
    /* 期待: 準備できていれば, 成功する */
    zassert_equal(sensor_init(), EXIT_SUCCESS);
}

/** sensor_read() は決められた範囲の温度と湿度を返す */
ZTEST(sensor_simulator, test_read_range)
{
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    int i = 0;             /* ループ用の添字 */

    for (i = 0; i < REPEAT; i++) {
        /* 期待: 温度と湿度は, 常に, 決められた範囲に収まる */
        zassert_equal(sensor_read(&temp, &humidity), EXIT_SUCCESS);
        zassert_true((temp >= TEMP_MIN_X10) && (temp < TEMP_LIMIT_X10), "temp=%d", temp);
        zassert_true((humidity >= HUMIDITY_MIN_X10) && (humidity < HUMIDITY_LIMIT_X10),
                     "humidity=%u", humidity);
    }
}

/** 値は毎回同じにならない (乱数で作る) */
ZTEST(sensor_simulator, test_read_varies)
{
    int16_t first = 0;     /* 最初の温度 */
    int16_t temp = 0;      /* 温度 */
    uint16_t humidity = 0; /* 湿度 */
    bool changed = false;  /* 値が変わったか */
    int i = 0;             /* ループ用の添字 */

    /* 繰り返しの間に最初と違う値が 1 回でも出れば, 値は変化している */
    zassert_equal(sensor_read(&first, &humidity), EXIT_SUCCESS);
    for (i = 0; i < REPEAT; i++) {
        zassert_equal(sensor_read(&temp, &humidity), EXIT_SUCCESS);
        if (temp != first) {
            changed = true;
        }
    }
    zassert_true(changed, "temperature is always %d", first);
}

ZTEST_SUITE(sensor_simulator, NULL, NULL, NULL, NULL, NULL);
