/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の, ボタンを押したときの音の素材 (このアプリのオリジナル)
 *
 * 約 0.1 秒の, 上がる 2 音の, 短い音. このアプリ用に, 作ったもの.
 */

#include <zephyr/sys/util.h> /* ARRAY_SIZE */

#include "button_assets.h"

/** ボタンを押したときの音 (音名 [notes.h], 長さ [ms]. NOTE_REST は休符) */
const struct buzzer_note button_melody[] = {
    {NOTE_A5, 40},
    {NOTE_E6, 60},
};

/** 音の数 */
const size_t button_melody_count = ARRAY_SIZE(button_melody);
