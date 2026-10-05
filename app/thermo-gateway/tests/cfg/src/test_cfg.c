/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief cfg.c の単体テスト
 *
 * settings と TLS の認証情報の関数を, FFF のモックに置き換えて, 次を確認する.
 *  - 設定の保存, 読み出し, 消去と, エラーの扱い
 *  - フラッシュから読み込んだ設定と証明書の登録 (settings のハンドラを直接呼ぶ)
 *  - 接続に必要な設定が揃っているかの判定
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/settings/settings.h>
#include <errno.h>  /* EIO ENOENT EFBIG EINVAL ENOMEM EBUSY etc... */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* strncpy memcpy memset strlen */

#include "cfg.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, settings_subsys_init)
FAKE_VALUE_FUNC(int, settings_register, struct settings_handler *)
FAKE_VALUE_FUNC(int, settings_load_subtree, const char *)
FAKE_VALUE_FUNC(int, settings_save_one, const char *, const void *, size_t)
FAKE_VALUE_FUNC(int, settings_delete, const char *)
FAKE_VALUE_FUNC(int, tls_credential_add, sec_tag_t, enum tls_credential_type, const void *, size_t)
FAKE_VALUE_FUNC(int, tls_credential_get, sec_tag_t, enum tls_credential_type, void *, size_t *)
FAKE_VALUE_FUNC(int, tls_credential_delete, sec_tag_t, enum tls_credential_type)

/** 保存の記録の最大数 */
#define MAX_SAVES  32U
/** 1 回の保存の, データの最大サイズ */
#define SAVE_DATA  2048U
/** 証明書の種類の数 */
#define CRED_COUNT 3U

/** settings_save_one() に渡された内容の記録 */
static struct {
    char name[32];           /**< 項目の名前 */
    uint8_t data[SAVE_DATA]; /**< 保存されたデータ */
    size_t len;              /**< データの長さ */
} saves[MAX_SAVES];
/** 記録した保存の回数 */
static unsigned int save_count;
/** この番号 (0 から) の保存を失敗させる (負なら, 失敗させない) */
static int save_fail_index = -1;

/** TLS の認証情報の, 代わりの保存先 (種類は, TLS_CREDENTIAL_CA_CERTIFICATE が 1 から続く) */
static struct {
    const void *data; /**< 登録されたデータ (コピーしない. 実物と同じ) */
    size_t len;       /**< データの長さ */
    bool present;     /**< 登録されているか */
} store[CRED_COUNT];

/** settings のハンドラに渡す, 保存されている値 */
static struct {
    const uint8_t *data; /**< 値 */
    size_t len;          /**< 値の長さ */
    int err;             /**< 負なら, 読み出しが失敗する */
} source;

/**
 * settings_save_one() のモック動作 (内容を記録する. 指定の番号では, 失敗する)
 *
 * @param[in] name 項目の名前
 * @param[in] value 値
 * @param[in] len 値の長さ
 * @return 0, または, -EIO (save_fail_index の番号のとき)
 */
static int fake_save_one(const char *name, const void *value, size_t len)
{
    unsigned int i = save_count; /* ループ用の添字 */

    save_count++;
    if ((int)i == save_fail_index) {
        return -EIO;
    }
    zassert_true(i < MAX_SAVES);
    zassert_true(len <= SAVE_DATA);
    (void)strncpy(saves[i].name, name, sizeof(saves[i].name) - 1U);
    (void)memcpy(saves[i].data, value, len);
    saves[i].len = len;
    return 0;
}

/**
 * tls_credential_add() のモック動作 (データのポインタを保持する)
 *
 * @param[in] tag 使用しない
 * @param[in] type 種類
 * @param[in] cred データ
 * @param[in] credlen データの長さ
 * @return 0
 */
static int fake_cred_add(sec_tag_t tag, enum tls_credential_type type, const void *cred,
                         size_t credlen)
{
    ARG_UNUSED(tag);
    store[type - TLS_CREDENTIAL_CA_CERTIFICATE].data = cred;
    store[type - TLS_CREDENTIAL_CA_CERTIFICATE].len = credlen;
    store[type - TLS_CREDENTIAL_CA_CERTIFICATE].present = true;
    return 0;
}

