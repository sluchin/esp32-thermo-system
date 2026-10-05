/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief ntp.c の単体テスト
 *
 * SNTP とソケットの関数を FFF のモックに置き換えて, ntp_sync() の流れを確認する (時計は,
 * Zephyr の本物を使う. sys_clock_settime() は, システムコールで, モックにできない).
 *  - 同期の成功 (時計の設定, 同期済みの状態, UNIX 時刻の取得)
 *  - 名前の解決, SNTP の初期化, 問い合わせの, 各段階の失敗
 *  - 同期済みのときの, 再同期の間隔
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/clock.h>
#include <errno.h> /* EHOSTUNREACH EAGAIN ETIMEDOUT ENOMEM */

#include "ntp.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(int, zsock_getaddrinfo, const char *, const char *, const struct zsock_addrinfo *,
                struct zsock_addrinfo **)
FAKE_VOID_FUNC(zsock_freeaddrinfo, struct zsock_addrinfo *)
FAKE_VALUE_FUNC(int, sntp_init, struct sntp_ctx *, struct sockaddr *, socklen_t)
FAKE_VALUE_FUNC(int, sntp_query, struct sntp_ctx *, uint32_t, struct sntp_time *)
FAKE_VOID_FUNC(sntp_close, struct sntp_ctx *)

/** テストの NTP サーバ */
#define SERVER   "ntp.test"
/** 再同期の間隔 [s] (CMakeLists.txt の CONFIG_THERMO_NTP_RESYNC_S) */
#define RESYNC_S 3600
/** テストの UNIX 時刻 [s] */
#define TEST_SEC 1790000000ULL

/** 名前の解決の結果のアドレス (getaddrinfo のモックが返す) */
static struct sockaddr_in resolved_addr;
/** 名前の解決の結果 (getaddrinfo のモックが返す) */
static struct zsock_addrinfo resolved = {
    .ai_addr = (struct sockaddr *)&resolved_addr,
    .ai_addrlen = sizeof(resolved_addr),
};

/**
 * zsock_getaddrinfo() のモック動作 (解決の結果を返す)
 *
 * @param[in] host 使用しない
 * @param[in] port 使用しない
 * @param[in] hints 使用しない
 * @param[out] res 解決の結果
 * @return 0
 */
static int getaddrinfo_ok(const char *host, const char *port, const struct zsock_addrinfo *hints,
                          struct zsock_addrinfo **res)
{
    ARG_UNUSED(host);
    ARG_UNUSED(port);
    ARG_UNUSED(hints);
    *res = &resolved;
    return 0;
}

/**
 * sntp_query() のモック動作 (固定の時刻を返す)
 *
 * @param[in] ctx 使用しない
 * @param[in] timeout 使用しない
 * @param[out] ts 時刻
 * @return 0
 */
static int sntp_query_ok(struct sntp_ctx *ctx, uint32_t timeout, struct sntp_time *ts)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(timeout);
    ts->seconds = TEST_SEC;
    ts->fraction = 0;
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
    RESET_FAKE(zsock_getaddrinfo);
    RESET_FAKE(zsock_freeaddrinfo);
    RESET_FAKE(sntp_init);
    RESET_FAKE(sntp_query);
    RESET_FAKE(sntp_close);
    FFF_RESET_HISTORY();
    zsock_getaddrinfo_fake.custom_fake = getaddrinfo_ok;
    sntp_query_fake.custom_fake = sntp_query_ok;
}

ZTEST_SUITE(ntp, NULL, NULL, before, NULL, NULL);

/* 同期できなかったテストを先に実行する (同期済みの状態は, リセットできないため, 名前順) */

/** 名前を解決できなければ -EHOSTUNREACH で, SNTP には進まず, 同期済みにならない */
ZTEST(ntp, test_a_resolve_failure)
{
    int64_t sec = 0; /* UNIX 時刻 [s] */

    zsock_getaddrinfo_fake.custom_fake = NULL;
    zsock_getaddrinfo_fake.return_val = -1;
    zassert_equal(ntp_sync(SERVER, K_MSEC(100)), -EHOSTUNREACH);
    zassert_equal(sntp_init_fake.call_count, 0U);
    zassert_false(ntp_is_synced());
    zassert_equal(ntp_unix_time(&sec), -EAGAIN);
}

/** 問い合わせに失敗したら, そのエラーを返して, SNTP を閉じて, 時計は設定しない */
ZTEST(ntp, test_b_query_failure)
{
    sntp_query_fake.custom_fake = NULL;
    sntp_query_fake.return_val = -ETIMEDOUT;
    zassert_equal(ntp_sync(SERVER, K_MSEC(100)), -ETIMEDOUT);
    zassert_equal(sntp_close_fake.call_count, 1U);
    zassert_false(ntp_is_synced());
}

/** SNTP の初期化に失敗したら, そのエラーを返して, 問い合わせない */
ZTEST(ntp, test_c_init_failure)
{
    sntp_init_fake.return_val = -ENOMEM;
    zassert_equal(ntp_sync(SERVER, K_MSEC(100)), -ENOMEM);
    zassert_equal(sntp_query_fake.call_count, 0U);
    zassert_false(ntp_is_synced());
}

/** 同期に成功すると時計を設定して, 同期済みになる. 間隔内の再呼び出しは, 問い合わせない */
ZTEST(ntp, test_e_success_then_skip)
{
    int64_t sec = 0; /* UNIX 時刻 [s] */

    zassert_equal(ntp_sync(SERVER, K_MSEC(100)), 0);
    zassert_true(ntp_is_synced());
    zassert_equal(ntp_unix_time(&sec), 0);
    /* 期待: 設定した時刻から, ほとんど進んでいない (本物の時計を使う) */
    zassert_within(sec, (int64_t)TEST_SEC, 5);

    /* 同期済みで, 間隔がたっていなければ, 問い合わせない */
    zassert_equal(ntp_sync(SERVER, K_MSEC(100)), 0);
    zassert_equal(sntp_query_fake.call_count, 1U);

    /* 再同期の間隔 (CONFIG_THERMO_NTP_RESYNC_S) がたてば, もう一度, 問い合わせる */
    k_sleep(K_SECONDS(RESYNC_S));
    zassert_equal(ntp_sync(SERVER, K_MSEC(100)), 0);
    zassert_equal(sntp_query_fake.call_count, 2U);
}
