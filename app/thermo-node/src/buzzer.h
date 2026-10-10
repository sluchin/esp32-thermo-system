/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BUZZER_H
#define THERMO_NODE_BUZZER_H

/**
 * @file
 * @brief thermo-node のブザー (拡張ボードのパッシブブザー. PWM で音を出す)
 *
 * ブザーは, Devicetree の alias thermo-buzzer (pwms を持つノード) で選ぶ. パッシブ型なので, PWM の
 * 周波数が, 音の高さになる (デューティ比は 50%). メロディは, 音の表 (周波数と長さ) を, 順に鳴らす.
 * 鳴らすのは, システムのワークキューで行うので, 呼び出し元は, 待たされない.
 * 使わないときは, CONFIG_THERMO_BUZZER=n でビルドする (このファイルの機能は, ビルドされない).
 */

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint16_t */

/** メロディの 1 つの音 */
struct buzzer_note {
    uint16_t freq_hz;     /**< 音の高さ (周波数) [Hz]. 0 のときは, 休符 (音を出さない) */
    uint16_t duration_ms; /**< 音の長さ [ms] */
};

int buzzer_init(void);

int buzzer_play(const struct buzzer_note *notes, size_t count);

#endif /* THERMO_NODE_BUZZER_H */
