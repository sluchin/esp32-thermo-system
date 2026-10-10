/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief 時刻の取得に使う WiFi の設定 (SSID, パスワード) の保存と読み出し
 */

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <errno.h>  /* ENOENT EINVAL */
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* strcmp strlen memcpy */

#include "thermo_log.h"
#include "wifi_cfg.h"

LOG_MODULE_REGISTER(thermo_wifi_cfg, THERMO_LOG_LEVEL);

/** settings のこのアプリの項目のルート ("thermo/ssid" のように使う) */
#define SETTINGS_ROOT      "thermo"
/** settings の項目の名前 ("thermo/ssid" など) の最大長 (NUL を含む) */
#define SETTINGS_NAME_SIZE 32U

/** 設定の項目ごとの名前と, 値の最大長 */
static const struct {
    const char *name; /**< 項目の名前 (settings の名前と, シェルの引数に使う) */
    size_t max;       /**< 値の最大長 (NUL を除く) */
} key_infos[WIFI_CFG_COUNT] = {
    [WIFI_CFG_SSID] = {"ssid", WIFI_CFG_SSID_MAX},
    [WIFI_CFG_PSK] = {"psk", WIFI_CFG_PSK_MAX},
};

/** 設定の値 (NUL で終わる) */
static char values[WIFI_CFG_COUNT][WIFI_CFG_PSK_MAX + 1U];

static void settings_name(char *out, const char *name);
static void load_value(enum wifi_cfg_key key, size_t len, settings_read_cb read_cb, void *cb_arg);
static int settings_set_cb(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg);

/**
 * @brief 項目の名前 ("ssid" など) から, 項目を探す
 *
 * @param[in] name 項目の名前
 * @return 項目 (enum wifi_cfg_key). ない場合は -ENOENT
 */
int wifi_cfg_key_from_name(const char *name)
{
    size_t i = 0U; /* ループ用の添字 */

    for (i = 0U; i < ARRAY_SIZE(key_infos); i++) {
        if (strcmp(name, key_infos[i].name) == 0) {
            return (int)i;
        }
    }

    return -ENOENT;
}

/**
 * @brief 項目の名前を返す
 *
 * @param[in] key 項目
 * @return 項目の名前 (NUL で終わる)
 */
const char *wifi_cfg_key_name(enum wifi_cfg_key key)
{
    return key_infos[key].name;
}

/**
 * @brief 項目の値を返す
 *
 * @param[in] key 項目
 * @return 値 (NUL で終わる. 未設定なら空の文字列)
 */
const char *wifi_cfg_get(enum wifi_cfg_key key)
{
    return values[key];
}

/**
 * @brief 項目の値を設定して, フラッシュに保存する
 *
 * @param[in] key   項目
 * @param[in] value 値 (空でもよい)
 * @retval EXIT_SUCCESS 成功
 * @retval -EINVAL      値が長すぎる
 * @retval negative     保存に失敗 (負の errno)
 */
int wifi_cfg_set(enum wifi_cfg_key key, const char *value)
{
    char name[SETTINGS_NAME_SIZE] = {0}; /* settings の名前 */
    size_t len = strlen(value);          /* 値の長さ [バイト] */
    int err = EXIT_SUCCESS;              /* エラーコード */

    if (len > key_infos[key].max) {
        return -EINVAL;
    }

    /* フラッシュに保存できたときだけ, RAM の値を更新する (NUL も保存する) */
    settings_name(name, key_infos[key].name);
    err = settings_save_one(name, value, len + 1U);
    if (err != 0) {
        return err;
    }

    (void)memcpy(values[key], value, len + 1U);

    return EXIT_SUCCESS;
}

/**
 * @brief 接続に必要な設定が揃っているか調べる
 *
 * SSID が空でないこと (パスワードは, オープンネットワークでは空).
 *
 * @return 揃っていれば true
 */
bool wifi_cfg_is_complete(void)
{
    return values[WIFI_CFG_SSID][0] != '\0';
}

/**
 * @brief 設定を全て消す (フラッシュからも消す)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno. 最初のエラーを返すが全ての項目の削除を試みる)
 */
int wifi_cfg_reset(void)
{
    char name[SETTINGS_NAME_SIZE] = {0}; /* settings の名前 */
    size_t i = 0U;                       /* ループ用の添字 */
    int first_err = EXIT_SUCCESS;        /* 最初のエラー */
    int err = EXIT_SUCCESS;              /* エラーコード */

    /* 失敗しても残りの項目の削除を続けて, 最初のエラーを返す */
    for (i = 0U; i < ARRAY_SIZE(key_infos); i++) {
        settings_name(name, key_infos[i].name);
        err = settings_delete(name);
        if ((err != 0) && (first_err == EXIT_SUCCESS)) {
            first_err = err;
        }
        values[i][0] = '\0';
    }

    return first_err;
}

/** settings のこのアプリの項目のハンドラ */
static struct settings_handler handler = {.name = SETTINGS_ROOT, .h_set = settings_set_cb};

/**
 * @brief 設定をフラッシュから読み込む
 *
 * @retval EXIT_SUCCESS 成功 (保存された設定がなくても成功)
 * @retval negative     失敗 (負の errno)
 */
int wifi_cfg_init(void)
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

/**
 * settings から読み込んだ設定の値を, 登録する
 *
 * @param[in] key     項目
 * @param[in] len     保存されている値の長さ (NUL を含む)
 * @param[in] read_cb 値を読むコールバック
 * @param[in] cb_arg  read_cb に渡す引数
 */
static void load_value(enum wifi_cfg_key key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    ssize_t read_len = 0; /* 読み込んだ長さ [バイト] */

    if ((len == 0U) || (len > (key_infos[key].max + 1U))) {
        LOG_WRN("Ignoring '%s' with an invalid length %zu", key_infos[key].name, len);
        return;
    }

    read_len = read_cb(cb_arg, values[key], len);
    if (read_len < 0) {
        LOG_WRN("Reading '%s' failed (err %d)", key_infos[key].name, (int)read_len);
        values[key][0] = '\0';
        return;
    }
    values[key][len - 1U] = '\0'; /* 保存した値には NUL が含まれるが, 念のため */
}

/**
 * settings が保存された項目を読み込むときに呼ぶコールバック
 *
 * 読み込めない項目があってもほかの項目の読み込みを続けるため, 常に 0 を返す.
 *
 * @param[in] name   項目の名前 (SETTINGS_ROOT の下の名前. "ssid" など)
 * @param[in] len    保存されている値の長さ
 * @param[in] read_cb 値を読むコールバック
 * @param[in] cb_arg  read_cb に渡す引数
 * @return 0
 */
static int settings_set_cb(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    int key = wifi_cfg_key_from_name(name); /* 設定項目の番号 */

    if (key >= 0) {
        load_value((enum wifi_cfg_key)key, len, read_cb, cb_arg);
    } else {
        LOG_WRN("Ignoring the unknown setting '%s'", name);
    }

    return 0;
}
