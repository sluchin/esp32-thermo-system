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
 * Zephyr の net_mgmt で, アクセスポイントに接続して, DHCP で IPv4 アドレスをもらうまで待つ.
 */

#include <zephyr/kernel.h>
#include <stdbool.h> /* bool */

int wifi_link_init(void);

int wifi_link_connect(const char *ssid, const char *psk, k_timeout_t timeout);

bool wifi_link_is_up(void);

void wifi_link_disconnect(void);

#endif /* THERMO_GATEWAY_WIFI_LINK_H */
