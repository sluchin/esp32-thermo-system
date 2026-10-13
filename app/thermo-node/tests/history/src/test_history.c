/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief history.c の単体テスト
 *
 * 次を確認する.
 *  - 同じ区切りの中の測定値は平均 1 点になること (今の区切りの平均も取り出せる)
 *  - 区切りが変わると, 前の区切りの平均を覚えること
 *  - 満杯になったら, 古い点から上書きして, 古い順に取り出せること
 *  - 取り出す数が少ないときは新しい点だけを取り出すこと
 */

#include <zephyr/ztest.h>
#include <stdint.h> /* int16_t int64_t */
#include <string.h> /* memset */

#include "history.h"

/** テストの区切りの長さ [ms] (10 秒) */
#define STEP_MS 10000

/** テストで使う履歴 */
static struct history h;
/** 取り出した点 */
static int16_t out[HISTORY_SIZE];

/**
 * 各テストの前に履歴を空にする
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)memset(&h, 0, sizeof(h));
    (void)memset(out, 0, sizeof(out));
}

/** 何も加えていなければ, 取り出せる点は 0 */
ZTEST(history, test_get_empty)
{
    zassert_equal(history_get(&h, out, HISTORY_SIZE), 0U);
}

/** 同じ区切りの中の測定値は平均 1 点 (今の区切りの平均) として取り出せる */
ZTEST(history, test_get_current_bucket_average)
{
    history_add(&h, 200, 0, STEP_MS);
    history_add(&h, 210, 5000, STEP_MS);
    history_add(&h, 220, 9999, STEP_MS);

    zassert_equal(history_get(&h, out, HISTORY_SIZE), 1U);
    zassert_equal(out[0], 210);
}

/** 区切りが変わると, 前の区切りの平均を覚えて, 今の区切りの平均が最後の点になる */
ZTEST(history, test_bucket_change_keeps_previous_average)
{
    history_add(&h, 200, 0, STEP_MS);
    history_add(&h, 220, 5000, STEP_MS);
    history_add(&h, 300, 10000, STEP_MS);

    zassert_equal(history_get(&h, out, HISTORY_SIZE), 2U);
    zassert_equal(out[0], 210);
    zassert_equal(out[1], 300);
}

/** 平均は 0 に向かって切り捨てる (負の値も, 同じ) */
ZTEST(history, test_average_truncates_toward_zero)
{
    history_add(&h, -35, 0, STEP_MS);
    history_add(&h, -36, 1000, STEP_MS);

    zassert_equal(history_get(&h, out, HISTORY_SIZE), 1U);
    zassert_equal(out[0], -35);
}

/** 満杯 (HISTORY_SIZE 点) になったら, 古い点を上書きして, 古い順に新しい 128 点を取り出す */
ZTEST(history, test_wraps_and_returns_oldest_first)
{
    unsigned int i = 0U; /* 区切りの番号 */
    unsigned int n = 0U; /* 取り出した点の数 */

    /* 区切り 0 から 129 まで (130 区切り) に値 = 区切りの番号を加える */
    for (i = 0U; i < HISTORY_SIZE + 2U; i++) {
        history_add(&h, (int16_t)i, (int64_t)i * STEP_MS, STEP_MS);
    }

    /* 覚えているのは区切り 1 から 129 の 129 点 (取り出すのは新しい 128 点) */
    n = history_get(&h, out, HISTORY_SIZE);
    zassert_equal(n, HISTORY_SIZE);
    zassert_equal(out[0], 2, "oldest");
    zassert_equal(out[HISTORY_SIZE - 2U], 128);
    zassert_equal(out[HISTORY_SIZE - 1U], 129, "newest (the current bucket)");
}

/** 取り出す数が少ないときは新しい点だけを古い順に取り出す */
ZTEST(history, test_get_limited_returns_newest)
{
    history_add(&h, 10, 0, STEP_MS);
    history_add(&h, 20, 10000, STEP_MS);
    history_add(&h, 30, 20000, STEP_MS);

    zassert_equal(history_get(&h, out, 2U), 2U);
    zassert_equal(out[0], 20);
    zassert_equal(out[1], 30);
}

/** 測定値のなかった区切りは HISTORY_NONE の点になる (途中の区切りも, 今の区切りの手前も) */
ZTEST(history, test_gap_becomes_none)
{
    history_add(&h, 10, 0, STEP_MS);
    history_add(&h, 40, 3 * STEP_MS, STEP_MS);

    /* 区切り 1 と 2 は測定値なし */
    zassert_equal(history_get(&h, out, HISTORY_SIZE), 4U);
    zassert_equal(out[0], 10);
    zassert_equal(out[1], HISTORY_NONE);
    zassert_equal(out[2], HISTORY_NONE);
    zassert_equal(out[3], 40);

    /* 次の点が覚えられても, 間は同じ */
    history_add(&h, 50, 5 * STEP_MS, STEP_MS);
    zassert_equal(history_get(&h, out, HISTORY_SIZE), 6U);
    zassert_equal(out[3], 40);
    zassert_equal(out[4], HISTORY_NONE);
    zassert_equal(out[5], 50);
    zassert_equal(history_last(&h), 40);
}

/** 測定値のない区切りが多すぎても, 点の数は HISTORY_SIZE まで (最新の点が最後) */
ZTEST(history, test_gap_is_limited)
{
    unsigned int n = 0U; /* 取り出した点の数 */

    history_add(&h, 10, 0, STEP_MS);
    history_add(&h, 20, 1000 * STEP_MS, STEP_MS);
    history_add(&h, 30, 2000 * STEP_MS, STEP_MS);

    n = history_get(&h, out, HISTORY_SIZE);
    zassert_equal(n, HISTORY_SIZE);
    zassert_equal(out[0], HISTORY_NONE);
    zassert_equal(out[HISTORY_SIZE - 2U], HISTORY_NONE);
    zassert_equal(out[HISTORY_SIZE - 1U], 30);
    zassert_equal(history_last(&h), 20);
}

/** 保存してあった点を戻す: 間の区切りは HISTORY_NONE. 今の区切りの測定値は変わらない */
ZTEST(history, test_restore)
{
    history_restore(&h, 2, 20);
    history_restore(&h, 5, 50);
    zassert_equal(history_last(&h), 50);

    /* 戻しただけでは今の区切りの点はない */
    zassert_equal(history_get(&h, out, HISTORY_SIZE), 4U);
    zassert_equal(out[0], 20);
    zassert_equal(out[1], HISTORY_NONE);
    zassert_equal(out[2], HISTORY_NONE);
    zassert_equal(out[3], 50);

    /* 区切り 6 から 8 までの間は測定値なし. 区切り 9 の測定値が今の点 */
    history_add(&h, 90, 9 * STEP_MS, STEP_MS);
    zassert_equal(history_get(&h, out, HISTORY_SIZE), 8U);
    zassert_equal(out[3], 50);
    zassert_equal(out[4], HISTORY_NONE);
    zassert_equal(out[6], HISTORY_NONE);
    zassert_equal(out[7], 90);

    /* 区切り 9 の次の区切り 10 の測定値で, 区切り 9 の点が覚えられる (間はない) */
    history_add(&h, 100, 10 * STEP_MS, STEP_MS);
    zassert_equal(history_get(&h, out, HISTORY_SIZE), 9U);
    zassert_equal(out[7], 90);
    zassert_equal(out[8], 100);
}

ZTEST_SUITE(history, NULL, NULL, before, NULL, NULL);
