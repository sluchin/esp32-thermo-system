/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief history_store.c の単体テスト
 *
 * node_time_unix() と backlog_foreach_since() を FFF のモックに置き換えて, 次を確認する.
 *  - RTC に時刻が入るまでは何もしないこと (入ったら, 1 回だけ復元すること)
 *  - SD カードの記録が区切りごとの平均になって, 時刻のとおりの位置に戻ること (記録のなかった
 *    区切りは測定値なし)
 *  - 今の区切り以降の記録と, 古すぎる記録を戻さないこと
 *  - 湿度を測れない記録 (湿度なし) の扱い
 *  - 履歴にすでに点があるときは戻さないこと. SD カードを読めなくても続けること
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <errno.h>   /* ENODATA EIO */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t int64_t */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* memset */

#include "backlog.h"
#include "history.h"
#include "history_store.h"
#include "node_time.h"
#include "thermo_ble_uuid.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, node_time_unix, int64_t *)
FAKE_VALUE_FUNC(int, backlog_foreach_since, int64_t, backlog_visit_t, void *)

/** 区切りの長さ [ms] (10 秒) */
#define STEP_MS   10000
/** 区切りの長さ [s] */
#define STEP_S    10
/** 記録の置き場の大きさ */
#define STORE_MAX 16U

/** 温度の履歴 */
static struct history temp_h;
/** 湿度の履歴 */
static struct history humi_h;
/** 取り出した点 */
static int16_t out[HISTORY_SIZE];
/** SD カードにある記録 (古い順) */
static struct backlog_record store[STORE_MAX];
/** store の記録の数 */
static unsigned int store_count;
/** RTC の UNIX 時刻 [s] */
static int64_t rtc_unix_s;
/** RTC に時刻があるか */
static bool rtc_ok;
/** backlog_foreach_since() の戻り値 (記録を渡したあとに返す) */
static int foreach_result;

/**
 * node_time_unix() のモック動作
 *
 * @param[out] unix_s UNIX 時刻 [s]
 * @return rtc_ok なら EXIT_SUCCESS, でなければ -ENODATA
 */
static int fake_node_time_unix(int64_t *unix_s)
{
    *unix_s = rtc_unix_s;
    if (!rtc_ok) {
        return -ENODATA;
    }

    return EXIT_SUCCESS;
}

/**
 * backlog_foreach_since() のモック動作 (since_unix_s 以降の store の記録を古い順に渡す)
 *
 * @param[in] since_unix_s この時刻以降の記録を渡す
 * @param[in] visit        記録ごとに呼ぶ関数
 * @param[in] ctx          visit に渡す値
 * @return foreach_result
 */
static int fake_foreach(int64_t since_unix_s, backlog_visit_t visit, void *ctx)
{
    unsigned int i = 0U; /* ループ用の添字 */

    for (i = 0U; i < store_count; i++) {
        if (since_unix_s <= store[i].unix_s) {
            visit(&store[i], ctx);
        }
    }

    return foreach_result;
}

/**
 * SD カードに記録を 1 件置く
 *
 * @param[in] unix_s 測った UNIX 時刻 [s]
 * @param[in] temp   温度 [℃ の 10 倍]
 * @param[in] humi   湿度 [% の 10 倍] (THERMO_HUMIDITY_NONE は測れない)
 */
static void put(int64_t unix_s, int16_t temp, uint16_t humi)
{
    zassert_true(store_count < STORE_MAX);
    store[store_count].unix_s = unix_s;
    store[store_count].temp_x10 = temp;
    store[store_count].humidity_x10 = humi;
    store_count++;
}

/**
 * 各テストの前に, 履歴と記録を空にして, RTC に時刻がある状態にする (時刻は 1000045 秒. 起動の 2
 * 秒後)
 *
 * 区切りの番号は稼働時間 2 秒 / 10 秒 = 0, 時刻 1000045 秒 / 10 秒 = 100004. base = (1000045 - 2)
 * / 10 = 100004.
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);

    RESET_FAKE(node_time_unix);
    RESET_FAKE(backlog_foreach_since);
    FFF_RESET_HISTORY();
    node_time_unix_fake.custom_fake = fake_node_time_unix;
    backlog_foreach_since_fake.custom_fake = fake_foreach;
    (void)memset(&temp_h, 0, sizeof(temp_h));
    (void)memset(&humi_h, 0, sizeof(humi_h));
    (void)memset(out, 0, sizeof(out));
    (void)memset(store, 0, sizeof(store));
    store_count = 0U;
    rtc_unix_s = 1000045;
    rtc_ok = true;
    foreach_result = EXIT_SUCCESS;
    history_store_reset();
}

/**
 * 稼働時間 2 秒の測定値 (温度 250, 湿度 480) を加えて, 復元を進める (main.c と同じ順序)
 */
static void first_sample(void)
{
    history_add(&temp_h, 250, 2000, STEP_MS);
    history_add(&humi_h, 480, 2000, STEP_MS);
    history_store_update(&temp_h, &humi_h, 2000, STEP_MS);
}

ZTEST_SUITE(history_store, NULL, NULL, before, NULL, NULL);

