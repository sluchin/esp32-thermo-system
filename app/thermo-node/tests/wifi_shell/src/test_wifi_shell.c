/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief wifi_shell.c の単体テスト
 *
 * wifi_cfg と wifi_time と node_time の関数を, FFF のモックに置き換えて, シェルのコマンド
 * (`thermo ...`) を, ダミーのバックエンドで実行して, 戻り値と出力を確認する.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <errno.h>  /* ENOENT EINVAL EIO ETIMEDOUT */
#include <string.h> /* strcmp memset strstr */

#include "node_time.h"
#include "wifi_cfg.h"
#include "wifi_time.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, wifi_cfg_key_from_name, const char *)
FAKE_VALUE_FUNC(const char *, wifi_cfg_key_name, enum wifi_cfg_key)
FAKE_VALUE_FUNC(const char *, wifi_cfg_get, enum wifi_cfg_key)
FAKE_VALUE_FUNC(int, wifi_cfg_set, enum wifi_cfg_key, const char *)
FAKE_VALUE_FUNC(bool, wifi_cfg_is_complete)
FAKE_VALUE_FUNC(int, wifi_cfg_reset)
FAKE_VALUE_FUNC(int, wifi_time_fetch, int64_t *)
FAKE_VALUE_FUNC(int, node_time_set, int64_t)

/** 項目の名前 */
static const char *const key_names[] = {"ssid", "psk"};
/** wifi_cfg_get() が返す値 */
static const char *values[WIFI_CFG_COUNT];
/** wifi_time_fetch() が返す時刻 [s] */
#define FETCHED_SEC 1790000000

/**
 * wifi_cfg_key_from_name() のモック動作
 *
 * @param[in] name 項目の名前
 * @return 項目. ない名前なら -ENOENT
 */
static int fake_key_from_name(const char *name)
{
    size_t i = 0U; /* ループ用の添字 */

    for (i = 0U; i < ARRAY_SIZE(key_names); i++) {
        if (strcmp(name, key_names[i]) == 0) {
            return (int)i;
        }
    }

    return -ENOENT;
}

/**
 * wifi_cfg_key_name() のモック動作
 *
 * @param[in] key 項目
 * @return 名前
 */
static const char *fake_key_name(enum wifi_cfg_key key)
{
    return key_names[key];
}

/**
 * wifi_cfg_get() のモック動作
 *
 * @param[in] key 項目
 * @return values の値
 */
static const char *fake_get(enum wifi_cfg_key key)
{
    return values[key];
}

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
 * 各テストの前にモックと値を, 初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(wifi_cfg_key_from_name);
    RESET_FAKE(wifi_cfg_key_name);
    RESET_FAKE(wifi_cfg_get);
    RESET_FAKE(wifi_cfg_set);
    RESET_FAKE(wifi_cfg_is_complete);
    RESET_FAKE(wifi_cfg_reset);
    RESET_FAKE(wifi_time_fetch);
    RESET_FAKE(node_time_set);
    FFF_RESET_HISTORY();
    wifi_cfg_key_from_name_fake.custom_fake = fake_key_from_name;
    wifi_cfg_key_name_fake.custom_fake = fake_key_name;
    wifi_cfg_get_fake.custom_fake = fake_get;
    wifi_time_fetch_fake.custom_fake = fake_fetch;

    (void)memset(values, 0, sizeof(values));
    values[WIFI_CFG_SSID] = "";
    values[WIFI_CFG_PSK] = "";
}

/** `thermo set <項目> <値>` は, 値を保存して, 保存したことを表示する */
ZTEST(thermo_wifi_shell, test_set)
{
    int ret = 0;                                            /* 戻り値 */
    const char *out = run("thermo set ssid home-ap", &ret); /* シェルの出力 */

    /* 期待: 値を保存して, "<項目> saved" と表示する */
    zassert_equal(ret, 0);
    zassert_equal(wifi_cfg_set_fake.call_count, 1U);
    zassert_equal(wifi_cfg_set_fake.arg0_val, WIFI_CFG_SSID);
    zassert_str_equal(wifi_cfg_set_fake.arg1_val, "home-ap");
    zassert_not_null(strstr(out, "ssid saved"));
}

/** 知らない項目は -EINVAL (保存しない) */
ZTEST(thermo_wifi_shell, test_set_unknown_item)
{
    int ret = 0;                                             /* 戻り値 */
    const char *out = run("thermo set nothing value", &ret); /* シェルの出力 */

    /* 期待: 知らない項目は -EINVAL で, 保存しない (使える項目を表示する) */
    zassert_equal(ret, -EINVAL);
    zassert_equal(wifi_cfg_set_fake.call_count, 0U);
    zassert_not_null(strstr(out, "Unknown item 'nothing'"));
}

/** 保存に失敗したら, そのエラーを返して, 表示する */
ZTEST(thermo_wifi_shell, test_set_failure)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    wifi_cfg_set_fake.return_val = -EIO;
    out = run("thermo set psk secret", &ret);

    zassert_equal(ret, -EIO);
    zassert_not_null(strstr(out, "Failed to set 'psk'"));
}

/** `thermo show` は, SSID と, パスワードの有無 (値は隠す) と, 揃っているかを表示する */
ZTEST(thermo_wifi_shell, test_show)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    /* 期待: 未設定のときは, パスワードは (empty) で, 揃っていない */
    out = run("thermo show", &ret);
    zassert_equal(ret, 0);
    zassert_not_null(strstr(out, "psk: (empty)"));
    zassert_not_null(strstr(out, "ready to get the time: no"));

    /* 期待: 設定済みのときは, SSID を表示して, パスワードは隠し, 揃っている */
    values[WIFI_CFG_SSID] = "home-ap";
    values[WIFI_CFG_PSK] = "secret-pass";
    wifi_cfg_is_complete_fake.return_val = true;
    out = run("thermo show", &ret);
    zassert_not_null(strstr(out, "ssid: home-ap"));
    zassert_not_null(strstr(out, "psk: ********"));
    zassert_is_null(strstr(out, "secret-pass"));
    zassert_not_null(strstr(out, "ready to get the time: yes"));
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

/** `thermo reset` は, 設定を消して, 表示する */
ZTEST(thermo_wifi_shell, test_reset)
{
    int ret = 0;                                 /* 戻り値 */
    const char *out = run("thermo reset", &ret); /* シェルの出力 */

    zassert_equal(ret, 0);
    zassert_equal(wifi_cfg_reset_fake.call_count, 1U);
    zassert_not_null(strstr(out, "settings erased"));
}

/** 消去に失敗したら, そのエラーを返す */
ZTEST(thermo_wifi_shell, test_reset_failure)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    wifi_cfg_reset_fake.return_val = -EIO;
    out = run("thermo reset", &ret);

    zassert_equal(ret, -EIO);
    zassert_not_null(strstr(out, "Failed to erase the settings"));
}

ZTEST_SUITE(thermo_wifi_shell, NULL, setup, before, NULL, NULL);
