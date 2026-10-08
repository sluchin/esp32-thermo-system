/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BUTTON_H
#define THERMO_NODE_BUTTON_H

/**
 * @file
 * @brief thermo-node のボタン (拡張ボードのユーザボタン. OLED の画面の切り替え用)
 *
 * ボタンは, Devicetree の alias thermo-button で選ぶ. 割り込みは使わずに, 呼び出すたびに,
 * 押されている状態を読んで, 押されていない状態から, 押された状態に変わったときだけ, 押されたと
 * 判断する. 呼び出す間隔 (50 ms ほど) が, チャタリングを無視する時間になる.
 * 使わないときは, CONFIG_THERMO_DISPLAY=n でビルドする (このファイルの機能は, ビルドされない).
 */

#include <stdbool.h> /* bool */

int button_init(void);

bool button_pressed(void);

#endif /* THERMO_NODE_BUTTON_H */
