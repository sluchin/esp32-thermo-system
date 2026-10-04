/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief ble.c の単体テスト
 *
 * Bluetooth スタックの関数 (bt_enable, bt_le_scan_start) を, FFF のモックに置き換えて,
 * ble_init() と ble_scan() が, 正しい引数で呼び出し, エラーを返すことを確認する.
 * スキャンのコールバックは, bt_le_scan_start() に渡された関数を取り出して, 呼び出す.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ble.h"

DEFINE_FFF_GLOBALS

/** 期待するスキャン間隔 [ms] (ble.c の SCAN_INTERVAL_MS) */
#define EXPECTED_INTERVAL_MS 100
/** 期待するスキャンウィンドウ [ms] (ble.c の SCAN_WINDOW_MS) */
#define EXPECTED_WINDOW_MS   50
/** テストで使う受信信号強度 [dBm] */
#define TEST_RSSI            (-60)

/**
 * スキャンのコールバックの関数ポインタ型
 * (bt_le_scan_cb_t は, ポインタではなく, 関数型の typedef なので, FFF の引数には使えない.
 *  関数の引数に書いた関数型は, 関数ポインタとして扱われるので, 同じ型になる)
 */
typedef bt_le_scan_cb_t *scan_cb_ptr_t;

FAKE_VALUE_FUNC(int, bt_enable, bt_ready_cb_t)
FAKE_VALUE_FUNC(int, bt_le_scan_start, const struct bt_le_scan_param *, scan_cb_ptr_t)

/** bt_le_scan_start() の引数 (呼び出しの後は, 引数の指す先が無効になるので, 写しを残す) */
static struct {
    struct bt_le_scan_param param; /**< スキャンパラメータ */
    scan_cb_ptr_t cb;              /**< スキャンのコールバック */
} captured;

/**
 * bt_le_scan_start() のモック動作 (引数の写しを残す)
 *
 * @param[in] param スキャンパラメータ
 * @param[in] cb スキャンのコールバック
 * @return 0
 */
static int capture_scan_start(const struct bt_le_scan_param *param, scan_cb_ptr_t cb)
{
    captured.param = *param;
    captured.cb = cb;
    return 0;
}

/**
 * 各テストの前に, モックを初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(bt_enable);
    RESET_FAKE(bt_le_scan_start);
    FFF_RESET_HISTORY();
    (void)memset(&captured, 0, sizeof(captured));
}

/** ble_init() は, bt_enable(NULL) を 1 回呼んで, 成功する */
ZTEST(ble_gateway, test_init_success)
{
    bt_enable_fake.return_val = 0;

    zassert_equal(ble_init(), EXIT_SUCCESS);
    zassert_equal(bt_enable_fake.call_count, 1u);
    zassert_is_null(bt_enable_fake.arg0_val);
}

/** ble_init() は, bt_enable() のエラーを, そのまま返す */
ZTEST(ble_gateway, test_init_failure)
{
    bt_enable_fake.return_val = -EIO;

    zassert_equal(ble_init(), -EIO);
    zassert_equal(bt_enable_fake.call_count, 1u);
}

/** ble_scan() は, アクティブスキャンを, 決められた間隔とウィンドウで始める */
ZTEST(ble_gateway, test_scan_success)
{
    bt_le_scan_start_fake.custom_fake = capture_scan_start;

    zassert_equal(ble_scan(), EXIT_SUCCESS);
    zassert_equal(bt_le_scan_start_fake.call_count, 1u);

    zassert_equal(captured.param.type, BT_LE_SCAN_TYPE_ACTIVE);
    zassert_equal(captured.param.options, BT_LE_SCAN_OPT_NONE);
    zassert_equal(captured.param.interval, BT_GAP_MS_TO_SCAN_INTERVAL(EXPECTED_INTERVAL_MS));
    zassert_equal(captured.param.window, BT_GAP_MS_TO_SCAN_WINDOW(EXPECTED_WINDOW_MS));
    /* ウィンドウは, 間隔を超えない (超えると, スキャンを始められない) */
    zassert_true(captured.param.window <= captured.param.interval);
    zassert_not_null(captured.cb);
}

/** ble_scan() は, bt_le_scan_start() のエラーを, そのまま返す */
ZTEST(ble_gateway, test_scan_failure)
{
    bt_le_scan_start_fake.return_val = -EALREADY;

    zassert_equal(ble_scan(), -EALREADY);
    zassert_equal(bt_le_scan_start_fake.call_count, 1u);
}

/** スキャンのコールバックは, アドレスと, 付加データなし (NULL) で呼んでも, 問題なく戻る */
ZTEST(ble_gateway, test_scan_callback)
{
    const bt_addr_le_t addr = {
            .type = BT_ADDR_LE_PUBLIC,
            .a = {.val = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
    };

    bt_le_scan_start_fake.custom_fake = capture_scan_start;
    zassert_equal(ble_scan(), EXIT_SUCCESS);
    zassert_not_null(captured.cb);

    /* 検出されたデバイスの通知. アドバタイズの種別と, データは, 使わない */
    captured.cb(&addr, TEST_RSSI, BT_GAP_ADV_TYPE_ADV_IND, NULL);
    captured.cb(&addr, 0, BT_GAP_ADV_TYPE_ADV_NONCONN_IND, NULL);
}

ZTEST_SUITE(ble_gateway, NULL, NULL, before, NULL, NULL);
