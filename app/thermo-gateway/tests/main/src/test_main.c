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
#include <errno.h>  /* EIO EALREADY ENOMSG EBUSY */
#include <stdint.h> /* uint32_t uint16_t uintptr_t int16_t */
#include <stdlib.h> /* EXIT_FAILURE */
#include <string.h> /* memset */

#include "ble.h"
#include "cloud.h"
#include "switchbot.h"

DEFINE_FFF_GLOBALS

/**
 * main.c の main() (CMakeLists.txt で名前を変えている)
 *
 * @return main.c の main() の戻り値
 */
int thermo_gateway_main(void);

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, ble_init)
FAKE_VALUE_FUNC(int, ble_scan)
FAKE_VOID_FUNC(ble_set_temperature_callback, ble_temperature_cb_t)
FAKE_VOID_FUNC(ble_set_switchbot_callback, ble_switchbot_cb_t)
FAKE_VALUE_FUNC(bool, switchbot_accept, const bt_addr_le_t *, const struct switchbot_ad *, uint32_t,
                struct switchbot_sample *)
FAKE_VALUE_FUNC(int, cloud_publish_switchbot, const bt_addr_le_t *, const struct switchbot_sample *)
FAKE_VALUE_FUNC(int, cloud_init)
FAKE_VALUE_FUNC(int, cloud_publish_temperature, const bt_addr_le_t *, uint16_t)

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

/**
 * 関数のアドレス (fff.call_history の要素と, 比べる)
 * (関数ポインタを, オブジェクトポインタへ, 直接キャストすることは, ISO C では許されない)
 */
#define FUNCTION_ADDRESS(f) ((void *)(uintptr_t)(f))

/** 温度のコールバックに渡す, ノードのアドレス */
static const bt_addr_le_t test_addr = {
    .type = BT_ADDR_LE_RANDOM,
    .a = {.val = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
};
/** 温度のコールバックに渡す, 温度の生値 */
#define TEST_RAW 0x0abcu

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

/** switchbot_accept() が, 送信する値として返す温度 [℃ の 10 倍] (テストが, 変える) */
static int16_t accepted_temp_x10 = 235;
/** cloud_publish_switchbot() に渡された値の写し (引数は, 呼び出しの後は, 無効になる) */
static struct switchbot_sample published_sample;

/**
 * switchbot_accept() のモック動作 (送信する値を返す. 戻り値は, return_val で決める)
 *
 * @param[in] addr 使用しない
 * @param[in] ad 使用しない
 * @param[in] now_ms 使用しない
 * @param[out] out 送信する値
 * @return switchbot_accept_fake.return_val
 */
static bool fake_accept(const bt_addr_le_t *addr, const struct switchbot_ad *ad, uint32_t now_ms,
                        struct switchbot_sample *out)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(ad);
    ARG_UNUSED(now_ms);
    out->temp_x10 = accepted_temp_x10;
    out->humidity = 55u;
    out->battery = 87;
    return switchbot_accept_fake.return_val;
}

/**
 * cloud_publish_switchbot() のモック動作 (渡された値を写す)
 *
 * @param[in] addr 使用しない
 * @param[in] sample 送信する値
 * @return cloud_publish_switchbot_fake.return_val
 */
static int capture_switchbot(const bt_addr_le_t *addr, const struct switchbot_sample *sample)
{
    ARG_UNUSED(addr);
    published_sample = *sample;
    return cloud_publish_switchbot_fake.return_val;
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
    RESET_FAKE(ble_set_temperature_callback);
    RESET_FAKE(ble_set_switchbot_callback);
    RESET_FAKE(switchbot_accept);
    RESET_FAKE(cloud_publish_switchbot);
    RESET_FAKE(cloud_init);
    switchbot_accept_fake.custom_fake = fake_accept;
    cloud_publish_switchbot_fake.custom_fake = capture_switchbot;
    (void)memset(&published_sample, 0, sizeof(published_sample));
    RESET_FAKE(cloud_publish_temperature);
    FFF_RESET_HISTORY();
}

/** BLE の初期化に失敗したら, スキャンには進まず, EXIT_FAILURE を返す */
ZTEST(main_gateway, test_ble_init_failure)
{
    ble_init_fake.return_val = -EIO;

    /* 期待: BLE の初期化に失敗したら, クラウドの初期化にもスキャンにも進まない */
    zassert_equal(thermo_gateway_main(), EXIT_FAILURE);
    zassert_equal(ble_init_fake.call_count, 1u);
    zassert_equal(ble_scan_fake.call_count, 0u);
    zassert_equal(ble_set_temperature_callback_fake.call_count, 0u);
}

/** クラウドの初期化に失敗したら, コールバックの設定とスキャンには進まず, EXIT_FAILURE を返す */
ZTEST(main_gateway, test_cloud_init_failure)
{
    cloud_init_fake.return_val = -EIO;

    /* 期待: クラウドの初期化の失敗で止まる (コールバックは, まだ設定しない) */
    zassert_equal(thermo_gateway_main(), EXIT_FAILURE);
    zassert_equal(ble_init_fake.call_count, 1u);
    zassert_equal(cloud_init_fake.call_count, 1u);
    zassert_equal(ble_set_temperature_callback_fake.call_count, 0u);
    zassert_equal(ble_scan_fake.call_count, 0u);
}

/** スキャンの開始に失敗したら, EXIT_FAILURE を返す */
ZTEST(main_gateway, test_scan_failure)
{
    ble_scan_fake.return_val = -EALREADY;

    /* 期待: スキャンの開始の失敗で, EXIT_FAILURE (コールバックは, 設定済み) */
    zassert_equal(thermo_gateway_main(), EXIT_FAILURE);
    zassert_equal(ble_init_fake.call_count, 1u);
    zassert_equal(ble_scan_fake.call_count, 1u);
    zassert_equal(ble_set_temperature_callback_fake.call_count, 1u);
}

