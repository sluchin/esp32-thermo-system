/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief wifi_cfg.c の単体テスト
 *
 * settings の関数を, FFF のモックに置き換えて, 次を確認する.
 *  - 設定の保存, 読み出し, 消去と, エラーの扱い
 *  - フラッシュから読み込んだ設定の登録 (settings のハンドラを直接呼ぶ)
 *  - 接続に必要な設定が揃っているかの判定
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/settings/settings.h>
#include <errno.h>  /* EIO ENOENT EINVAL */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* strncpy memcpy memset strlen */

#include "wifi_cfg.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, settings_subsys_init)
FAKE_VALUE_FUNC(int, settings_register, struct settings_handler *)
FAKE_VALUE_FUNC(int, settings_load_subtree, const char *)
FAKE_VALUE_FUNC(int, settings_save_one, const char *, const void *, size_t)
FAKE_VALUE_FUNC(int, settings_delete, const char *)

/** 保存の記録の最大数 */
#define MAX_SAVES 8U
/** 1 回の保存のデータの最大サイズ */
#define SAVE_DATA 128U

/** settings_save_one() に渡された内容の記録 */
static struct {
    char name[32];           /**< 項目の名前 */
    uint8_t data[SAVE_DATA]; /**< 保存されたデータ */
    size_t len;              /**< データの長さ */
} saves[MAX_SAVES];
/** 記録した保存の回数 */
static unsigned int save_count;
/** 保存を失敗させるか */
static bool save_fails;

/** 削除の記録の最大数 */
#define MAX_DELETES 8U

/** settings_delete() に渡された項目の名前の記録 (引数のバッファは, 呼び出しごとに上書きされる) */
static char deletes[MAX_DELETES][32];
/** 記録した削除の回数 */
static unsigned int delete_count;

/** settings のハンドラに渡す, 保存されている値 */
static struct {
    const uint8_t *data; /**< 値 */
    size_t len;          /**< 値の長さ */
    int err;             /**< 負なら, 読み出しが失敗する */
} source;

/**
 * settings_save_one() のモック動作 (内容を記録する. save_fails なら失敗する)
 *
 * @param[in] name 項目の名前
 * @param[in] value 値
 * @param[in] len 値の長さ
 * @return 0, または -EIO
 */
static int fake_save_one(const char *name, const void *value, size_t len)
{
    unsigned int i = save_count; /* 記録の添字 */

    if (save_fails) {
        return -EIO;
    }
    save_count++;
    zassert_true(i < MAX_SAVES);
    zassert_true(len <= SAVE_DATA);
    (void)strncpy(saves[i].name, name, sizeof(saves[i].name) - 1U);
    (void)memcpy(saves[i].data, value, len);
    saves[i].len = len;

    return 0;
}

/**
 * settings_delete() のモック動作 (項目の名前を記録する)
 *
 * @param[in] name 項目の名前
 * @return 0
 */
static int fake_delete(const char *name)
{
    zassert_true(delete_count < MAX_DELETES);
    (void)strncpy(deletes[delete_count], name, sizeof(deletes[0]) - 1U);
    delete_count++;

    return 0;
}

/**
 * settings の read_cb のモック動作 (source の値を返す)
 *
 * @param[in] cb_arg 使用しない
 * @param[out] data 出力先
 * @param[in] len 出力先のサイズ
 * @return 読んだ長さ. source.err が負なら, そのエラー
 */
static ssize_t read_source(void *cb_arg, void *data, size_t len)
{
    size_t n = MIN(len, source.len); /* コピーする長さ [バイト] */

    ARG_UNUSED(cb_arg);
    if (source.err < 0) {
        return source.err;
    }
    (void)memcpy(data, source.data, n);

    return (ssize_t)n;
}

/**
 * ハンドラ (settings が保存された項目を読み込むときに呼ぶ) を取り出す
 *
 * @return wifi_cfg_init() が登録した, settings のハンドラ
 */
