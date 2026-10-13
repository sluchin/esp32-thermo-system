/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_HISTORY_STORE_H
#define THERMO_NODE_HISTORY_STORE_H

/**
 * @file
 * @brief グラフの履歴の, 起動のときの復元 (SD カードの記録から)
 *
 * 測定値は backlog.c が全て SD カードに保存している (送信済みフラグつき). 起動したあと RTC に
 * 時刻が入ったら (1 回だけ), 直近の記録 (送信済みも含む) をグラフの区切り
 * (CONFIG_THERMO_GRAPH_STEP_SEC) ごとの平均にして, 履歴に戻す. 電源が切れていた間など, 記録の
 * なかった区切りは測定値のない点になる. 使わないときは CONFIG_THERMO_HISTORY_STORE=n で
 * ビルドする.
 */

#include <zephyr/sys/util.h> /* ARG_UNUSED */
#include <stdint.h>          /* int64_t */

#include "history.h"

#ifdef CONFIG_THERMO_HISTORY_STORE

void history_store_update(struct history *temp, struct history *humidity, int64_t now_ms,
                          int64_t step_ms);

void history_store_reset(void);

#else

/**
 * 何もしない (履歴を SD カードから戻さない)
 *
 * @param[in,out] temp     使用しない
 * @param[in,out] humidity 使用しない
 * @param[in]     now_ms   使用しない
 * @param[in]     step_ms  使用しない
 */
static inline void history_store_update(struct history *temp, struct history *humidity,
                                        int64_t now_ms, int64_t step_ms)
{
    ARG_UNUSED(temp);
    ARG_UNUSED(humidity);
    ARG_UNUSED(now_ms);
    ARG_UNUSED(step_ms);
}

#endif /* CONFIG_THERMO_HISTORY_STORE */

#endif /* THERMO_NODE_HISTORY_STORE_H */
