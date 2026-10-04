/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief main.c の単体テスト
 *
 * ble の関数を, FFF のモックに置き換えて, 次を確認する.
 *  - 初期化に失敗したら, 後の処理に進まず, EXIT_FAILURE を返すこと
 *  - 初期化に成功したら, 終了せずに, 稼働状況の出力を続けること (別スレッドで main を動かす)
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdlib.h>

#include "ble.h"

DEFINE_FFF_GLOBALS

/** main.c の main() (CMakeLists.txt で名前を変えている) */
int thermo_gateway_main(void);

FAKE_VALUE_FUNC(int, ble_init)
FAKE_VALUE_FUNC(int, ble_scan)

/** メインループのスレッドのスタックサイズ */
#define STACK_SIZE        2048
/** メインループのスレッドの優先度 */
#define THREAD_PRIORITY   5
/** main.c の稼働状況の出力間隔 [秒] (STATUS_INTERVAL_S) */
#define STATUS_INTERVAL_S 10
/** 起動直後を待つ時間 [ms] */
#define STARTUP_WAIT_MS   100
/** 何周期ぶん待つか */
#define WAIT_CYCLES       3

/** main を動かすスレッド */
static struct k_thread main_thread;
/** main を動かすスレッドのスタック */
static K_THREAD_STACK_DEFINE(main_stack, STACK_SIZE)

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
    (void)thermo_gateway_main();
}

/**
 * 各テストの前に, モックを初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(ble_init);
    RESET_FAKE(ble_scan);
    FFF_RESET_HISTORY();
}

/** BLE の初期化に失敗したら, スキャンには進まず, EXIT_FAILURE を返す */
ZTEST(main_gateway, test_ble_init_failure)
{
    ble_init_fake.return_val = -EIO;

    zassert_equal(thermo_gateway_main(), EXIT_FAILURE);
    zassert_equal(ble_init_fake.call_count, 1u);
    zassert_equal(ble_scan_fake.call_count, 0u);
}

/** スキャンの開始に失敗したら, EXIT_FAILURE を返す */
ZTEST(main_gateway, test_scan_failure)
{
    ble_scan_fake.return_val = -EALREADY;

    zassert_equal(thermo_gateway_main(), EXIT_FAILURE);
    zassert_equal(ble_init_fake.call_count, 1u);
    zassert_equal(ble_scan_fake.call_count, 1u);
}

/** 初期化に成功したら, 初期化とスキャン開始を 1 回ずつ行い, 終了せずに動き続ける */
ZTEST(main_gateway, test_main_loop)
{
    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(ble_init_fake.call_count, 1u);
    zassert_equal(ble_scan_fake.call_count, 1u);

    /* 何周期か待っても, 終了せず (k_thread_join() が, EBUSY を返す), 初期化を繰り返さない */
    k_sleep(K_SECONDS(STATUS_INTERVAL_S * WAIT_CYCLES));
    zassert_equal(k_thread_join(&main_thread, K_NO_WAIT), -EBUSY);
    zassert_equal(ble_init_fake.call_count, 1u);
    zassert_equal(ble_scan_fake.call_count, 1u);

    k_thread_abort(&main_thread);
}

ZTEST_SUITE(main_gateway, NULL, NULL, before, NULL, NULL);