static struct settings_handler *handler_of(void)
{
    zassert_equal(wifi_cfg_init(), EXIT_SUCCESS);

    return settings_register_fake.arg0_val;
}

/**
 * ハンドラに保存されている項目を渡す
 *
 * @param[in] name 項目の名前
 * @param[in] data 値
 * @param[in] len 値の長さ
 */
static void load(const char *name, const void *data, size_t len)
{
    struct settings_handler *h = handler_of(); /* settings のハンドラ */

    source.data = (const uint8_t *)data;
    source.len = len;
    source.err = 0;
    zassert_equal(h->h_set(name, len, read_source, NULL), 0);
}

/**
 * 各テストの前にモックと記録を, 初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(settings_subsys_init);
    RESET_FAKE(settings_register);
    RESET_FAKE(settings_load_subtree);
    RESET_FAKE(settings_save_one);
    RESET_FAKE(settings_delete);
    FFF_RESET_HISTORY();
    settings_save_one_fake.custom_fake = fake_save_one;
    settings_delete_fake.custom_fake = fake_delete;

    (void)memset(deletes, 0, sizeof(deletes));
    delete_count = 0U;
    (void)memset(saves, 0, sizeof(saves));
    (void)memset(&source, 0, sizeof(source));
    save_count = 0U;
    save_fails = false;

    /* wifi_cfg.c の static な状態 (設定の値) を空に戻す */
    zassert_equal(wifi_cfg_reset(), EXIT_SUCCESS);
    RESET_FAKE(settings_delete);
    settings_delete_fake.custom_fake = fake_delete;
    (void)memset(deletes, 0, sizeof(deletes));
    delete_count = 0U;
}

/** 項目の名前から, 項目を探せる. ない名前は -ENOENT */
ZTEST(wifi_cfg, test_key_names)
{
    /* 名前から項目 */
    zassert_equal(wifi_cfg_key_from_name("ssid"), WIFI_CFG_SSID);
    zassert_equal(wifi_cfg_key_from_name("psk"), WIFI_CFG_PSK);
    zassert_equal(wifi_cfg_key_from_name("unknown"), -ENOENT);
    zassert_equal(wifi_cfg_key_from_name(""), -ENOENT);

    /* 項目から名前 */
    zassert_str_equal(wifi_cfg_key_name(WIFI_CFG_SSID), "ssid");
    zassert_str_equal(wifi_cfg_key_name(WIFI_CFG_PSK), "psk");
}

/** 設定した値は読み出せて, フラッシュに (NUL を含めて) 保存される */
ZTEST(wifi_cfg, test_set_saves_and_get)
{
    /* 期待: 取り出せて, "thermo/ssid" に, NUL を含めて, 保存される */
    zassert_equal(wifi_cfg_set(WIFI_CFG_SSID, "home-ap"), EXIT_SUCCESS);

    zassert_str_equal(wifi_cfg_get(WIFI_CFG_SSID), "home-ap");
    zassert_equal(save_count, 1U);
    zassert_str_equal(saves[0].name, "thermo/ssid");
    zassert_equal(saves[0].len, strlen("home-ap") + 1U);
    zassert_mem_equal(saves[0].data, "home-ap", strlen("home-ap") + 1U);
}

/** 空の値も設定できる (オープンネットワークのパスワード) */
ZTEST(wifi_cfg, test_set_empty_value)
{
    /* 期待: 空の値も保存できる (保存の長さは NUL の 1 byte) */
    zassert_equal(wifi_cfg_set(WIFI_CFG_PSK, "secret"), EXIT_SUCCESS);
    zassert_equal(wifi_cfg_set(WIFI_CFG_PSK, ""), EXIT_SUCCESS);

    zassert_str_equal(wifi_cfg_get(WIFI_CFG_PSK), "");
    zassert_equal(saves[1].len, 1U);
}