/** 初期化に成功したら, 初期化とスキャン開始を 1 回ずつ行い, 終了せずに動き続ける */
ZTEST(main_gateway, test_main_loop)
{
    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(ble_init_fake.call_count, 1u);
    zassert_equal(ble_scan_fake.call_count, 1u);

    /*
     * 温度のコールバックは, スキャンを始める前に設定する.
     * 呼び出しの順序: ble_init, cloud_init (コールバックが呼ばれる前に, 送信の準備をする),
     * set, scan
     */
    zassert_equal(ble_set_temperature_callback_fake.call_count, 1u);
    zassert_not_null(ble_set_temperature_callback_fake.arg0_val);
    zassert_equal(cloud_init_fake.call_count, 1u);
    zassert_equal(fff.call_history[0], FUNCTION_ADDRESS(ble_init));
    zassert_equal(fff.call_history[1], FUNCTION_ADDRESS(cloud_init));
    zassert_equal(fff.call_history[2], FUNCTION_ADDRESS(ble_set_temperature_callback));
    zassert_equal(fff.call_history[3], FUNCTION_ADDRESS(ble_set_switchbot_callback));
    zassert_equal(fff.call_history[4], FUNCTION_ADDRESS(ble_scan));
    zassert_equal(ble_set_switchbot_callback_fake.call_count, 1u);

    /* 温度のコールバックは, ノードのアドレスと温度を, クラウドの送信のキューに渡す */
    ble_set_temperature_callback_fake.arg0_val(&test_addr, TEST_RAW);
    zassert_equal(cloud_publish_temperature_fake.call_count, 1u);
    zassert_equal(cloud_publish_temperature_fake.arg0_val, &test_addr);
    zassert_equal(cloud_publish_temperature_fake.arg1_val, TEST_RAW);

    /* キューに入れられなくても (満杯など), ログに出すだけで, 問題なく戻る */
    cloud_publish_temperature_fake.return_val = -ENOMSG;
    ble_set_temperature_callback_fake.arg0_val(&test_addr, TEST_RAW);
    zassert_equal(cloud_publish_temperature_fake.call_count, 2u);

    /* 何周期か待っても, 終了せず (k_thread_join() が, EBUSY を返す), 初期化を繰り返さない */
    k_sleep(K_SECONDS(STATUS_INTERVAL_S * WAIT_CYCLES));
    zassert_equal(k_thread_join(&main_thread, K_NO_WAIT), -EBUSY);
    zassert_equal(ble_init_fake.call_count, 1u);
    zassert_equal(ble_scan_fake.call_count, 1u);

    k_thread_abort(&main_thread);
}

/** SwitchBot のコールバックは, 間引きを通った値を, クラウドの送信のキューに渡す */
ZTEST(main_gateway, test_switchbot_forwarded_to_cloud)
{
    /* 広告データ */
    const struct switchbot_ad ad = {.kind = SWITCHBOT_ENV, .temp_x10 = 235, .humidity = 55u};

    /* コールバックを取り出すため, スキャンの失敗で, main() を終わらせる */
    ble_scan_fake.return_val = -EIO;
    zassert_equal(thermo_gateway_main(), EXIT_FAILURE);

    switchbot_accept_fake.return_val = true;
    accepted_temp_x10 = 235;
    ble_set_switchbot_callback_fake.arg0_val(&test_addr, &ad);

    /* 期待: 間引きの結果の値 (23.5 ℃, 55 %, 電池 87 %) を, クラウドに渡す */
    zassert_equal(switchbot_accept_fake.call_count, 1u);
    zassert_equal(cloud_publish_switchbot_fake.call_count, 1u);
    zassert_equal(published_sample.temp_x10, 235);
    zassert_equal(published_sample.humidity, 55u);
    zassert_equal(published_sample.battery, 87);
}

/** 間引きで捨てられた値 (または, 機種と電池残量だけのデータ) は, クラウドに渡さない */
ZTEST(main_gateway, test_switchbot_dropped_by_interval)
{
    const struct switchbot_ad ad = {.kind = SWITCHBOT_INFO}; /* 広告データ */

    ble_scan_fake.return_val = -EIO;
    zassert_equal(thermo_gateway_main(), EXIT_FAILURE);

    switchbot_accept_fake.return_val = false;
    ble_set_switchbot_callback_fake.arg0_val(&test_addr, &ad);

    zassert_equal(switchbot_accept_fake.call_count, 1u);
    zassert_equal(cloud_publish_switchbot_fake.call_count, 0u);
}

/** 0 ℃ 未満の値と, クラウドに渡せなかった (キューが満杯) 場合も, ログに出すだけで, 問題なく戻る */
ZTEST(main_gateway, test_switchbot_negative_and_queue_full)
{
    const struct switchbot_ad ad = {.kind = SWITCHBOT_ENV}; /* 広告データ */

    ble_scan_fake.return_val = -EIO;
    zassert_equal(thermo_gateway_main(), EXIT_FAILURE);

    switchbot_accept_fake.return_val = true;
    accepted_temp_x10 = -53;
    cloud_publish_switchbot_fake.return_val = -ENOMSG;
    ble_set_switchbot_callback_fake.arg0_val(&test_addr, &ad);

    zassert_equal(published_sample.temp_x10, -53);
    zassert_equal(cloud_publish_switchbot_fake.call_count, 1u);
    accepted_temp_x10 = 235;
}

ZTEST_SUITE(main_gateway, NULL, NULL, before, NULL, NULL);