/** RTC に時刻がない間は何もせず, 時刻が入ったら, 1 回だけ復元する */
ZTEST(history_store, test_waits_for_rtc_then_restores_once)
{
    put(1000000, 200, 450U);
    rtc_ok = false;

    first_sample();
    first_sample();
    zassert_equal(backlog_foreach_since_fake.call_count, 0U);

    rtc_ok = true;
    (void)memset(&temp_h, 0, sizeof(temp_h));
    (void)memset(&humi_h, 0, sizeof(humi_h));
    first_sample();
    zassert_equal(backlog_foreach_since_fake.call_count, 1U);

    first_sample();
    zassert_equal(backlog_foreach_since_fake.call_count, 1U);
}

/** 記録は区切りごとの平均になり, 時刻のとおりの位置に戻る (記録のなかった区切りは測定値なし) */
ZTEST(history_store, test_restores_averages_with_a_gap)
{
    put(1000000, 190, 440U); /* 区切り 100000: 2 件の平均 */
    put(1000005, 210, 460U);
    put(1000010, 210, 460U); /* 区切り 100001 */
    put(1000020, 220, 470U); /* 区切り 100002 */
    /* 区切り 100003: 記録なし */

    first_sample();

    /* 区切り 100000 から 100002 が履歴の区切り -4 から -2. 今の区切り 0 の手前に 1 つの欠け */
    zassert_equal(history_get(&temp_h, out, HISTORY_SIZE), 5U);
    zassert_equal(out[0], 200);
    zassert_equal(out[1], 210);
    zassert_equal(out[2], 220);
    zassert_equal(out[3], HISTORY_NONE);
    zassert_equal(out[4], 250);
    zassert_equal(history_get(&humi_h, out, HISTORY_SIZE), 5U);
    zassert_equal(out[0], 450);
    zassert_equal(out[2], 470);
    zassert_equal(out[3], HISTORY_NONE);
    zassert_equal(out[4], 480);
}

/** 記録を読む範囲は直近 128 区間 (今の区切りの手前まで) */
ZTEST(history_store, test_reads_the_last_128_buckets)
{
    first_sample();

    zassert_equal(backlog_foreach_since_fake.arg0_val, (100004 - (int64_t)HISTORY_SIZE) * STEP_S);
}

/** 今の区切り以降の記録 (この起動で測った分) は戻さない. 古すぎる記録も, 渡されても戻さない */
ZTEST(history_store, test_current_and_future_buckets_are_not_restored)
{
    put(1000020, 220, 470U); /* 区切り 100002 */
    put(1000040, 999, 999U); /* 区切り 100004: 今の区切り */
    put(1000050, 888, 888U); /* 区切り 100005: 先 */

    first_sample();

    zassert_equal(history_get(&temp_h, out, HISTORY_SIZE), 3U);
    zassert_equal(out[0], 220);
    zassert_equal(out[1], HISTORY_NONE);
    zassert_equal(out[2], 250);
}

/** 記録がなければ (電源が切れていた), 履歴は今の測定値だけ */
ZTEST(history_store, test_no_records)
{
    first_sample();

    zassert_equal(history_get(&temp_h, out, HISTORY_SIZE), 1U);
    zassert_equal(out[0], 250);
}

/** 湿度を測れない記録は湿度の平均に含めない. 区切りの全てが湿度なしなら, その区切りは測定値なし
 */
ZTEST(history_store, test_humidity_none)
{
    put(1000000, 200, 450U); /* 区切り 100000 */
    put(1000005, 200, THERMO_HUMIDITY_NONE);
    put(1000010, 210, THERMO_HUMIDITY_NONE); /* 区切り 100001: 湿度なし */
    put(1000020, 220, 470U);                 /* 区切り 100002 */

    first_sample();

    zassert_equal(history_get(&temp_h, out, HISTORY_SIZE), 5U);
    zassert_equal(out[1], 210);
    zassert_equal(history_get(&humi_h, out, HISTORY_SIZE), 5U);
    zassert_equal(out[0], 450);
    zassert_equal(out[1], HISTORY_NONE);
    zassert_equal(out[2], 470);
    zassert_equal(out[4], 480);
}

/** 履歴にすでに点があるとき (時刻が入るのが遅かったとき) は戻さない */
ZTEST(history_store, test_does_not_restore_when_history_has_points)
{
    put(1000000, 200, 450U);
    history_add(&temp_h, 100, 0, STEP_MS);
    history_add(&temp_h, 110, STEP_MS, STEP_MS); /* 区切り 0 が終わって, 1 点覚えている */

    history_store_update(&temp_h, &humi_h, 2000 + STEP_MS, STEP_MS);

    zassert_equal(backlog_foreach_since_fake.call_count, 0U);
    zassert_equal(history_get(&temp_h, out, HISTORY_SIZE), 2U);
}

/** SD カードを読めなくても (失敗しても), 読めた分を戻して, 続ける */
ZTEST(history_store, test_read_failure_keeps_going)
{
    put(1000000, 200, 450U);
    foreach_result = -EIO;

    first_sample();

    zassert_equal(history_get(&temp_h, out, HISTORY_SIZE), 5U);
    zassert_equal(out[0], 200);
    zassert_equal(out[4], 250);
}
