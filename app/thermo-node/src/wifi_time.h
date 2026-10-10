/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_WIFI_TIME_H
#define THERMO_NODE_WIFI_TIME_H

/**
 * @file
 * @brief WiFi と SNTP による時刻の取得
 *
 * BLE でゲートウェイから時刻が届かないときの予備. WiFi に接続して, SNTP で UTC の UNIX 時刻を
 * 取り, すぐに WiFi を切る (常時は, つながない). 時刻の RTC への設定は, 呼び出し側が行う.
 */

#include <stdint.h> /* int64_t */

/** WiFi の接続を待つ時間 [s] */
#define WIFI_TIME_CONNECT_S 20

/** SNTP の応答を待つ時間 [s] */
#define WIFI_TIME_SNTP_S 5

int wifi_time_init(void);

int wifi_time_fetch(int64_t *unix_s);

#endif /* THERMO_NODE_WIFI_TIME_H */