/**
 * tls_credential_get() のモック動作 (ない: -ENOENT, 領域が小さい: -EFBIG)
 *
 * @param[in] tag 使用しない
 * @param[in] type 種類
 * @param[out] cred データの出力先
 * @param[in,out] credlen 出力先のサイズ. 登録されているデータの長さ
 * @return 0, -ENOENT, または -EFBIG
 */
static int fake_cred_get(sec_tag_t tag, enum tls_credential_type type, void *cred, size_t *credlen)
{
    size_t i = (size_t)type - (size_t)TLS_CREDENTIAL_CA_CERTIFICATE; /* ループ用の添字 */

    ARG_UNUSED(tag);
    if (!store[i].present) {
        return -ENOENT;
    }
    if (*credlen < store[i].len) {
        *credlen = store[i].len;
        return -EFBIG;
    }
    (void)memcpy(cred, store[i].data, store[i].len);
    *credlen = store[i].len;
    return 0;
}

/**
 * tls_credential_delete() のモック動作
 *
 * @param[in] tag 使用しない
 * @param[in] type 種類
 * @return 0, または, 登録されていなければ -ENOENT
 */
static int fake_cred_delete(sec_tag_t tag, enum tls_credential_type type)
{
    size_t i = (size_t)type - (size_t)TLS_CREDENTIAL_CA_CERTIFICATE; /* ループ用の添字 */
    bool present = store[i].present;                                 /* 保存されているか */
    int ret = (present ? 0 : -ENOENT);                               /* 戻り値 */

    ARG_UNUSED(tag);
    store[i].present = false;
    return ret;
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
 * ハンドラ (settings が, 保存された項目を読み込むときに呼ぶ) を取り出す
 *
 * @return cfg_init() が登録した, settings のハンドラ
 */
static struct settings_handler *handler_of(void)
{
    zassert_equal(cfg_init(), EXIT_SUCCESS);
    return settings_register_fake.arg0_val;
}

/**
 * ハンドラに, 保存されている項目を渡す
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
 * 3 つの証明書を, TLS の認証情報に登録する
 */
static void register_all_creds(void)
{
    static const char ca[] = "CA-PEM";     /* CA 証明書 (テスト用) */
    static const char cert[] = "CERT-PEM"; /* クライアント証明書 (テスト用) */
    static const char key[] = "KEY-PEM";   /* 設定項目の番号 */

    (void)fake_cred_add(CFG_TLS_SEC_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, ca, sizeof(ca));
    (void)fake_cred_add(CFG_TLS_SEC_TAG, TLS_CREDENTIAL_PUBLIC_CERTIFICATE, cert, sizeof(cert));
    (void)fake_cred_add(CFG_TLS_SEC_TAG, TLS_CREDENTIAL_PRIVATE_KEY, key, sizeof(key));
}

/**
 * 全ての項目を設定して, 全ての証明書を登録する
 */
static void set_all(void)
{
    zassert_equal(cfg_set(CFG_KEY_SSID, "home-ap"), EXIT_SUCCESS);
    zassert_equal(cfg_set(CFG_KEY_PSK, "secret-pass"), EXIT_SUCCESS);
    zassert_equal(cfg_set(CFG_KEY_ENDPOINT, "example-ats.iot.ap-northeast-1.amazonaws.com"),
                  EXIT_SUCCESS);
    zassert_equal(cfg_set(CFG_KEY_CLIENT_ID, "gateway-01"), EXIT_SUCCESS);
    register_all_creds();
}

/**
 * 各テストの前に, モックと記録を, 初期状態に戻す
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
    RESET_FAKE(tls_credential_add);
    RESET_FAKE(tls_credential_get);
    RESET_FAKE(tls_credential_delete);
    FFF_RESET_HISTORY();
    settings_save_one_fake.custom_fake = fake_save_one;
    tls_credential_add_fake.custom_fake = fake_cred_add;
    tls_credential_get_fake.custom_fake = fake_cred_get;
    tls_credential_delete_fake.custom_fake = fake_cred_delete;

    (void)memset(store, 0, sizeof(store));
    (void)memset(saves, 0, sizeof(saves));
    (void)memset(&source, 0, sizeof(source));
    save_count = 0U;
    save_fail_index = -1;

    /* ble.c の static な状態 (設定の値) を, 空に戻す */
    zassert_equal(cfg_reset(), EXIT_SUCCESS);
    RESET_FAKE(settings_delete);
    RESET_FAKE(tls_credential_delete);
    tls_credential_delete_fake.custom_fake = fake_cred_delete;
}

