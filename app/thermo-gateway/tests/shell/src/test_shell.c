/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief cfg_shell.c の単体テスト
 *
 * cfg と cloud の関数を, FFF のモックに置き換えて, シェルのコマンド (`thermo ...`) を,
 * ダミーのバックエンドで実行して, 戻り値と出力を確認する.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <errno.h>  /* ENOENT EINVAL EIO */
#include <string.h> /* strcmp memset strstr */

#include "cfg.h"
#include "cloud.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, cfg_key_from_name, const char *)
FAKE_VALUE_FUNC(const char *, cfg_key_name, enum cfg_key)
FAKE_VALUE_FUNC(const char *, cfg_get, enum cfg_key)
FAKE_VALUE_FUNC(int, cfg_set, enum cfg_key, const char *)
FAKE_VALUE_FUNC(const char *, cfg_cred_name, enum cfg_cred)
FAKE_VALUE_FUNC(bool, cfg_has_cred, enum cfg_cred)
FAKE_VALUE_FUNC(int, cfg_save_credentials)
FAKE_VALUE_FUNC(bool, cfg_is_complete)
FAKE_VALUE_FUNC(int, cfg_reset)
FAKE_VOID_FUNC(cloud_reconnect)

/** 項目の名前 */
static const char *const key_names[] = {"ssid", "psk", "endpoint", "client_id"};
/** 証明書の種類の名前 */
static const char *const cred_names[] = {"ca", "cert", "key"};
/** cfg_get() が返す値 */
static const char *values[CFG_KEY_COUNT];
/** cfg_has_cred() が返す値 */
static bool has_cred[CFG_CRED_COUNT];

/**
 * cfg_key_from_name() のモック動作
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
 * cfg_key_name() のモック動作
 *
 * @param[in] key 項目
 * @return 名前
 */
static const char *fake_key_name(enum cfg_key key)
{
    return key_names[key];
}

/**
 * cfg_cred_name() のモック動作
 *
 * @param[in] cred 種類
 * @return 名前
 */
static const char *fake_cred_name(enum cfg_cred cred)
{
    return cred_names[cred];
}

/**
 * cfg_get() のモック動作
 *
 * @param[in] key 項目
 * @return values の値
 */
static const char *fake_get(enum cfg_key key)
{
    return values[key];
}

/**
 * cfg_has_cred() のモック動作
 *
 * @param[in] cred 種類
 * @return has_cred の値
 */
static bool fake_has_cred(enum cfg_cred cred)
{
    return has_cred[cred];
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
 * スイートの前に, シェルが使えるようになるまで待つ
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
 * 各テストの前に, モックと値を, 初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(cfg_key_from_name);
    RESET_FAKE(cfg_key_name);
    RESET_FAKE(cfg_get);
    RESET_FAKE(cfg_set);
    RESET_FAKE(cfg_cred_name);
    RESET_FAKE(cfg_has_cred);
    RESET_FAKE(cfg_save_credentials);
    RESET_FAKE(cfg_is_complete);
    RESET_FAKE(cfg_reset);
    RESET_FAKE(cloud_reconnect);
    FFF_RESET_HISTORY();
    cfg_key_from_name_fake.custom_fake = fake_key_from_name;
    cfg_key_name_fake.custom_fake = fake_key_name;
    cfg_cred_name_fake.custom_fake = fake_cred_name;
    cfg_get_fake.custom_fake = fake_get;
    cfg_has_cred_fake.custom_fake = fake_has_cred;

    (void)memset(values, 0, sizeof(values));
    (void)memset(has_cred, 0, sizeof(has_cred));
    values[CFG_KEY_SSID] = "";
    values[CFG_KEY_PSK] = "";
    values[CFG_KEY_ENDPOINT] = "";
    values[CFG_KEY_CLIENT_ID] = "";
}

/** `thermo set <項目> <値>` は, 値を保存して, 保存したことを表示する */
ZTEST(thermo_shell, test_set)
{
    int ret = 0;                                            /* 戻り値 */
    const char *out = run("thermo set ssid home-ap", &ret); /* シェルの出力 */

    /* 期待: 値を保存して, "<項目> saved" と表示する */
    zassert_equal(ret, 0);
    zassert_equal(cfg_set_fake.call_count, 1U);
    zassert_equal(cfg_set_fake.arg0_val, CFG_KEY_SSID);
    zassert_str_equal(cfg_set_fake.arg1_val, "home-ap");
    zassert_not_null(strstr(out, "ssid saved"));
}

/** 知らない項目は, -EINVAL (保存しない) */
ZTEST(thermo_shell, test_set_unknown_item)
{
    int ret = 0;                                             /* 戻り値 */
    const char *out = run("thermo set nothing value", &ret); /* シェルの出力 */

    /* 期待: 知らない項目は -EINVAL で, 保存しない (使える項目を表示する) */
    zassert_equal(ret, -EINVAL);
    zassert_equal(cfg_set_fake.call_count, 0U);
    zassert_not_null(strstr(out, "Unknown item 'nothing'"));
}

/** 保存に失敗したら, そのエラーを返して, 表示する */
ZTEST(thermo_shell, test_set_failure)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    cfg_set_fake.return_val = -EINVAL;
    out = run("thermo set psk toolong", &ret);

    /* 期待: 保存の失敗を, 戻り値と表示で伝える */
    zassert_equal(ret, -EINVAL);
    zassert_not_null(strstr(out, "Failed to set 'psk' (err -22)"));
}

