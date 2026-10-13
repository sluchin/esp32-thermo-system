/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BUTTON_ASSETS_H
#define THERMO_NODE_BUTTON_ASSETS_H

/**
 * @file
 * @brief thermo-node のボタンを押したときの音の素材
 *
 * 素材は button_assets.c が定義する (このアプリはオリジナルの短い音を持つ). ビルドのときに,
 * app/thermo-node/private/src/button_assets.c があれば, そちらを使う (別のプライベートリポジトリを
 * サブモジュールとして置くための差し替えの仕組み. boot_assets.h と同じ. SETUP.md
 * の「ボタンを押したときの音」を参照). 差し替えるファイルはここで宣言した全てを同じ名前と形で,
 * 定義すること. 音は notes.h の音名で書ける.
 */

#include <stddef.h> /* size_t */

#include "buzzer.h"
#include "notes.h" /* NOTE_* (private/src/button_assets.c が使う) */

extern const struct buzzer_note button_melody[];

extern const size_t button_melody_count;

#endif /* THERMO_NODE_BUTTON_ASSETS_H */
