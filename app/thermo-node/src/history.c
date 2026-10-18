/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の測定値の履歴の実装
 */

#include <zephyr/sys/util.h> /* MIN */
#include <stdbool.h>         /* bool */
#include <stdint.h>          /* int16_t int32_t int64_t */

#include "history.h"

static void push(struct history *h, int16_t value);

static void push_at(struct history *h, int64_t bucket, int16_t value);

/**
 * @brief 測定値を履歴に加える
 *
 * 稼働時間を step_ms ごとの区切りに分けて, 区切りが変わったら, その区切りの平均を 1 点として
 * 覚える. 同じ区切りの中の測定値は合計に加えるだけ. 測定値のなかった区切りは HISTORY_NONE の
 * 点として覚える.
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
        push_at(h, h->bucket, (int16_t)(h->sum / (int32_t)h->n));
        h->sum = 0;
        h->n = 0U;
    }

    h->bucket = bucket;
    h->sum += value;
    h->n++;
}

/**
 * @brief 履歴の点を古い順に取り出す
 *
 * 今の区切りに測定値があれば, その平均を最後の点に含める. 取り出せる点が max より多いときは
 * 新しい方から max 個だけ取り出す.
 *
 * @param[in]  h   履歴 (NULL 不可)
 * @param[out] out 点の格納先 (max 個以上. NULL 不可)
 * @param[in]  max 取り出す最大の数
 * @return 取り出した点の数
 */
unsigned int history_get(const struct history *h, int16_t *out, unsigned int max)
{
    const bool has_current = (h->n > 0U); /* 今の区切りに測定値があるか */
    const unsigned int gap =
            ((has_current && (h->count > 0U) && (h->end < h->bucket))
                     ? (unsigned int)MIN(h->bucket - h->end, (int64_t)HISTORY_SIZE)
                     : 0U); /* 最後に覚えた点と, 今の区切りの間の, 測定値のない区切りの数 */
    unsigned int total = h->count + gap + (has_current ? 1U : 0U); /* 取り出せる点の数 */
    unsigned int skip = ((total > max) ? (total - max) : 0U);      /* 捨てる (古い) 点の数 */
    unsigned int oldest = (h->head + HISTORY_SIZE - h->count) % HISTORY_SIZE; /* 最も古い点 */
    unsigned int i = 0U; /* 点の番号 (古い順) */

    for (i = skip; i < total; i++) {
        if (i < h->count) {
            out[i - skip] = h->points[(oldest + i) % HISTORY_SIZE];
        } else if (i < (h->count + gap)) {
            out[i - skip] = HISTORY_NONE;
        } else {
            out[i - skip] = (int16_t)(h->sum / (int32_t)h->n);
        }
    }

    return total - skip;
}

/**
 * @brief 最後に覚えた点 (区切りが終わった最新の点) を返す
 *
 * @param[in] h 履歴 (NULL 不可. 点を 1 つ以上覚えていること)
 * @return 最後に覚えた点の値
 */
int16_t history_last(const struct history *h)
{
    return h->points[(h->head + HISTORY_SIZE - 1U) % HISTORY_SIZE];
}

/**
 * @brief 保存してあった点を履歴に戻す
 *
 * 起動のあとに SD カードから読んだ点を古い順に戻すために使う. 前の点との間に測定値のない
 * 区切りがあれば, HISTORY_NONE の点を入れる. 今の区切りの測定値は変えない (戻す点の区切りは
 * 今の区切りより前であること).
 *
 * @param[in,out] h      履歴 (NULL 不可)
 * @param[in]     bucket 点の区切りの番号 (前に戻した点より大きいこと)
 * @param[in]     value  点の値
 */
void history_restore(struct history *h, int64_t bucket, int16_t value)
{
    push_at(h, bucket, value);
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

/**
 * 区切りの番号を付けて, 履歴に 1 点を加える
 *
 * 前の点との間に区切りがあれば (測定値がなかった区切り), その数だけ HISTORY_NONE の点を先に加える.
 * 数は HISTORY_SIZE まで (それ以上は全て押し出されるので).
 *
 * @param[in,out] h      履歴 (NULL 不可)
 * @param[in]     bucket 点の区切りの番号
 * @param[in]     value  点の値
 */
static void push_at(struct history *h, int64_t bucket, int16_t value)
{
    int64_t gap = 0; /* 前の点との間の, 測定値のない区切りの数 */
    int64_t i = 0;   /* 数える番号 */

    if ((h->count > 0U) && (h->end < bucket)) {
        gap = MIN(bucket - h->end, (int64_t)HISTORY_SIZE);
        for (i = 0; i < gap; i++) {
            push(h, HISTORY_NONE);
        }
    }
    push(h, value);
    h->end = bucket + 1;
}
