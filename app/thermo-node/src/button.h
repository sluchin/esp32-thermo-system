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
 * ボタンは Devicetree の alias thermo-button で選ぶ. 割り込みは使わずに, 呼び出すたびに
 * ボタンの状態を読む. 押されていない状態から押された状態に変わったときだけ, 押されたと判断
 * する. 呼び出す間隔 (50 ms ほど) がチャタリングを無視する時間になる.
 * 使わないときは CONFIG_THERMO_DISPLAY=n でビルドする (このファイルの機能はビルドされない).
 */

#include <stdbool.h> /* bool */

int button_init(void);

bool button_is_pressed(void);

#endif /* THERMO_NODE_BUTTON_H */
