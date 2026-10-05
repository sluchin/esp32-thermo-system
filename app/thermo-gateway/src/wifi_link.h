/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_GATEWAY_WIFI_LINK_H
#define THERMO_GATEWAY_WIFI_LINK_H

/**
 * @file
 * @brief WiFi (ステーションモード) の接続
 *
 * Zephyr の net_mgmt で、アクセスポイントに接続して、DHCP で IPv4 アドレスをもらうまで待つ。
 */

#include <zephyr/kernel.h>
#include <stdbool.h> /* bool */

/**
 * @brief WiFi のイベント (接続の結果、切断、IPv4 アドレスの取得) のコールバックを登録する。
 *
 * @retval EXIT_SUCCESS 成功
 */
int wifi_link_init(void);

/**
 * @brief アクセスポイントに接続して、IPv4 アドレスを取得するまで待つ。
 *
 * すでに接続していれば、すぐ戻る。
 *
 * @param[in] ssid    SSID
 * @param[in] psk     パスワード (空の文字列なら、暗号化なしで接続する)
 * @param[in] timeout 待つ時間
 * @retval EXIT_SUCCESS    接続して、IPv4 アドレスを取得した
 * @retval -ENODEV         ネットワークインターフェースがない
 * @retval -ETIMEDOUT      時間内に、完了しなかった
 * @retval -ECONNREFUSED   接続に失敗した (SSID やパスワードの誤りなど)
 * @retval negative        接続の要求に失敗した (負の errno)
 */
int wifi_link_connect(const char *ssid, const char *psk, k_timeout_t timeout);

/**
 * @brief IPv4 アドレスを取得した状態か調べる。
 *
 * @return 接続していて、IPv4 アドレスがあれば true
 */
bool wifi_link_is_up(void);

/**
 * @brief アクセスポイントから切断する (結果は、待たない)。
 */
void wifi_link_disconnect(void);

#endif /* THERMO_GATEWAY_WIFI_LINK_H */
