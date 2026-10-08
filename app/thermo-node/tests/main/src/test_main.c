/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief main.c の単体テスト
 *
 * sensor と ble と oled の関数を, FFF のモックに置き換えて, 次を確認する.
 *  - 初期化に失敗したら, 後の処理に進まず, EXIT_FAILURE を返すこと
 *  - 初期化に成功したら, 一定間隔で温度を読み取り続けること (別スレッドで main を動かす)
 *  - OLED の初期化や表示に失敗しても, 測定と通知を続けること
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <errno.h>  /* ENODEV EIO ENOMEM */
#include <stdint.h> /* int16_t uint16_t */
#include <stdlib.h> /* EXIT_FAILURE */

#include "ble.h"
#include "oled.h"
#include "sensor.h"
#include "thermo_ble_uuid.h"

DEFINE_FFF_GLOBALS

/**
 * main.c の main() (CMakeLists.txt で名前を変えている)
 *
 * @return main.c の main() の戻り値
 */
int thermo_node_main(void);

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, sensor_init)
FAKE_VALUE_FUNC(int, sensor_read, int16_t *, uint16_t *)
FAKE_VALUE_FUNC(int, ble_init)
FAKE_VALUE_FUNC(int, ble_advertise)
FAKE_VALUE_FUNC(int, ble_notify_temperature, int16_t, uint16_t)
FAKE_VALUE_FUNC(int, oled_init)
FAKE_VALUE_FUNC(int, oled_show_sample, int16_t, uint16_t)

/** メインループのスレッドのスタックサイズ */
#define STACK_SIZE        2048
/** メインループのスレッドの優先度 */
#define THREAD_PRIORITY   5
/** main.c の読み取り間隔 [秒] (SAMPLE_INTERVAL_S) */
#define SAMPLE_INTERVAL_S 5
/** 起動直後の初回の読み取りを待つ時間 [ms] */
#define STARTUP_WAIT_MS   100
/** モックの 1 回目の温度 [℃ の 10 倍] (-3.5 ℃. 負の温度の出力を通す) */
#define FAKE_TEMP_1_X10   (-35)
/** モックの 1 回目の湿度 [% の 10 倍] (45.0 %) */
#define FAKE_HUMIDITY_1   450U
/** モックの 2 回目以降の温度 [℃ の 10 倍] (23.5 ℃) */
#define FAKE_TEMP_2_X10   235

/** main を動かすスレッド */
static struct k_thread main_thread;
/** main を動かすスレッドのスタック */
static K_THREAD_STACK_DEFINE(main_stack, STACK_SIZE)

/**
 * sensor_read() のモック動作 (値を書き込む)
 *
 * 1 回目は, 負の温度と湿度. 2 回目以降は, 0 ℃ 以上の温度と, 湿度なし (ログの出力の分岐を通す).
 *
 * @param[out] temp_x10     温度 [℃ の 10 倍]
 * @param[out] humidity_x10 湿度 [% の 10 倍]
 * @return 0
 */
static int fake_read(int16_t *temp_x10, uint16_t *humidity_x10)
{
    if (sensor_read_fake.call_count == 1U) {
        *temp_x10 = (int16_t)FAKE_TEMP_1_X10;
        *humidity_x10 = (uint16_t)FAKE_HUMIDITY_1;
    } else {
        *temp_x10 = (int16_t)FAKE_TEMP_2_X10;
        *humidity_x10 = THERMO_HUMIDITY_NONE;
    }

    return 0;
}

/**
 * main を動かすスレッドの入口
 *
 * @param[in] p1 使用しない
 * @param[in] p2 使用しない
 * @param[in] p3 使用しない
 */
static void main_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    (void)thermo_node_main();
}

/**
 * 各テストの前にモックを初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(sensor_init);
    RESET_FAKE(sensor_read);
    RESET_FAKE(ble_init);
    RESET_FAKE(ble_advertise);
    RESET_FAKE(ble_notify_temperature);
    RESET_FAKE(oled_init);
    RESET_FAKE(oled_show_sample);
    FFF_RESET_HISTORY();
}

/** センサの初期化に失敗したら, BLE には進まず, EXIT_FAILURE を返す */
ZTEST(main_node, test_sensor_init_failure)
{
    sensor_init_fake.return_val = -ENODEV;

    /* 期待: センサの初期化に失敗したら, BLE には進まない */
    zassert_equal(thermo_node_main(), EXIT_FAILURE);
    zassert_equal(sensor_init_fake.call_count, 1U);
    zassert_equal(ble_init_fake.call_count, 0U);
    zassert_equal(ble_advertise_fake.call_count, 0U);
}

/** BLE の初期化に失敗したら, アドバタイズには進まず, EXIT_FAILURE を返す */
ZTEST(main_node, test_ble_init_failure)
{
    ble_init_fake.return_val = -EIO;

    /* 期待: BLE の初期化に失敗したら, アドバタイズにも読み取りにも進まない */
    zassert_equal(thermo_node_main(), EXIT_FAILURE);
    zassert_equal(sensor_init_fake.call_count, 1U);
    zassert_equal(ble_init_fake.call_count, 1U);
    zassert_equal(ble_advertise_fake.call_count, 0U);
    zassert_equal(sensor_read_fake.call_count, 0U);
}

