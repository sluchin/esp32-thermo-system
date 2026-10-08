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
 * 拡張ボードの OLED (SSD1306, 128x64) に, 温度と湿度を表示する. OLED は, Devicetree の alias
 * thermo-display で選ぶ. 使わないときは, CONFIG_THERMO_DISPLAY=n でビルドする (このファイルの
 * 機能は, ビルドされない).
 */

#include <stdint.h> /* int16_t uint16_t */

int oled_init(void);

int oled_show_sample(int16_t temp_x10, uint16_t humidity_x10);

#endif /* THERMO_NODE_OLED_H */
