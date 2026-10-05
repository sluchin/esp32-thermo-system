/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief 実行時の設定 (WiFi、AWS IoT Core のエンドポイント、証明書) の保存と読み出し
 */

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <errno.h>  /* ENOENT EINVAL */
#include <stdint.h> /* uint8_t */
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* strcmp strlen memcpy */

#include "cfg.h"

LOG_MODULE_REGISTER(thermo_cfg);

/** settings の、このアプリの項目のルート ("thermo/ssid" のように使う) */
#define SETTINGS_ROOT      "thermo"
/** settings の項目の名前 ("thermo/client_id" など) の最大長 (NUL を含む) */
#define SETTINGS_NAME_SIZE 32u

/** 設定の項目ごとの、名前と、値の最大長 */
static const struct {
    const char *name; /**< 項目の名前 (settings の名前と、シェルの引数に使う) */
    size_t max;       /**< 値の最大長 (NUL を除く) */
} key_infos[CFG_KEY_COUNT] = {
        [CFG_KEY_SSID] = {"ssid", CFG_SSID_MAX},
        [CFG_KEY_PSK] = {"psk", CFG_PSK_MAX},
        [CFG_KEY_ENDPOINT] = {"endpoint", CFG_ENDPOINT_MAX},
        [CFG_KEY_CLIENT_ID] = {"client_id", CFG_CLIENT_ID_MAX},
};

/**
 * フラッシュから読み込んだ、CA 証明書の保存先
 *
 * tls_credential_add() は、データをコピーせず、ポインタを保持するので、登録している間は、
 * 領域を残しておく必要がある.
 */
static uint8_t ca_buf[CFG_CERT_MAX];
/** フラッシュから読み込んだ、クライアント証明書の保存先 */
static uint8_t cert_buf[CFG_CERT_MAX];
/** フラッシュから読み込んだ、秘密鍵の保存先 */
static uint8_t key_buf[CFG_KEY_PEM_MAX];

/** 証明書の種類ごとの、名前と、TLS の認証情報の種類と、保存先 */
static const struct {
    const char *name;              /**< 名前 (settings の名前と、シェルの引数に使う) */
    enum tls_credential_type type; /**< TLS の認証情報の種類 */
    uint8_t *buf;                  /**< フラッシュから読み込んだ証明書の保存先 */
    size_t max;                    /**< 証明書の最大サイズ */
} cred_infos[CFG_CRED_COUNT] = {
        [CFG_CRED_CA] = {"ca", TLS_CREDENTIAL_CA_CERTIFICATE, ca_buf, sizeof(ca_buf)},
        [CFG_CRED_CERT] = {"cert", TLS_CREDENTIAL_PUBLIC_CERTIFICATE, cert_buf, sizeof(cert_buf)},
        [CFG_CRED_KEY] = {"key", TLS_CREDENTIAL_PRIVATE_KEY, key_buf, sizeof(key_buf)},
};

/** 設定の値 (NUL で終わる) */
static char values[CFG_KEY_COUNT][CFG_ENDPOINT_MAX + 1u];

/** 証明書をフラッシュに保存するときの、一時的な領域 (最も大きい証明書が入る大きさ) */
static uint8_t save_buf[CFG_KEY_PEM_MAX];

/**
 * settings の項目の名前 ("thermo/ssid" など) を作る
 *
 * @param[out] out  出力先 (SETTINGS_NAME_SIZE)
 * @param[in]  name 項目の名前 ("ssid" など)
 */
static void settings_name(char *out, const char *name)
{
    (void)snprintf(out, SETTINGS_NAME_SIZE, SETTINGS_ROOT "/%s", name);
}

/* 項目の名前から、項目を探す */
int cfg_key_from_name(const char *name)
{
    size_t i = 0u; /* ループ用の添字 */

    for (i = 0u; i < ARRAY_SIZE(key_infos); i++) {
        if (strcmp(name, key_infos[i].name) == 0) {
            return (int)i;
        }
    }
    return -ENOENT;
}

/* 項目の名前を返す */
const char *cfg_key_name(enum cfg_key key)
{
    return key_infos[key].name;
}

/* 項目の値を返す */
const char *cfg_get(enum cfg_key key)
{
    return values[key];
}

