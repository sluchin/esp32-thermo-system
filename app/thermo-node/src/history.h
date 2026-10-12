/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_HISTORY_H
#define THERMO_NODE_HISTORY_H

/**
 * @file
 * @brief thermo-node の測定値の履歴 (OLED のグラフ用)
 *
 * 測定値を一定の時間 (step_ms) ごとの平均にして, 新しい方から, HISTORY_SIZE 個だけ覚える
 * (古いものは, 上書きする). 時間の区切りに入っている途中の平均も, グラフに含める. 測定値が
 * 1 つもなかった区切り (電源が切れていた間など) は, HISTORY_NONE の点にする (グラフに線を引かない).
 */

#include <stdint.h> /* int16_t int32_t int64_t INT16_MIN */

/** 覚える点の数 (OLED の幅 128 ドットと同じ. 1 点が 1 ドット) */
#define HISTORY_SIZE 128U

/** 測定値がなかった区切りの点の値 (温度と湿度の 10 倍の値では, 取らない値) */
#define HISTORY_NONE INT16_MIN

/** 測定値の履歴 (0 で初期化して使う) */
struct history {
    int16_t points[HISTORY_SIZE]; /**< 区切りごとの平均 (リングバッファ) */
    unsigned int head;            /**< 次に書き込む位置 (points の添字) */
    unsigned int count;           /**< 覚えている点の数 (HISTORY_SIZE まで) */
    int64_t end;    /**< 最後に覚えた点の区切りの番号 + 1 (count が 1 以上のときだけ使う) */
    int64_t bucket; /**< 今の区切りの番号 (稼働時間 / step_ms) */
    int32_t sum;    /**< 今の区切りの測定値の合計 */
    unsigned int n; /**< 今の区切りの測定値の数 */
};

void history_add(struct history *h, int16_t value, int64_t now_ms, int64_t step_ms);

int16_t history_last(const struct history *h);

void history_restore(struct history *h, int64_t bucket, int16_t value);

unsigned int history_get(const struct history *h, int16_t *out, unsigned int max);

#endif /* THERMO_NODE_HISTORY_H */