/** アドバタイズの開始に失敗したら, 温度は読まず, EXIT_FAILURE を返す */
ZTEST(main_node, test_advertise_failure)
{
    ble_advertise_fake.return_val = -ENOMEM;

    /* 期待: アドバタイズの失敗で止まる (温度は読まない) */
    zassert_equal(thermo_node_main(), EXIT_FAILURE);
    zassert_equal(ble_advertise_fake.call_count, 1U);
    zassert_equal(sensor_read_fake.call_count, 0U);
    /* OLED は, アドバタイズの開始のあとに, 初期化する */
    zassert_equal(oled_init_fake.call_count, 0U);
}

/** 初期化に成功したら, 初期化を 1 回ずつ行い, 一定間隔で温度を読み続ける */
ZTEST(main_node, test_main_loop)
{
    sensor_read_fake.custom_fake = fake_read;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    /* 起動直後: 初期化を 1 回ずつ行い, 1 回目の温度を読む */
    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(sensor_init_fake.call_count, 1U);
    zassert_equal(ble_init_fake.call_count, 1U);
    zassert_equal(ble_advertise_fake.call_count, 1U);
    zassert_equal(sensor_read_fake.call_count, 1U);
    zassert_equal(oled_init_fake.call_count, 1U);

    /* 読み取った温度と湿度を OLED に表示する */
    zassert_equal(oled_show_sample_fake.call_count, 1U);
    zassert_equal(oled_show_sample_fake.arg0_history[0], FAKE_TEMP_1_X10);
    zassert_equal(oled_show_sample_fake.arg1_history[0], FAKE_HUMIDITY_1);

    /* 読み取った温度を通知する */
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    zassert_equal(ble_notify_temperature_fake.arg0_history[0], FAKE_TEMP_1_X10);
    zassert_equal(ble_notify_temperature_fake.arg1_history[0], FAKE_HUMIDITY_1);

    /* 読み取り間隔だけ待つと, 2 回目の温度を読んで通知する (初期化は繰り返さない) */
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(sensor_read_fake.call_count, 2U);
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);
    zassert_equal(ble_notify_temperature_fake.arg0_history[1], FAKE_TEMP_2_X10);
    zassert_equal(ble_notify_temperature_fake.arg1_history[1], THERMO_HUMIDITY_NONE);
    zassert_equal(oled_show_sample_fake.call_count, 2U);
    zassert_equal(oled_show_sample_fake.arg0_history[1], FAKE_TEMP_2_X10);
    zassert_equal(oled_show_sample_fake.arg1_history[1], THERMO_HUMIDITY_NONE);
    zassert_equal(sensor_init_fake.call_count, 1U);
    zassert_equal(oled_init_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

/** 通知に失敗しても次の周期で, 再び温度を読んで通知する */
ZTEST(main_node, test_notify_failure_retries)
{
    sensor_read_fake.custom_fake = fake_read;
    ble_notify_temperature_fake.return_val = -EIO;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    /* 1 回目: 通知に失敗する */
    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    /* 失敗しても止まらず, 次の周期で再び読み取って, 通知する */
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(sensor_read_fake.call_count, 2U);
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);

    k_thread_abort(&main_thread);
}

/** 温度の読み取りに失敗しても次の周期で, 再び読み取る */
ZTEST(main_node, test_read_failure_retries)
{
    sensor_read_fake.return_val = -EIO;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(sensor_read_fake.call_count, 1U);
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(sensor_read_fake.call_count, 2U);

    /* 読み取りに失敗したときは, 通知しない */
    zassert_equal(ble_notify_temperature_fake.call_count, 0U);

    k_thread_abort(&main_thread);
}

/** OLED の初期化に失敗しても, 表示だけを諦めて, 測定と通知を続ける */
ZTEST(main_node, test_oled_init_failure_continues)
{
    sensor_read_fake.custom_fake = fake_read;
    oled_init_fake.return_val = -ENODEV;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(oled_init_fake.call_count, 1U);
    zassert_equal(sensor_read_fake.call_count, 1U);
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);

    /* 初期化に失敗した OLED には, 表示しない */
    zassert_equal(oled_show_sample_fake.call_count, 0U);

    k_thread_abort(&main_thread);
}

/** OLED の表示に失敗しても, 通知を続けて, 次の周期で再び表示する */
ZTEST(main_node, test_oled_show_failure_retries)
{
    sensor_read_fake.custom_fake = fake_read;
    oled_show_sample_fake.return_val = -EIO;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(oled_show_sample_fake.call_count, 1U);
    /* 表示に失敗しても, 通知する */
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(oled_show_sample_fake.call_count, 2U);
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);

    k_thread_abort(&main_thread);
}

ZTEST_SUITE(main_node, NULL, NULL, before, NULL, NULL);