/** 項目の名前から, 項目を探せる. ない名前は -ENOENT */
ZTEST(cfg, test_key_names)
{
    /* 名前から項目 */
    zassert_equal(cfg_key_from_name("ssid"), CFG_KEY_SSID);
    zassert_equal(cfg_key_from_name("psk"), CFG_KEY_PSK);
    zassert_equal(cfg_key_from_name("endpoint"), CFG_KEY_ENDPOINT);
    zassert_equal(cfg_key_from_name("client_id"), CFG_KEY_CLIENT_ID);
    zassert_equal(cfg_key_from_name("unknown"), -ENOENT);
    zassert_equal(cfg_key_from_name(""), -ENOENT);

    /* 項目から名前 */
    zassert_str_equal(cfg_key_name(CFG_KEY_SSID), "ssid");
    zassert_str_equal(cfg_key_name(CFG_KEY_CLIENT_ID), "client_id");
}

/** 証明書の種類の名前から, 種類を探せる. ない名前は -ENOENT */
ZTEST(cfg, test_cred_names)
{
    /* 期待: 名前から種類, 種類から名前を引ける. 項目の名前 (ssid) は, 種類ではない */
    zassert_equal(cfg_cred_from_name("ca"), CFG_CRED_CA);
    zassert_equal(cfg_cred_from_name("cert"), CFG_CRED_CERT);
    zassert_equal(cfg_cred_from_name("key"), CFG_CRED_KEY);
    zassert_equal(cfg_cred_from_name("ssid"), -ENOENT);

    zassert_str_equal(cfg_cred_name(CFG_CRED_CA), "ca");
    zassert_str_equal(cfg_cred_name(CFG_CRED_KEY), "key");
}

/** 設定した値は, 読み出せて, フラッシュに (NUL を含めて) 保存される */
ZTEST(cfg, test_set_saves_and_get)
{
    /* 期待: 取り出せて, "thermo/ssid" に, NUL を含めて, 保存される */
    zassert_equal(cfg_set(CFG_KEY_SSID, "home-ap"), EXIT_SUCCESS);

    zassert_str_equal(cfg_get(CFG_KEY_SSID), "home-ap");
    zassert_equal(save_count, 1U);
    zassert_str_equal(saves[0].name, "thermo/ssid");
    zassert_equal(saves[0].len, strlen("home-ap") + 1U);
    zassert_mem_equal(saves[0].data, "home-ap", strlen("home-ap") + 1U);
}

/** 空の値も, 設定できる (オープンネットワークの, パスワード) */
ZTEST(cfg, test_set_empty_value)
{
    /* 期待: 空の値も保存できる (保存の長さは, NUL の 1 byte) */
    zassert_equal(cfg_set(CFG_KEY_PSK, "secret"), EXIT_SUCCESS);
    zassert_equal(cfg_set(CFG_KEY_PSK, ""), EXIT_SUCCESS);

    zassert_str_equal(cfg_get(CFG_KEY_PSK), "");
    zassert_equal(saves[1].len, 1U);
}

