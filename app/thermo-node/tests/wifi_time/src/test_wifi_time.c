/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief wifi_time.c の単体テスト
 *
 * wifi_cfg と wifi_link の関数と, SNTP とソケットの関数を, FFF のモックに置き換えて, 次を確認する.
 *  - 初期化 (設定の読み込みと WiFi の準備. どちらの失敗も伝わること)
 *  - 時刻の取得の流れ (設定の SSID とパスワードで接続して, SNTP で時刻を得て, 切る)
 *  - SSID が未設定のとき, 接続しないこと
 *  - 接続, 名前の解決, SNTP の初期化と問い合わせの各段階の失敗 (失敗しても, WiFi を切ること)
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <errno.h>  /* ENOENT EHOSTUNREACH ETIMEDOUT ECONNREFUSED ENOMEM EIO */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "wifi_cfg.h"
#include "wifi_link.h"
#include "wifi_time.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, wifi_cfg_init)
FAKE_VALUE_FUNC(const char *, wifi_cfg_get, enum wifi_cfg_key)
FAKE_VALUE_FUNC(bool, wifi_cfg_is_complete)
FAKE_VALUE_FUNC(int, wifi_link_init)
FAKE_VALUE_FUNC(int, wifi_link_connect, const char *, const char *, k_timeout_t)
FAKE_VOID_FUNC(wifi_link_disconnect)
FAKE_VALUE_FUNC(int, zsock_getaddrinfo, const char *, const char *, const struct zsock_addrinfo *,
                struct zsock_addrinfo **)
FAKE_VOID_FUNC(zsock_freeaddrinfo, struct zsock_addrinfo *)
FAKE_VALUE_FUNC(int, sntp_init, struct sntp_ctx *, struct sockaddr *, socklen_t)
FAKE_VALUE_FUNC(int, sntp_query, struct sntp_ctx *, uint32_t, struct sntp_time *)
FAKE_VOID_FUNC(sntp_close, struct sntp_ctx *)

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
 * wifi_cfg_get() のモック動作 (SSID とパスワードを返す)
 *
 * @param[in] key 項目
 * @return 値
 */
static const char *cfg_get_values(enum wifi_cfg_key key)
{
    return ((key == WIFI_CFG_SSID) ? "home-ap" : "secret-pass");
}

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
 * 各テストの前にモックを初期状態に戻す (設定は揃っていて, 全て成功する)
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(wifi_cfg_init);
    RESET_FAKE(wifi_cfg_get);
    RESET_FAKE(wifi_cfg_is_complete);
    RESET_FAKE(wifi_link_init);
    RESET_FAKE(wifi_link_connect);
    RESET_FAKE(wifi_link_disconnect);
    RESET_FAKE(zsock_getaddrinfo);
    RESET_FAKE(zsock_freeaddrinfo);
    RESET_FAKE(sntp_init);
    RESET_FAKE(sntp_query);
    RESET_FAKE(sntp_close);
    FFF_RESET_HISTORY();
    wifi_cfg_get_fake.custom_fake = cfg_get_values;
    wifi_cfg_is_complete_fake.return_val = true;
    zsock_getaddrinfo_fake.custom_fake = getaddrinfo_ok;
    sntp_query_fake.custom_fake = sntp_query_ok;
}

/** 初期化は, 設定を読み込んで, WiFi の準備をする */
ZTEST(wifi_time, test_init)
{
    zassert_equal(wifi_time_init(), EXIT_SUCCESS);

    /* 期待: 両方を 1 回ずつ呼ぶ */
    zassert_equal(wifi_cfg_init_fake.call_count, 1U);
    zassert_equal(wifi_link_init_fake.call_count, 1U);
}

/** 設定の読み込みに失敗したら, そのエラーを返して, WiFi の準備には進まない */
ZTEST(wifi_time, test_init_cfg_failure)
{
    wifi_cfg_init_fake.return_val = -EIO;

    zassert_equal(wifi_time_init(), -EIO);
    zassert_equal(wifi_link_init_fake.call_count, 0U);
}

/** WiFi の準備に失敗したら, そのエラーを返す */
ZTEST(wifi_time, test_init_link_failure)
{
    wifi_link_init_fake.return_val = -ENOMEM;

    zassert_equal(wifi_time_init(), -ENOMEM);
}