/** 最大の長さの値は設定できて, 1 byte でも長ければ, -EINVAL (保存も更新もしない) */
ZTEST(wifi_cfg, test_set_length_limit)
{
    char value[WIFI_CFG_PSK_MAX + 2U]; /* 値 */

    (void)memset(value, 'a', sizeof(value));

    /* 期待: SSID は 32 byte まで, パスワードは 64 byte まで */
    value[WIFI_CFG_SSID_MAX] = '\0';
    zassert_equal(wifi_cfg_set(WIFI_CFG_SSID, value), EXIT_SUCCESS);
    value[WIFI_CFG_SSID_MAX] = 'a';
    value[WIFI_CFG_SSID_MAX + 1U] = '\0';
    zassert_equal(wifi_cfg_set(WIFI_CFG_SSID, value), -EINVAL);
    zassert_equal(strlen(wifi_cfg_get(WIFI_CFG_SSID)), WIFI_CFG_SSID_MAX);

    (void)memset(value, 'a', sizeof(value));
    value[WIFI_CFG_PSK_MAX] = '\0';
    zassert_equal(wifi_cfg_set(WIFI_CFG_PSK, value), EXIT_SUCCESS);
    value[WIFI_CFG_PSK_MAX] = 'a';
    value[WIFI_CFG_PSK_MAX + 1U] = '\0';
    zassert_equal(wifi_cfg_set(WIFI_CFG_PSK, value), -EINVAL);
    zassert_equal(save_count, 2U);
}

/** 保存に失敗したら, そのエラーを返して, RAM の値を更新しない */
ZTEST(wifi_cfg, test_set_save_failure)
{
    zassert_equal(wifi_cfg_set(WIFI_CFG_SSID, "old-ap"), EXIT_SUCCESS);
    save_fails = true;

    /* 期待: 保存の失敗を返し, 値は古いまま */
    zassert_equal(wifi_cfg_set(WIFI_CFG_SSID, "new-ap"), -EIO);
    zassert_str_equal(wifi_cfg_get(WIFI_CFG_SSID), "old-ap");
}

/** SSID が空でなければ, 揃っている (パスワードは, 空でもよい) */
ZTEST(wifi_cfg, test_is_complete)
{
    zassert_false(wifi_cfg_is_complete());

    zassert_equal(wifi_cfg_set(WIFI_CFG_PSK, "secret"), EXIT_SUCCESS);
    zassert_false(wifi_cfg_is_complete());

    zassert_equal(wifi_cfg_set(WIFI_CFG_SSID, "home-ap"), EXIT_SUCCESS);
    zassert_true(wifi_cfg_is_complete());

    zassert_equal(wifi_cfg_set(WIFI_CFG_PSK, ""), EXIT_SUCCESS);
    zassert_true(wifi_cfg_is_complete());
}

/** 消去は, 全ての項目を settings から消して, 値を空にする */
ZTEST(wifi_cfg, test_reset)
{
    zassert_equal(wifi_cfg_set(WIFI_CFG_SSID, "home-ap"), EXIT_SUCCESS);
    zassert_equal(wifi_cfg_set(WIFI_CFG_PSK, "secret"), EXIT_SUCCESS);

    zassert_equal(wifi_cfg_reset(), EXIT_SUCCESS);

    /* 期待: 2 項目を消して, 値が空になる */
    zassert_equal(settings_delete_fake.call_count, 2U);
    zassert_str_equal(deletes[0], "thermo/ssid");
    zassert_str_equal(deletes[1], "thermo/psk");
    zassert_str_equal(wifi_cfg_get(WIFI_CFG_SSID), "");
    zassert_false(wifi_cfg_is_complete());
}

