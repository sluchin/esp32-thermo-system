/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_WIFI_CFG_H
#define THERMO_NODE_WIFI_CFG_H

/**
 * @file
 * @brief 時刻の取得に使う WiFi の設定 (SSID, パスワード) の保存と読み出し
 *
 * 設定は Zephyr のシェル (`thermo` コマンド. wifi_shell.c) で入力して, フラッシュ (settings の
 * NVS) に保存する.
 */

#include <stdbool.h> /* bool */

/** SSID の最大長 [byte] (NUL を除く) */
#define WIFI_CFG_SSID_MAX 32U

/** WiFi のパスワードの最大長 [byte] (NUL を除く) */
#define WIFI_CFG_PSK_MAX 64U

/** 設定の項目 */
enum wifi_cfg_key {
    WIFI_CFG_SSID,  /**< WiFi の SSID */
    WIFI_CFG_PSK,   /**< WiFi のパスワード (空なら, オープンネットワーク) */
    WIFI_CFG_COUNT, /**< 項目の数 */
};

int wifi_cfg_init(void);

int wifi_cfg_key_from_name(const char *name);

const char *wifi_cfg_key_name(enum wifi_cfg_key key);

const char *wifi_cfg_get(enum wifi_cfg_key key);

int wifi_cfg_set(enum wifi_cfg_key key, const char *value);

bool wifi_cfg_is_complete(void);

int wifi_cfg_reset(void);

#endif /* THERMO_NODE_WIFI_CFG_H */