/** 最大の長さの値は, 設定できて, 1 byte でも長ければ, -EINVAL (保存も, 更新もしない) */
ZTEST(cfg, test_set_length_limit)
{
    char value[CFG_ENDPOINT_MAX + 2U]; /* 値 */

    /* ちょうど最大の長さ: 設定できる */
    (void)memset(value, 'a', sizeof(value));
    value[CFG_ENDPOINT_MAX] = '\0';
    zassert_equal(cfg_set(CFG_KEY_ENDPOINT, value), EXIT_SUCCESS);
    zassert_equal(strlen(cfg_get(CFG_KEY_ENDPOINT)), CFG_ENDPOINT_MAX);

    /* 1 byte 長い: 拒否する (前の値のまま. 保存は, 1 回目だけ) */
    value[CFG_ENDPOINT_MAX] = 'a';
    value[CFG_ENDPOINT_MAX + 1U] = '\0';
    zassert_equal(cfg_set(CFG_KEY_ENDPOINT, value), -EINVAL);
    zassert_equal(strlen(cfg_get(CFG_KEY_ENDPOINT)), CFG_ENDPOINT_MAX);
    zassert_equal(save_count, 1U);
}

/** 保存に失敗したら, そのエラーを返して, 値は更新しない */
ZTEST(cfg, test_set_save_failure)
{
    /* 期待: 保存に失敗したら, エラーを返して, 前の値のままにする */
    zassert_equal(cfg_set(CFG_KEY_SSID, "old"), EXIT_SUCCESS);

    save_fail_index = 1;
    zassert_equal(cfg_set(CFG_KEY_SSID, "new"), -EIO);
    zassert_str_equal(cfg_get(CFG_KEY_SSID), "old");
}

/** 証明書が登録されているかを, 調べられる */
ZTEST(cfg, test_has_cred)
{
    /* 期待: 登録前は false, 登録後は, 3 つとも true (セキュリティタグは, CFG_TLS_SEC_TAG) */
    zassert_false(cfg_has_cred(CFG_CRED_CA));

    register_all_creds();
    zassert_true(cfg_has_cred(CFG_CRED_CA));
    zassert_true(cfg_has_cred(CFG_CRED_CERT));
    zassert_true(cfg_has_cred(CFG_CRED_KEY));
    zassert_equal(tls_credential_get_fake.arg0_val, CFG_TLS_SEC_TAG);
}

/** 登録した証明書は, 種類ごとに, フラッシュに保存される */
ZTEST(cfg, test_save_credentials)
{
    register_all_creds();

    /* 3 つの証明書が, CA, クライアント証明書, 秘密鍵の順に, 種類ごとの名前で保存される */
    zassert_equal(cfg_save_credentials(), EXIT_SUCCESS);
    zassert_equal(save_count, 3U);
    zassert_str_equal(saves[0].name, "thermo/ca");
    zassert_mem_equal(saves[0].data, "CA-PEM", sizeof("CA-PEM"));
    zassert_equal(saves[0].len, sizeof("CA-PEM"));
    zassert_str_equal(saves[1].name, "thermo/cert");
    zassert_mem_equal(saves[1].data, "CERT-PEM", sizeof("CERT-PEM"));
    zassert_str_equal(saves[2].name, "thermo/key");
    zassert_mem_equal(saves[2].data, "KEY-PEM", sizeof("KEY-PEM"));
}

/** 証明書が 1 つでも登録されていなければ, -ENOENT (その前までは, 保存される) */
ZTEST(cfg, test_save_credentials_missing)
{
    register_all_creds();
    store[CFG_CRED_KEY].present = false;

    /* 期待: 秘密鍵がないので, -ENOENT (CA と証明書は, その前に, 保存される) */
    zassert_equal(cfg_save_credentials(), -ENOENT);
    zassert_equal(save_count, 2U);
}

/** 証明書が大きすぎて, 保存の領域に入らなければ, -EFBIG */
ZTEST(cfg, test_save_credentials_too_large)
{
    static uint8_t large[CFG_KEY_PEM_MAX + 1U]; /* 上限を超える大きさのデータ */

    register_all_creds();
    (void)fake_cred_add(CFG_TLS_SEC_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, large, sizeof(large));

    /* 期待: 保存の領域に入らない証明書は, -EFBIG (何も保存しない) */
    zassert_equal(cfg_save_credentials(), -EFBIG);
    zassert_equal(save_count, 0U);
}

