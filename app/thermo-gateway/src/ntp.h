/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_GATEWAY_NTP_H
#define THERMO_GATEWAY_NTP_H

/**
 * @file
 * @brief SNTP による時刻の同期
 *
 * NTP サーバに問い合わせて, Zephyr のシステム時計 (SYS_CLOCK_REALTIME) を合わせる.
 * WiFi に接続したあとで呼ぶ.
 */

#include <zephyr/kernel.h>
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int64_t */

int ntp_sync(const char *server, k_timeout_t timeout);

bool ntp_is_synced(void);

int ntp_unix_time(int64_t *sec);

#endif /* THERMO_GATEWAY_NTP_H */