/* 項目の値を、フラッシュに保存して、設定する */
int cfg_set(enum cfg_key key, const char *value)
{
    char name[SETTINGS_NAME_SIZE] = {0}; /* settings の名前 */
    size_t len = strlen(value);          /* 値の長さ [バイト] */
    int err = EXIT_SUCCESS;              /* エラーコード */

    if (len > key_infos[key].max) {
        return -EINVAL;
    }

    /* フラッシュに保存できたときだけ、RAM の値を更新する (NUL も保存する) */
    settings_name(name, key_infos[key].name);
    err = settings_save_one(name, value, len + 1u);
    if (err != 0) {
        return err;
    }

    (void)memcpy(values[key], value, len + 1u);
    return EXIT_SUCCESS;
}

/* 証明書の種類の名前から、種類を探す */
int cfg_cred_from_name(const char *name)
{
    size_t i = 0u; /* ループ用の添字 */

    for (i = 0u; i < ARRAY_SIZE(cred_infos); i++) {
        if (strcmp(name, cred_infos[i].name) == 0) {
            return (int)i;
        }
    }
    return -ENOENT;
}

/* 証明書の種類の名前を返す */
const char *cfg_cred_name(enum cfg_cred cred)
{
    return cred_infos[cred].name;
}

/* 証明書が、TLS の認証情報に登録されているか調べる */
bool cfg_has_cred(enum cfg_cred cred)
{
    uint8_t probe = 0u;         /* 存在確認用の 1 バイト */
    size_t len = sizeof(probe); /* 値の長さ [バイト] */

    /* 登録されていれば、1 byte の領域には入らないので、-EFBIG になる */
    return tls_credential_get(CFG_TLS_SEC_TAG, cred_infos[cred].type, &probe, &len) != -ENOENT;
}

/* 登録されている証明書を、フラッシュに保存する */
int cfg_save_credentials(void)
{
    char name[SETTINGS_NAME_SIZE] = {0}; /* settings の名前 */
    size_t len = 0u;                     /* 値の長さ [バイト] */
    size_t i = 0u;                       /* ループ用の添字 */
    int err = EXIT_SUCCESS;              /* エラーコード */

    for (i = 0u; i < ARRAY_SIZE(cred_infos); i++) {
        /* 登録されている証明書を、一時的な領域に取り出して、フラッシュに保存する */
        len = sizeof(save_buf);
        err = tls_credential_get(CFG_TLS_SEC_TAG, cred_infos[i].type, save_buf, &len);
        if (err != 0) {
            LOG_ERR("Credential '%s' is not available (err %d)", cred_infos[i].name, err);
            return err;
        }

        settings_name(name, cred_infos[i].name);
        err = settings_save_one(name, save_buf, len);
        if (err != 0) {
            LOG_ERR("Saving credential '%s' failed (err %d)", cred_infos[i].name, err);
            return err;
        }
    }
    return EXIT_SUCCESS;
}

/* 接続に必要な設定と証明書が、全て揃っているか調べる */
bool cfg_is_complete(void)
{
    size_t i = 0u; /* ループ用の添字 */

    /* PSK は、オープンネットワークでは、空 */
    if ((values[CFG_KEY_SSID][0] == '\0') || (values[CFG_KEY_ENDPOINT][0] == '\0') ||
        (values[CFG_KEY_CLIENT_ID][0] == '\0')) {
        return false;
    }

    for (i = 0u; i < ARRAY_SIZE(cred_infos); i++) {
        if (!cfg_has_cred((enum cfg_cred)i)) {
            return false;
        }
    }
    return true;
}

/* 設定と証明書を、フラッシュからも消す */
int cfg_reset(void)
{
    char name[SETTINGS_NAME_SIZE] = {0}; /* settings の名前 */
    size_t i = 0u;                       /* ループ用の添字 */
    int first_err = EXIT_SUCCESS;        /* 最初のエラー */
    int err = EXIT_SUCCESS;              /* エラーコード */

    /* 失敗しても、残りの項目の削除を続けて、最初のエラーを返す */
    for (i = 0u; i < ARRAY_SIZE(key_infos); i++) {
        settings_name(name, key_infos[i].name);
        err = settings_delete(name);
        if ((err != 0) && (first_err == EXIT_SUCCESS)) {
            first_err = err;
        }
        values[i][0] = '\0';
    }

    for (i = 0u; i < ARRAY_SIZE(cred_infos); i++) {
        settings_name(name, cred_infos[i].name);
        err = settings_delete(name);
        if ((err != 0) && (first_err == EXIT_SUCCESS)) {
            first_err = err;
        }
        /* 登録されていなくても (-ENOENT)、かまわない */
        (void)tls_credential_delete(CFG_TLS_SEC_TAG, cred_infos[i].type);
    }
    return first_err;
}