/** 保存に失敗したら, そのエラーを返して, 残りは保存しない */
ZTEST(cfg, test_save_credentials_save_failure)
{
    register_all_creds();
    save_fail_index = 1;

    /* 期待: 2 つめの保存の失敗で止まって, そのエラーを返す */
    zassert_equal(cfg_save_credentials(), -EIO);
    zassert_equal(save_count, 2U);
}

/** 設定が全て揃い, 証明書が 3 つ登録されていれば, 完全 */
ZTEST(cfg, test_is_complete)
{
    /* 期待: 何も設定していなければ false, 全て揃えれば true */
    zassert_false(cfg_is_complete());

    set_all();
    zassert_true(cfg_is_complete());
}

/** パスワードが空 (オープンネットワーク) でも, 完全 */
ZTEST(cfg, test_is_complete_without_psk)
{
    set_all();
    /* 期待: パスワードが空でも, 完全 (オープンネットワーク) */
    zassert_equal(cfg_set(CFG_KEY_PSK, ""), EXIT_SUCCESS);

    zassert_true(cfg_is_complete());
}

/** SSID, エンドポイント, クライアント ID のどれか 1 つでも空なら, 完全ではない */
ZTEST(cfg, test_is_complete_missing_value)
{
    /* SSID が空 */
    set_all();
    zassert_equal(cfg_set(CFG_KEY_SSID, ""), EXIT_SUCCESS);
    zassert_false(cfg_is_complete());

    /* エンドポイントが空 */
    set_all();
    zassert_equal(cfg_set(CFG_KEY_ENDPOINT, ""), EXIT_SUCCESS);
    zassert_false(cfg_is_complete());

    /* クライアント ID が空 */
    set_all();
    zassert_equal(cfg_set(CFG_KEY_CLIENT_ID, ""), EXIT_SUCCESS);
    zassert_false(cfg_is_complete());
}

/** 証明書が 1 つでも登録されていなければ, 完全ではない */
ZTEST(cfg, test_is_complete_missing_cred)
{
    size_t i = 0U; /* ループ用の添字 */

    for (i = 0U; i < CRED_COUNT; i++) {
        set_all();
        store[i].present = false;
        /* 期待: 証明書のどれか 1 つがなければ, false */
        zassert_false(cfg_is_complete(), "cred %zu", i);
    }
}

/** 設定と証明書を消すと, フラッシュの全ての項目を消して, 値を空にして, 証明書を削除する */
ZTEST(cfg, test_reset)
{
    set_all();
    RESET_FAKE(settings_delete);
    RESET_FAKE(tls_credential_delete);
    tls_credential_delete_fake.custom_fake = fake_cred_delete;

    zassert_equal(cfg_reset(), EXIT_SUCCESS);

    zassert_equal(settings_delete_fake.call_count, 7U); /* 項目 4 つと, 証明書 3 つ */
    zassert_equal(tls_credential_delete_fake.call_count, 3U);
    zassert_str_equal(cfg_get(CFG_KEY_SSID), "");
    zassert_str_equal(cfg_get(CFG_KEY_CLIENT_ID), "");
    zassert_false(cfg_has_cred(CFG_CRED_CA));
    zassert_false(cfg_is_complete());
}

/** 削除に失敗しても, 残りを全て消して, 最初のエラーを返す (項目, 証明書の, どちらも) */
ZTEST(cfg, test_reset_failure_returns_first_error)
{
    int results[] = {0, -EIO, -ENOMEM, 0, -EBUSY, 0, -EAGAIN}; /* モックが順に返す戻り値 */

    set_all();
    RESET_FAKE(settings_delete);
    SET_RETURN_SEQ(settings_delete, results, ARRAY_SIZE(results));

    zassert_equal(cfg_reset(), -EIO);
    zassert_equal(settings_delete_fake.call_count, 7U);
    zassert_str_equal(cfg_get(CFG_KEY_PSK), ""); /* 失敗しても, 値は空にする */
}

