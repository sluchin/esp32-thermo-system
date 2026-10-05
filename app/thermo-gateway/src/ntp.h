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
 * NTP サーバに問い合わせて、Zephyr のシステム時計 (SYS_CLOCK_REALTIME) を合わせる。
 * WiFi に接続したあとで呼ぶ。
 */

#include <zephyr/kernel.h>
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int64_t */

/**
 * @brief NTP サーバに問い合わせて、システム時計を合わせる。
 *
 * すでに同期していて、前回の同期から CONFIG_THERMO_NTP_RESYNC_S 秒たっていなければ、
 * 問い合わせずに、すぐ戻る。
 *
 * @param[in] server  NTP サーバのホスト名
 * @param[in] timeout 応答を待つ時間
 * @retval EXIT_SUCCESS    同期した (または、同期済み)
 * @retval -EHOSTUNREACH   サーバの名前を解決できなかった
 * @retval -ETIMEDOUT      時間内に、応答がなかった
 * @retval negative        問い合わせ、または、時計の設定の失敗 (負の errno)
 */
int ntp_sync(const char *server, k_timeout_t timeout);

/**
 * @brief 時刻を同期したことがあるか調べる。
 *
 * @return 1 回でも同期していれば true
 */
bool ntp_is_synced(void);

/**
 * @brief 現在の UNIX 時刻を返す。
 *
 * @param[out] sec UNIX 時刻 [s] (同期していなければ、変更しない)
 * @retval EXIT_SUCCESS 成功
 * @retval -EAGAIN      まだ、同期していない
 */
int ntp_unix_time(int64_t *sec);

#endif /* THERMO_GATEWAY_NTP_H */
