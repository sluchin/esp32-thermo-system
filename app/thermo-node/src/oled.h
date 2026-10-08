/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_OLED_H
#define THERMO_NODE_OLED_H

/**
 * @file
 * @brief thermo-node の OLED の表示
 *
 * 拡張ボードの OLED (SSD1306, 128x64) に, 温度, 湿度, 日付, 時刻を表示する. OLED は,
 * Devicetree の alias thermo-display で選ぶ. 使わないときは, CONFIG_THERMO_DISPLAY=n で
 * ビルドする (このファイルの機能は, ビルドされない).
 */

#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t */

#include "node_time.h"

/** OLED に表示する内容 */
struct oled_view {
    bool has_sample;       /**< 測定値があるか (false のときは, 温度と湿度を --.- と表示する) */
    int16_t temp_x10;      /**< 温度 [℃ の 10 倍] */
    uint16_t humidity_x10; /**< 湿度 [% の 10 倍] (THERMO_HUMIDITY_NONE は, 測れない) */
    const struct node_datetime *time; /**< 日時 (NULL のときは, 日付と時刻の行を表示しない) */
};

int oled_init(void);

int oled_show(const struct oled_view *view);

#endif /* THERMO_NODE_OLED_H */