/** 設定の SSID とパスワードで接続して, SNTP の時刻を返し, WiFi を切る */
ZTEST(wifi_time, test_fetch_success)
{
    int64_t unix_s = 0; /* UTC の UNIX 時刻 [s] */

    zassert_equal(wifi_time_fetch(&unix_s), EXIT_SUCCESS);

    /* 期待: 時刻を得て, 接続の引数は設定の値で, 最後に 1 回切る */
    zassert_equal(unix_s, (int64_t)TEST_SEC);
    zassert_equal(wifi_link_connect_fake.call_count, 1U);
    zassert_str_equal(wifi_link_connect_fake.arg0_val, "home-ap");
    zassert_str_equal(wifi_link_connect_fake.arg1_val, "secret-pass");
    zassert_equal(wifi_link_connect_fake.arg2_val.ticks, K_SECONDS(WIFI_TIME_CONNECT_S).ticks);
    zassert_str_equal(zsock_getaddrinfo_fake.arg0_val, "ntp.test");
    zassert_equal(sntp_query_fake.arg1_val, (uint32_t)WIFI_TIME_SNTP_S * 1000U);
    zassert_equal(sntp_close_fake.call_count, 1U);
    zassert_equal(zsock_freeaddrinfo_fake.call_count, 1U);
    zassert_equal(wifi_link_disconnect_fake.call_count, 1U);
}

/** SSID が設定されていなければ, -ENOENT で, 接続しない */
ZTEST(wifi_time, test_fetch_not_configured)
{
    int64_t unix_s = 0; /* UTC の UNIX 時刻 [s] */

    wifi_cfg_is_complete_fake.return_val = false;

    zassert_equal(wifi_time_fetch(&unix_s), -ENOENT);
    zassert_equal(wifi_link_connect_fake.call_count, 0U);
    zassert_equal(wifi_link_disconnect_fake.call_count, 0U);
}

/** 接続に失敗したら, そのエラーを返して, SNTP には進まず, WiFi を切る */
ZTEST(wifi_time, test_fetch_connect_failure)
{
    int64_t unix_s = 0; /* UTC の UNIX 時刻 [s] */

    wifi_link_connect_fake.return_val = -ECONNREFUSED;

    zassert_equal(wifi_time_fetch(&unix_s), -ECONNREFUSED);
    zassert_equal(zsock_getaddrinfo_fake.call_count, 0U);
    zassert_equal(wifi_link_disconnect_fake.call_count, 1U);
}

/** 名前を解決できなければ -EHOSTUNREACH で, SNTP には進まず, WiFi を切る */
ZTEST(wifi_time, test_fetch_resolve_failure)
{
    int64_t unix_s = 0; /* UTC の UNIX 時刻 [s] */

    zsock_getaddrinfo_fake.custom_fake = NULL;
    zsock_getaddrinfo_fake.return_val = -1;

    zassert_equal(wifi_time_fetch(&unix_s), -EHOSTUNREACH);
    zassert_equal(sntp_init_fake.call_count, 0U);
    zassert_equal(wifi_link_disconnect_fake.call_count, 1U);
}

/** SNTP の初期化に失敗したら, そのエラーを返して, 問い合わせず, WiFi を切る */
ZTEST(wifi_time, test_fetch_sntp_init_failure)
{
    int64_t unix_s = 0; /* UTC の UNIX 時刻 [s] */

    sntp_init_fake.return_val = -ENOMEM;

    zassert_equal(wifi_time_fetch(&unix_s), -ENOMEM);
    zassert_equal(sntp_query_fake.call_count, 0U);
    zassert_equal(wifi_link_disconnect_fake.call_count, 1U);
}

/** 問い合わせに失敗したら, そのエラーを返して, SNTP を閉じて, WiFi を切る */
ZTEST(wifi_time, test_fetch_sntp_query_failure)
{
    int64_t unix_s = 0; /* UTC の UNIX 時刻 [s] */

    sntp_query_fake.custom_fake = NULL;
    sntp_query_fake.return_val = -ETIMEDOUT;

    zassert_equal(wifi_time_fetch(&unix_s), -ETIMEDOUT);
    zassert_equal(sntp_close_fake.call_count, 1U);
    zassert_equal(unix_s, 0);
    zassert_equal(wifi_link_disconnect_fake.call_count, 1U);
}

ZTEST_SUITE(wifi_time, NULL, NULL, before, NULL, NULL);
