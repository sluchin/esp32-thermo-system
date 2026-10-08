/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の測定値の履歴の実装
 */

#include <stdint.h> /* int16_t int32_t int64_t */

#include "history.h"

static void push(struct history *h, int16_t value);

/**
 * @brief 測定値を履歴に加える
 *
 * 稼働時間を step_ms ごとの区切りに分けて, 区切りが変わったら, その区切りの平均を 1 点として
 * 覚える. 同じ区切りの中の測定値は, 合計に加えるだけ.
 *
 * @param[in,out] h       履歴 (NULL 不可)
 * @param[in]     value   測定値
 * @param[in]     now_ms  今の稼働時間 [ms]
 * @param[in]     step_ms 区切りの長さ [ms] (1 以上)
 */
void history_add(struct history *h, int16_t value, int64_t now_ms, int64_t step_ms)
{
    int64_t bucket = now_ms / step_ms; /* 今の区切りの番号 */

    if ((h->n > 0U) && (bucket != h->bucket)) {
        push(h, (int16_t)(h->sum / (int32_t)h->n));
        h->sum = 0;
        h->n = 0U;
    }

    h->bucket = bucket;
    h->sum += value;
    h->n++;
}

/**
 * @brief 履歴の点を, 古い順に取り出す
 *
 * 今の区切りに測定値があれば, その平均を, 最後の点に含める. 取り出せる点が max より多いときは,
 * 新しい方から max 個だけ取り出す.
 *
 * @param[in]  h   履歴 (NULL 不可)
 * @param[out] out 点の格納先 (max 個以上. NULL 不可)
 * @param[in]  max 取り出す最大の数
 *
 * @return 取り出した点の数
 */
unsigned int history_get(const struct history *h, int16_t *out, unsigned int max)
{
    unsigned int total = h->count + ((h->n > 0U) ? 1U : 0U);  /* 取り出せる点の数 */
    unsigned int skip = ((total > max) ? (total - max) : 0U); /* 捨てる (古い) 点の数 */
    unsigned int oldest = (h->head + HISTORY_SIZE - h->count) % HISTORY_SIZE; /* 最も古い点 */
    unsigned int i = 0U; /* 点の番号 (古い順) */

    for (i = skip; i < total; i++) {
        if (i < h->count) {
            out[i - skip] = h->points[(oldest + i) % HISTORY_SIZE];
        } else {
            out[i - skip] = (int16_t)(h->sum / (int32_t)h->n);
        }
    }

    return total - skip;
}

/**
 * 履歴に 1 点を加える (満杯なら, 最も古い点を上書きする)
 *
 * @param[in,out] h     履歴 (NULL 不可)
 * @param[in]     value 点の値
 */
static void push(struct history *h, int16_t value)
{
    h->points[h->head] = value;
    h->head = (h->head + 1U) % HISTORY_SIZE;
    if (h->count < HISTORY_SIZE) {
        h->count++;
    }
}