/** 項目は成功して, 証明書の削除だけが失敗した場合も, そのエラーを返す */
ZTEST(cfg, test_reset_failure_in_credentials)
{
    int results[] = {0, 0, 0, 0, 0, -ENOMEM, -EIO}; /* モックが順に返す戻り値 */

    RESET_FAKE(settings_delete);
    SET_RETURN_SEQ(settings_delete, results, ARRAY_SIZE(results));

    /* 期待: 項目は成功して, 証明書の削除の失敗 (最初のエラー) を返す */
    zassert_equal(cfg_reset(), -ENOMEM);
}

/** 起動時は, settings を初期化して, ハンドラを登録して, "thermo" 以下を読み込む */
ZTEST(cfg, test_init)
{
    /* 期待: settings を初期化して, ハンドラを登録して, "thermo" 以下を読み込む */
    zassert_equal(cfg_init(), EXIT_SUCCESS);

    zassert_equal(settings_subsys_init_fake.call_count, 1U);
    zassert_equal(settings_register_fake.call_count, 1U);
    zassert_str_equal(settings_register_fake.arg0_val->name, "thermo");
    zassert_equal(settings_load_subtree_fake.call_count, 1U);
    zassert_str_equal(settings_load_subtree_fake.arg0_val, "thermo");
}

/** settings の初期化に失敗したら, そのエラーを返す (ハンドラは, 登録しない) */
ZTEST(cfg, test_init_subsys_failure)
{
    settings_subsys_init_fake.return_val = -EIO;

    /* 期待: settings の初期化に失敗したら, ハンドラは登録しない */
    zassert_equal(cfg_init(), -EIO);
    zassert_equal(settings_register_fake.call_count, 0U);
}

/** ハンドラの登録に失敗したら, そのエラーを返す (読み込まない) */
ZTEST(cfg, test_init_register_failure)
{
    settings_register_fake.return_val = -ENOMEM;

    /* 期待: ハンドラの登録に失敗したら, 読み込まない */
    zassert_equal(cfg_init(), -ENOMEM);
    zassert_equal(settings_load_subtree_fake.call_count, 0U);
}

/** 読み込みに失敗したら, そのエラーを返す */
ZTEST(cfg, test_init_load_failure)
{
    settings_load_subtree_fake.return_val = -EIO;

    /* 期待: 読み込みに失敗したら, そのエラーを返す */
    zassert_equal(cfg_init(), -EIO);
}

/** フラッシュに保存された項目は, 読み込まれて, 取り出せる */
ZTEST(cfg, test_load_value)
{
    static const char ssid[] = "saved-ap"; /* SSID (テスト用) */

    load("ssid", ssid, sizeof(ssid));

    /* 期待: 保存されていた値が, 取り出せる */
    zassert_str_equal(cfg_get(CFG_KEY_SSID), "saved-ap");
}

/** 長さが 0 の項目は, 無視する (値は, そのまま) */
ZTEST(cfg, test_load_value_empty_ignored)
{
    /* 期待: 長さ 0 の項目は無視して, 設定済みの値のまま */
    zassert_equal(cfg_set(CFG_KEY_SSID, "keep"), EXIT_SUCCESS);

    load("ssid", "x", 0U);

    zassert_str_equal(cfg_get(CFG_KEY_SSID), "keep");
}

/** 長さが, 最大 (NUL を含めて, 最大 + 1) を超える項目は, 無視する */
ZTEST(cfg, test_load_value_too_long_ignored)
{
    static char big[CFG_SSID_MAX + 2U]; /* 上限を超える長さの文字列 */

    (void)memset(big, 'a', sizeof(big));
    /* 期待: 最大を超える長さの項目は無視して, 設定済みの値のまま */
    zassert_equal(cfg_set(CFG_KEY_SSID, "keep"), EXIT_SUCCESS);

    load("ssid", big, sizeof(big));

    zassert_str_equal(cfg_get(CFG_KEY_SSID), "keep");
}

/** 読み出しに失敗した項目は, 空にする */
ZTEST(cfg, test_load_value_read_failure)
{
    struct settings_handler *h = handler_of(); /* settings のハンドラ */

    /* 期待: 読み出しに失敗した項目は, 空にする (ハンドラは 0 を返して, 読み込みを続ける) */
    zassert_equal(cfg_set(CFG_KEY_SSID, "keep"), EXIT_SUCCESS);
    source.err = -EIO;

    zassert_equal(h->h_set("ssid", 5U, read_source, NULL), 0);
    zassert_str_equal(cfg_get(CFG_KEY_SSID), "");
}

