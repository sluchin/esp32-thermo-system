/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_COMMON_THERMO_RTC_H
#define THERMO_COMMON_THERMO_RTC_H

/**
 * @file
 * @brief RTC のドライバ (drivers/rtc_pcf8563.c) と node_time.c が共有する戻り値
 */

/**
 * rtc_get_time() の戻り値 (正の値): 時刻は読めて, 範囲にも入っているが, 電圧低下 (VL) の
 * ビットが立っているので, 信用できない (時刻は timeptr に入っている)
 */
#define THERMO_RTC_UNVERIFIED 1

#endif /* THERMO_COMMON_THERMO_RTC_H */
