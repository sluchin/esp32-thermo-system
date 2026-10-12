/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief wifi_shell.c の単体テスト
 *
 * wifi_time と node_time の関数を FFF のモックに置き換えて, シェルのコマンド
 * (`thermo ...`) をダミーのバックエンドで実行して, 戻り値と出力を確認する.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <errno.h>  /* ENODEV ENOENT EINVAL EIO ETIMEDOUT */
#include <string.h> /* strcmp memset strstr */

#include "direct.h"
#include "node_time.h"
#include "shell_extra.h"
#include "wifi_time.h"

DEFINE_FFF_GLOBALS

/* 本物の `thermo` コマンドは, ここでは使わない. `sync` だけの `thermo` を作る */
/** 試験用の `thermo` コマンドのサブコマンド (`sync` だけ) */
SHELL_STATIC_SUBCMD_SET_CREATE(thermo_cmds, THERMO_SHELL_EXTRA SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(thermo, &thermo_cmds, "Test of thermo", NULL);

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, wifi_time_fetch, int64_t *)
FAKE_VALUE_FUNC(int, node_time_set, int64_t)
FAKE_VALUE_FUNC(int, direct_sd_check, unsigned int *)
FAKE_VALUE_FUNC(int, direct_init_status)

/** wifi_time_fetch() が返す時刻 [s] */
#define FETCHED_SEC 1790000000

/**
 * wifi_time_fetch() のモック動作 (固定の時刻を返す)
 *
 * @param[out] unix_s UNIX 時刻 [s]
 * @return 0
 */
static int fake_fetch(int64_t *unix_s)
{
    *unix_s = FETCHED_SEC;

    return 0;
}

/**
 * コマンドを実行して, 出力を返す
 *
 * @param[in] cmd コマンド
 * @param[out] ret コマンドの戻り値
 * @return 出力 (NUL で終わる)
 */
static const char *run(const char *cmd, int *ret)
{
    const struct shell *sh = shell_backend_dummy_get_ptr(); /* ダミーのシェル */
    size_t size = 0U;                                       /* サイズ [バイト] */

    shell_backend_dummy_clear_output(sh);
    *ret = shell_execute_cmd(sh, cmd);

    return shell_backend_dummy_get_output(sh, &size);
}

/**
 * スイートの前にシェルが使えるようになるまで待つ
 *
 * @return 使用しない
 */
static void *setup(void)
{
    const struct shell *sh = shell_backend_dummy_get_ptr(); /* ダミーのシェル */

    while (!shell_ready(sh)) {
        k_msleep(10);
    }

    return NULL;
}

/**
 * 各テストの前にモックと値を初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(wifi_time_fetch);
    RESET_FAKE(node_time_set);
    RESET_FAKE(direct_sd_check);
    RESET_FAKE(direct_init_status);
    FFF_RESET_HISTORY();
    wifi_time_fetch_fake.custom_fake = fake_fetch;
}

/** `thermo sync` は, WiFi で時刻を取って, RTC に設定する */
ZTEST(thermo_wifi_shell, test_sync)
{
    int ret = 0;                                /* 戻り値 */
    const char *out = run("thermo sync", &ret); /* シェルの出力 */

    /* 期待: 取った時刻を RTC に設定して, 表示する */
    zassert_equal(ret, 0);
    zassert_equal(wifi_time_fetch_fake.call_count, 1U);
    zassert_equal(node_time_set_fake.call_count, 1U);
    zassert_equal(node_time_set_fake.arg0_val, FETCHED_SEC);
    zassert_not_null(strstr(out, "time set"));
}

/** 時刻を取れなかったら, そのエラーを返して, RTC に設定しない */
ZTEST(thermo_wifi_shell, test_sync_fetch_failure)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    wifi_time_fetch_fake.custom_fake = NULL;
    wifi_time_fetch_fake.return_val = -ETIMEDOUT;
    out = run("thermo sync", &ret);

    zassert_equal(ret, -ETIMEDOUT);
    zassert_equal(node_time_set_fake.call_count, 0U);
    zassert_not_null(strstr(out, "Failed to get the time over WiFi"));
}

/** RTC への設定に失敗したら, そのエラーを返す */
ZTEST(thermo_wifi_shell, test_sync_set_failure)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    node_time_set_fake.return_val = -EIO;
    out = run("thermo sync", &ret);

    zassert_equal(ret, -EIO);
    zassert_not_null(strstr(out, "Failed to set the RTC"));
}

/**
 * direct_sd_check() のモック動作 (未送信の記録 3 件)
 *
 * @param[out] pending 未送信の記録の数
 * @return 0
 */
static int fake_sd_check(unsigned int *pending)
{
    *pending = 3U;

    return 0;
}

/** `thermo sd` は, SD カードが使えることと, 未送信の数を表示する */
ZTEST(thermo_wifi_shell, test_sd)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    direct_sd_check_fake.custom_fake = fake_sd_check;
    out = run("thermo sd", &ret);

    zassert_equal(ret, 0);
    zassert_equal(direct_sd_check_fake.call_count, 1U);
    zassert_not_null(strstr(out, "SD card is ready (3 not sent)"));
    zassert_is_null(strstr(out, "Saving is off"));
}

/** SD カードが使えても, direct_init() が失敗していたら, 保存しないことを表示する */
ZTEST(thermo_wifi_shell, test_sd_saving_off)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    direct_sd_check_fake.custom_fake = fake_sd_check;
    direct_init_status_fake.return_val = -ENOENT;
    out = run("thermo sd", &ret);

    zassert_equal(ret, 0);
    zassert_not_null(strstr(out, "Saving is off: direct_init() failed at the start (err -2)"));
}

/** SD カードが使えなければ, そのエラーを返して, 表示する */
ZTEST(thermo_wifi_shell, test_sd_failure)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    direct_sd_check_fake.return_val = -ENODEV;
    out = run("thermo sd", &ret);

    zassert_equal(ret, -ENODEV);
    zassert_not_null(strstr(out, "SD card is not available (err -19"));
}

ZTEST_SUITE(thermo_wifi_shell, NULL, setup, before, NULL, NULL);
