/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BOOT_ASSETS_H
#define THERMO_NODE_BOOT_ASSETS_H

/**
 * @file
 * @brief thermo-node の起動画面と起動音の素材 (絵, 題名, メロディ)
 *
 * 素材は, boot_assets.c が定義する (このアプリは, オリジナルの素材を持つ). ビルドのときに,
 * app/thermo-node/private/src/boot_assets.c があれば, そちらを使う (別のプライベートリポジトリを,
 * サブモジュールとして置くための, 差し替えの仕組み. SETUP.md の「起動画面と起動音」を参照).
 * 差し替えるファイルは, ここで宣言した全てを, 同じ名前と形で, 定義すること.
 */

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint16_t */

#include "buzzer.h"
#include "notes.h" /* NOTE_* (private/src/boot_assets.c が使う) */

/** 絵の 1 辺の大きさ [ドット] (幅と高さは, 同じ) */
#define BOOT_SPRITE_SIZE 16U

/** 題名の大きさ [バイト] (12 文字 + 終端. 幅 128 ドットに, 収める) */
#define BOOT_TITLE_SIZE 13U

/** 走る絵の, 口の開き方の種類の数 (閉じる, 半分, 開く) */
#define BOOT_CHOMPER_FRAMES 3U

/** 題名 (12 文字まで. 長い題名は, 配列の大きさが合わず, コンパイルエラーになる) */
struct boot_title {
    char text[BOOT_TITLE_SIZE]; /**< 文字列 (終端の NUL を含む) */
};

/** 絵 (BOOT_SPRITE_SIZE x BOOT_SPRITE_SIZE の, 1 ビット/ドット) */
struct boot_sprite {
    uint16_t rows[BOOT_SPRITE_SIZE]; /**< 1 行ぶん (最上位ビットが左端. 1 が点灯) */
};

extern const struct boot_sprite boot_chomper[BOOT_CHOMPER_FRAMES];

extern const struct boot_sprite boot_chaser;

extern const struct boot_title boot_title;

extern const struct buzzer_note boot_melody[];

extern const size_t boot_melody_count;

#endif /* THERMO_NODE_BOOT_ASSETS_H */
