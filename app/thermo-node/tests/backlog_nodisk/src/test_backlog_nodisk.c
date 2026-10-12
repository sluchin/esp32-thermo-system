/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief backlog.c の単体テスト (SD カードがないとき)
 *
 * ディスクがなく, マウントできないとき, 次のようになることを確認する.
 *  - backlog_init() が -ENODEV を返すこと
 *  - 追記と, 読み出しと, 送り終えたことの記録と, 時刻以降の記録の読み出しが, 使えないこと
 * (保存した記録は, 0 件)
 */

#include <zephyr/ztest.h>
#include <errno.h>  /* ENODEV EINVAL */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "backlog.h"

/**
 * backlog_foreach_since() に渡す関数 (何もしない)
 *
 * @param[in] rec 使用しない
 * @param[in] ctx 使用しない
 */
static void ignore(const struct backlog_record *rec, void *ctx)
{
    ARG_UNUSED(rec);
    ARG_UNUSED(ctx);
}

/** マウントできないので, -ENODEV. 記録は, 0 件 */
ZTEST(backlog_nodisk, test_init_fails_without_the_disk)
{
    zassert_equal(backlog_init(), -ENODEV);
    zassert_equal(backlog_pending(), 0U);
}

/** マウントしていないので, 追記は -ENODEV, 読み出しは範囲の外 (-EINVAL), 数 0 の確定は何もしない */
ZTEST(backlog_nodisk, test_nothing_works_without_the_disk)
{
    struct backlog_record rec = {.unix_s = 1790000000LL, .temp_x10 = 235, .humidity_x10 = 450U};

    zassert_equal(backlog_init(), -ENODEV);
    zassert_equal(backlog_append(&rec, false), -ENODEV);
    zassert_equal(backlog_read(&rec, 0U), -EINVAL);
    zassert_equal(backlog_commit(0U), EXIT_SUCCESS);
    zassert_equal(backlog_commit(1U), -EINVAL);
    zassert_equal(backlog_foreach_since(0, ignore, NULL), -ENODEV);
}

ZTEST_SUITE(backlog_nodisk, NULL, NULL, NULL, NULL, NULL);