/** 知らない名前の項目は, 無視する (読み込みを止めないため, 0 を返す) */
ZTEST(cfg, test_load_unknown_ignored)
{
    load("unknown", "x", 2U);

    /* 期待: 知らない名前は無視する (証明書も, 項目も, 変わらない) */
    zassert_equal(tls_credential_add_fake.call_count, 0U);
    zassert_str_equal(cfg_get(CFG_KEY_SSID), "");
}

/** フラッシュに保存された証明書は, TLS の認証情報に登録される (既存の登録は, 置き換える) */
ZTEST(cfg, test_load_credential)
{
    static const char ca[] = "SAVED-CA"; /* CA 証明書 (テスト用) */
    const void *registered = NULL;       /* 登録されたハンドラ */

    load("ca", ca, sizeof(ca));

    /* 先に, 同じ種類の登録を消してから, 追加する */
    zassert_equal(tls_credential_delete_fake.call_count, 1U);
    zassert_equal(tls_credential_add_fake.call_count, 1U);
    zassert_equal(tls_credential_add_fake.arg0_val, CFG_TLS_SEC_TAG);
    zassert_equal(tls_credential_add_fake.arg1_val, TLS_CREDENTIAL_CA_CERTIFICATE);
    zassert_equal(tls_credential_add_fake.arg3_val, sizeof(ca));
    /* 登録されたデータは, 保存されていた内容と同じ (ポインタは, cfg.c の領域) */
    registered = tls_credential_add_fake.arg2_val;
    zassert_mem_equal(registered, ca, sizeof(ca));
    zassert_true(cfg_has_cred(CFG_CRED_CA));
}

/** 3 種類の証明書は, それぞれの種類で登録される */
ZTEST(cfg, test_load_credential_types)
{
    load("cert", "C", 2U);
    /* 期待: "cert" はクライアント証明書, "key" は秘密鍵の種類で, 登録する */
    zassert_equal(tls_credential_add_fake.arg1_val, TLS_CREDENTIAL_PUBLIC_CERTIFICATE);

    load("key", "K", 2U);
    zassert_equal(tls_credential_add_fake.arg1_val, TLS_CREDENTIAL_PRIVATE_KEY);
}

/** 長さが 0 の証明書, 領域に入らない証明書は, 無視する */
ZTEST(cfg, test_load_credential_invalid_length_ignored)
{
    static uint8_t big[CFG_KEY_PEM_MAX + 1U]; /* 上限を超える長さの文字列 */

    load("ca", "x", 0U);
    load("ca", big, CFG_CERT_MAX + 1U);
    load("key", big, CFG_KEY_PEM_MAX + 1U);

    /* 期待: 長さ 0 と, 領域に入らない証明書は, 登録しない */
    zassert_equal(tls_credential_add_fake.call_count, 0U);
}

/** 証明書の読み出しに失敗したら, 登録しない */
ZTEST(cfg, test_load_credential_read_failure)
{
    struct settings_handler *h = handler_of(); /* settings のハンドラ */

    source.err = -EIO;

    /* 期待: 読み出しに失敗した証明書は, 登録しない */
    zassert_equal(h->h_set("ca", 4U, read_source, NULL), 0);
    zassert_equal(tls_credential_add_fake.call_count, 0U);
}

/** 証明書の登録に失敗しても, 読み込みは続ける (0 を返す) */
ZTEST(cfg, test_load_credential_add_failure)
{
    tls_credential_add_fake.custom_fake = NULL;
    tls_credential_add_fake.return_val = -ENOMEM;

    load("ca", "x", 2U);

    /* 期待: 登録に失敗しても, 読み込みは続けて, 登録されていない状態のまま */
    zassert_equal(tls_credential_add_fake.call_count, 1U);
    zassert_false(cfg_has_cred(CFG_CRED_CA));
}

ZTEST_SUITE(cfg, NULL, NULL, before, NULL, NULL);