/** 消去に失敗しても, 残りを消して, 最初のエラーを返す */
ZTEST(wifi_cfg, test_reset_failure)
{
    int returns[] = {-EIO, -ENOENT}; /* settings_delete() の戻り値の列 */

    zassert_equal(wifi_cfg_set(WIFI_CFG_SSID, "home-ap"), EXIT_SUCCESS);
    settings_delete_fake.custom_fake = NULL;
    SET_RETURN_SEQ(settings_delete, returns, ARRAY_SIZE(returns));

    /* 期待: 最初のエラーを返すが, 2 項目とも消す */
    zassert_equal(wifi_cfg_reset(), -EIO);
    zassert_equal(settings_delete_fake.call_count, 2U);
    zassert_str_equal(wifi_cfg_get(WIFI_CFG_SSID), "");
}

/** 初期化は settings を初期化して, ハンドラを登録して, 読み込む */
ZTEST(wifi_cfg, test_init)
{
    zassert_equal(wifi_cfg_init(), EXIT_SUCCESS);

    zassert_equal(settings_subsys_init_fake.call_count, 1U);
    zassert_equal(settings_register_fake.call_count, 1U);
    zassert_str_equal(settings_load_subtree_fake.arg0_val, "thermo");
    zassert_str_equal(settings_register_fake.arg0_val->name, "thermo");
}

/** 初期化の各段階の失敗は, そのエラーを返し, 次の段階に進まない */
ZTEST(wifi_cfg, test_init_failures)
{
    settings_subsys_init_fake.return_val = -EIO;
    zassert_equal(wifi_cfg_init(), -EIO);
    zassert_equal(settings_register_fake.call_count, 0U);

    settings_subsys_init_fake.return_val = 0;
    settings_register_fake.return_val = -ENOENT;
    zassert_equal(wifi_cfg_init(), -ENOENT);
    zassert_equal(settings_load_subtree_fake.call_count, 0U);

    settings_register_fake.return_val = 0;
    settings_load_subtree_fake.return_val = -EIO;
    zassert_equal(wifi_cfg_init(), -EIO);
}

/** フラッシュから読み込んだ SSID とパスワードが, 設定に入る */
ZTEST(wifi_cfg, test_load_values)
{
    load("ssid", "saved-ap", strlen("saved-ap") + 1U);
    load("psk", "saved-pass", strlen("saved-pass") + 1U);

    zassert_str_equal(wifi_cfg_get(WIFI_CFG_SSID), "saved-ap");
    zassert_str_equal(wifi_cfg_get(WIFI_CFG_PSK), "saved-pass");
    zassert_true(wifi_cfg_is_complete());
}

/** 長さが 0 や長すぎる値と, 読み出しに失敗した値は, 無視する. 知らない項目も無視する */
ZTEST(wifi_cfg, test_load_invalid)
{
    static const char long_value[WIFI_CFG_SSID_MAX + 3U] = {'a'}; /* 長すぎる値 */
    struct settings_handler *h = NULL;                            /* settings のハンドラ */

    zassert_equal(wifi_cfg_set(WIFI_CFG_SSID, "kept"), EXIT_SUCCESS);

    /* 期待: 長さ 0 と, 長すぎる値は, 無視して, 値を変えない */
    load("ssid", "x", 0U);
    zassert_str_equal(wifi_cfg_get(WIFI_CFG_SSID), "kept");
    load("psk", long_value, sizeof(long_value) + WIFI_CFG_PSK_MAX);
    zassert_str_equal(wifi_cfg_get(WIFI_CFG_PSK), "");

    /* 期待: 読み出しに失敗したら, 値を空にする */
    h = handler_of();
    source.err = -EIO;
    zassert_equal(h->h_set("ssid", 3U, read_source, NULL), 0);
    zassert_str_equal(wifi_cfg_get(WIFI_CFG_SSID), "");

    /* 期待: 知らない項目は無視する (戻り値は 0) */
    source.err = 0;
    zassert_equal(h->h_set("unknown", 3U, read_source, NULL), 0);
}

ZTEST_SUITE(wifi_cfg, NULL, NULL, before, NULL, NULL);