/** 引数が足りなければ, 保存しない */
ZTEST(thermo_shell, test_set_missing_argument)
{
    int ret = 0; /* 戻り値 */

    (void)run("thermo set ssid", &ret);

    /* 期待: 値がなければ, シェルが拒否して, 保存しない */
    zassert_not_equal(ret, 0);
    zassert_equal(cfg_set_fake.call_count, 0U);
}

/** `thermo show` は, 値を表示して, パスワードは, 伏せる (設定済みなら "********") */
ZTEST(thermo_shell, test_show)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    /* 準備: パスワードまで設定して, 証明書は, CA とクライアント証明書だけを登録した状態 */
    values[CFG_KEY_SSID] = "home-ap";
    values[CFG_KEY_PSK] = "secret-pass";
    values[CFG_KEY_ENDPOINT] = "example.amazonaws.com";
    values[CFG_KEY_CLIENT_ID] = "gateway-01";
    has_cred[CFG_CRED_CA] = true;
    has_cred[CFG_CRED_CERT] = true;
    cfg_is_complete_fake.return_val = false;
    out = run("thermo show", &ret);

    zassert_equal(ret, 0);
    zassert_not_null(strstr(out, "ssid: home-ap"));
    /* パスワードは, 表示しない */
    zassert_not_null(strstr(out, "psk: ********"));
    zassert_is_null(strstr(out, "secret-pass"));
    zassert_not_null(strstr(out, "endpoint: example.amazonaws.com"));
    zassert_not_null(strstr(out, "client_id: gateway-01"));
    /* 証明書の登録の状態と, 接続できるか (秘密鍵が, ない) */
    zassert_not_null(strstr(out, "ca: registered"));
    zassert_not_null(strstr(out, "cert: registered"));
    zassert_not_null(strstr(out, "key: missing"));
    zassert_not_null(strstr(out, "ready to connect: no"));
}

/** パスワードが空なら "(empty)", 全て揃っていれば, 接続できると表示する */
ZTEST(thermo_shell, test_show_empty_psk_and_ready)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    cfg_is_complete_fake.return_val = true;
    out = run("thermo show", &ret);

    /* 期待: 空のパスワードは "(empty)". 揃っていれば, 接続できると表示する */
    zassert_equal(ret, 0);
    zassert_not_null(strstr(out, "psk: (empty)"));
    zassert_not_null(strstr(out, "ready to connect: yes"));
}

/** `thermo save-certs` は, 証明書を保存して, 保存したことを表示する */
ZTEST(thermo_shell, test_save_certs)
{
    int ret = 0;                                      /* 戻り値 */
    const char *out = run("thermo save-certs", &ret); /* シェルの出力 */

    /* 期待: 証明書を保存して, "certificates saved" と表示する */
    zassert_equal(ret, 0);
    zassert_equal(cfg_save_credentials_fake.call_count, 1U);
    zassert_not_null(strstr(out, "certificates saved"));
}

/** 証明書の保存に失敗したら, そのエラーを返して, 登録の手順を表示する */
ZTEST(thermo_shell, test_save_certs_failure)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    cfg_save_credentials_fake.return_val = -ENOENT;
    out = run("thermo save-certs", &ret);

    /* 期待: 失敗の戻り値と, 登録の手順 (cred add) を表示する */
    zassert_equal(ret, -ENOENT);
    zassert_not_null(strstr(out, "Failed to save the certificates (err -2)"));
    zassert_not_null(strstr(out, "cred add"));
}

/** `thermo apply` は, 接続をやり直させる */
ZTEST(thermo_shell, test_apply)
{
    int ret = 0;                                 /* 戻り値 */
    const char *out = run("thermo apply", &ret); /* シェルの出力 */

    /* 期待: 接続のやり直しを依頼して, "reconnecting" と表示する */
    zassert_equal(ret, 0);
    zassert_equal(cloud_reconnect_fake.call_count, 1U);
    zassert_not_null(strstr(out, "reconnecting"));
}

/** `thermo reset` は, 全て消して, 消したことを表示する */
ZTEST(thermo_shell, test_reset)
{
    int ret = 0;                                 /* 戻り値 */
    const char *out = run("thermo reset", &ret); /* シェルの出力 */

    /* 期待: 全て消して, "settings erased" と表示する */
    zassert_equal(ret, 0);
    zassert_equal(cfg_reset_fake.call_count, 1U);
    zassert_not_null(strstr(out, "settings erased"));
}

/** 消すのに失敗したら, そのエラーを返す */
ZTEST(thermo_shell, test_reset_failure)
{
    int ret = 0;            /* 戻り値 */
    const char *out = NULL; /* シェルの出力 */

    cfg_reset_fake.return_val = -EIO;
    out = run("thermo reset", &ret);

    /* 期待: 消すのに失敗したら, そのエラーを返して表示する */
    zassert_equal(ret, -EIO);
    zassert_not_null(strstr(out, "Failed to erase the settings (err -5)"));
}

ZTEST_SUITE(thermo_shell, NULL, setup, before, NULL, NULL);