/**
 * settings から読み込んだ設定の値を、登録する
 *
 * @param[in] key     項目
 * @param[in] len     保存されている値の長さ (NUL を含む)
 * @param[in] read_cb 値を読むコールバック
 * @param[in] cb_arg  read_cb に渡す引数
 */
static void load_value(enum cfg_key key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    ssize_t read_len = 0; /* 読み込んだ長さ [バイト] */

    if ((len == 0u) || (len > (key_infos[key].max + 1u))) {
        LOG_WRN("Ignoring '%s' with an invalid length %zu", key_infos[key].name, len);
        return;
    }

    read_len = read_cb(cb_arg, values[key], len);
    if (read_len < 0) {
        LOG_WRN("Reading '%s' failed (err %d)", key_infos[key].name, (int)read_len);
        values[key][0] = '\0';
        return;
    }
    values[key][len - 1u] = '\0'; /* 保存した値には NUL が含まれるが、念のため */
}

/**
 * settings から読み込んだ証明書を、TLS の認証情報に登録する
 *
 * @param[in] cred    種類
 * @param[in] len     保存されている証明書の長さ
 * @param[in] read_cb 証明書を読むコールバック
 * @param[in] cb_arg  read_cb に渡す引数
 */
static void load_credential(enum cfg_cred cred, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    ssize_t read_len = 0;   /* 読み込んだ長さ [バイト] */
    int err = EXIT_SUCCESS; /* エラーコード */

    if ((len == 0u) || (len > cred_infos[cred].max)) {
        LOG_WRN("Ignoring credential '%s' with an invalid length %zu", cred_infos[cred].name, len);
        return;
    }

    read_len = read_cb(cb_arg, cred_infos[cred].buf, len);
    if (read_len < 0) {
        LOG_WRN("Reading credential '%s' failed (err %d)", cred_infos[cred].name, (int)read_len);
        return;
    }

    /* 同じ種類が登録されていたら、置き換える (登録されていなくても、かまわない) */
    (void)tls_credential_delete(CFG_TLS_SEC_TAG, cred_infos[cred].type);
    err = tls_credential_add(CFG_TLS_SEC_TAG, cred_infos[cred].type, cred_infos[cred].buf, len);
    if (err != 0) {
        LOG_WRN("Adding credential '%s' failed (err %d)", cred_infos[cred].name, err);
    }
}

/**
 * settings が、保存された項目を読み込むときに呼ぶコールバック
 *
 * 読み込めない項目があっても、ほかの項目の読み込みを続けるため、常に 0 を返す.
 *
 * @param[in] name   項目の名前 (SETTINGS_ROOT の下の名前。"ssid" など)
 * @param[in] len    保存されている値の長さ
 * @param[in] read_cb 値を読むコールバック
 * @param[in] cb_arg  read_cb に渡す引数
 * @return 0
 */
static int settings_set_cb(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    int key = cfg_key_from_name(name);   /* 設定項目の番号 */
    int cred = cfg_cred_from_name(name); /* 証明書の番号 */

    if (key >= 0) {
        load_value((enum cfg_key)key, len, read_cb, cb_arg);
    } else if (cred >= 0) {
        load_credential((enum cfg_cred)cred, len, read_cb, cb_arg);
    } else {
        LOG_WRN("Ignoring the unknown setting '%s'", name);
    }
    return 0;
}

/** settings の、このアプリの項目のハンドラ */
static struct settings_handler handler = {.name = SETTINGS_ROOT, .h_set = settings_set_cb};

/* settings を初期化して、保存された設定と証明書を読み込む */
int cfg_init(void)
{
    int err = settings_subsys_init(); /* エラーコード */

    if (err != 0) {
        LOG_ERR("Settings init failed (err %d)", err);
        return err;
    }

    err = settings_register(&handler);
    if (err != 0) {
        LOG_ERR("Registering the settings handler failed (err %d)", err);
        return err;
    }

    err = settings_load_subtree(SETTINGS_ROOT);
    if (err != 0) {
        LOG_ERR("Loading the settings failed (err %d)", err);
        return err;
    }
    return EXIT_SUCCESS;
}
