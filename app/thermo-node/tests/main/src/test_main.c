/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief main.c の単体テスト
 *
 * sensor と ble の関数を, FFF のモックに置き換えて, 次を確認する.
 *  - 初期化に失敗したら, 後の処理に進まず, EXIT_FAILURE を返すこと
 *  - 初期化に成功したら, 一定間隔で, 温度を読み取り続けること (別スレッドで main を動かす)
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <errno.h>  /* ENODEV EIO ENOMEM */
#include <stdint.h> /* uint16_t */
#include <stdlib.h> /* EXIT_FAILURE */

#include "ble.h"
#include "sensor.h"

DEFINE_FFF_GLOBALS

/**
 * main.c の main() (CMakeLists.txt で名前を変えている)
 *
 * @return main.c の main() の戻り値
 */
int thermo_node_main(void);

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, sensor_init)
FAKE_VALUE_FUNC(int, sensor_read_temperature, uint16_t *)
FAKE_VALUE_FUNC(int, ble_init)
FAKE_VALUE_FUNC(int, ble_advertise)
FAKE_VALUE_FUNC(int, ble_notify_temperature, uint16_t)

/** メインループのスレッドのスタックサイズ */
#define STACK_SIZE        2048
/** メインループのスレッドの優先度 */
#define THREAD_PRIORITY   5
/** main.c の読み取り間隔 [秒] (SAMPLE_INTERVAL_S) */
#define SAMPLE_INTERVAL_S 5
/** 起動直後の, 初回の読み取りを待つ時間 [ms] */
#define STARTUP_WAIT_MS   100
/** モックの温度の生値 */
#define FAKE_TEMP_RAW     1234U

/** main を動かすスレッド */
static struct k_thread main_thread;
/** main を動かすスレッドのスタック */
static K_THREAD_STACK_DEFINE(main_stack, STACK_SIZE)

/**
 * sensor_read_temperature() のモック動作 (値を書き込む)
 *
 * @param[out] value 温度の生値
 * @return 0
 */
static int fake_read(uint16_t *value)
{
    *value = (uint16_t)FAKE_TEMP_RAW;

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
 * 各テストの前に, モックを初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(sensor_init);
    RESET_FAKE(sensor_read_temperature);
    RESET_FAKE(ble_init);
    RESET_FAKE(ble_advertise);
    RESET_FAKE(ble_notify_temperature);
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
    zassert_equal(sensor_read_temperature_fake.call_count, 0U);
}

/** アドバタイズの開始に失敗したら, 温度は読まず, EXIT_FAILURE を返す */
ZTEST(main_node, test_advertise_failure)
{
    ble_advertise_fake.return_val = -ENOMEM;

    /* 期待: アドバタイズの失敗で止まる (温度は, 読まない) */
    zassert_equal(thermo_node_main(), EXIT_FAILURE);
    zassert_equal(ble_advertise_fake.call_count, 1U);
    zassert_equal(sensor_read_temperature_fake.call_count, 0U);
}

/** 初期化に成功したら, 初期化を 1 回ずつ行い, 一定間隔で温度を読み続ける */
ZTEST(main_node, test_main_loop)
{
    sensor_read_temperature_fake.custom_fake = fake_read;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    /* 起動直後: 初期化を 1 回ずつ行い, 1 回目の温度を読む */
    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(sensor_init_fake.call_count, 1U);
    zassert_equal(ble_init_fake.call_count, 1U);
    zassert_equal(ble_advertise_fake.call_count, 1U);
    zassert_equal(sensor_read_temperature_fake.call_count, 1U);

    /* 読み取った温度を, 通知する */
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    zassert_equal(ble_notify_temperature_fake.arg0_val, FAKE_TEMP_RAW);

    /* 読み取り間隔だけ待つと, 2 回目の温度を読んで, 通知する (初期化は, 繰り返さない) */
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(sensor_read_temperature_fake.call_count, 2U);
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);
    zassert_equal(sensor_init_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

/** 通知に失敗しても, 次の周期で, 再び温度を読んで, 通知する */
ZTEST(main_node, test_notify_failure_retries)
{
    sensor_read_temperature_fake.custom_fake = fake_read;
    ble_notify_temperature_fake.return_val = -EIO;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    /* 1 回目: 通知に失敗する */
    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    /* 失敗しても止まらず, 次の周期で, 再び読み取って, 通知する */
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(sensor_read_temperature_fake.call_count, 2U);
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);

    k_thread_abort(&main_thread);
}

/** 温度の読み取りに失敗しても, 次の周期で, 再び読み取る */
ZTEST(main_node, test_read_failure_retries)
{
    sensor_read_temperature_fake.return_val = -EIO;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(sensor_read_temperature_fake.call_count, 1U);
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(sensor_read_temperature_fake.call_count, 2U);

    /* 読み取りに失敗したときは, 通知しない */
    zassert_equal(ble_notify_temperature_fake.call_count, 0U);

    k_thread_abort(&main_thread);
}

ZTEST_SUITE(main_node, NULL, NULL, before, NULL, NULL);
