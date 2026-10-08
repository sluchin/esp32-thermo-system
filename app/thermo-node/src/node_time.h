/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_NODE_TIME_H
#define THERMO_NODE_NODE_TIME_H

/**
 * @file
 * @brief thermo-node の時刻 (拡張ボードの RTC: PCF8563)
 *
 * RTC は UTC を保持する. 読み出すときは, ローカルタイム (UTC に CONFIG_THERMO_UTC_OFFSET_MIN を
 * 足した時刻) に直す. RTC は, Devicetree の alias thermo-rtc で選ぶ. 使わないときは,
 * CONFIG_THERMO_RTC=n でビルドする (node_time.c は, ビルドされない. このヘッダの構造体は, 使える).
 */

#include <stdbool.h> /* bool */
#include <stdint.h>  /* int64_t uint8_t uint16_t */

/** 日時 (ローカルタイム) */
struct node_datetime {
    bool valid;     /**< 時刻が設定済みで, 正しく読めたか (false のときは, 下の値を使わない) */
    uint16_t year;  /**< 年 (2000 から 2100) */
    uint8_t month;  /**< 月 (1 から 12) */
    uint8_t day;    /**< 日 (1 から 31) */
    uint8_t hour;   /**< 時 (0 から 23) */
    uint8_t minute; /**< 分 (0 から 59) */
    uint8_t second; /**< 秒 (0 から 59) */
};

int node_time_init(void);

int node_time_get(struct node_datetime *local);

int node_time_set(int64_t unix_s);

#endif /* THERMO_NODE_NODE_TIME_H */
