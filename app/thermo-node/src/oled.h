/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_OLED_H
#define THERMO_NODE_OLED_H

/**
 * @file
 * @brief thermo-node の OLED の表示
 *
 * 拡張ボードの OLED (SSD1306, 128x64) に, 温度, 湿度, 日付, 時刻 (現在値のページ) と, 温度と
 * 湿度の履歴の折れ線グラフ (グラフのページ) を表示する. ページは, ボタンで切り替える
 * (button.h. main.c が行う). OLED は,
 * Devicetree の alias thermo-display で選ぶ. 使わないときは, CONFIG_THERMO_DISPLAY=n で
 * ビルドする (このファイルの機能は, ビルドされない).
 */

#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t */

#include "history.h"
#include "node_time.h"

/** ページ: 現在値 (温度, 湿度, 日付, 時刻) */
#define OLED_PAGE_NOW            0U
/** ページ: 温度の履歴のグラフ */
#define OLED_PAGE_TEMP_GRAPH     1U
/** ページ: 湿度の履歴のグラフ */
#define OLED_PAGE_HUMIDITY_GRAPH 2U
/** ページの数 (ボタンで, 順に切り替えて, 最後の次は, 最初に戻る) */
#define OLED_PAGE_COUNT          3U

/** OLED に表示する内容 */
struct oled_view {
    bool has_sample;       /**< 測定値があるか (false のときは, 温度と湿度を --.- と表示する) */
    int16_t temp_x10;      /**< 温度 [℃ の 10 倍] */
    uint16_t humidity_x10; /**< 湿度 [% の 10 倍] (THERMO_HUMIDITY_NONE は, 測れない) */
    const struct node_datetime *time;       /**< 日時 (NULL のときは, 日付と時刻の行を表示しない) */
    unsigned int page;                      /**< 表示するページ (OLED_PAGE_*) */
    const struct history *temp_history;     /**< 温度の履歴 (NULL なら, グラフは, データなし) */
    const struct history *humidity_history; /**< 湿度の履歴 (NULL なら, グラフは, データなし) */
};

int oled_init(void);

int oled_show(const struct oled_view *view);

#endif /* THERMO_NODE_OLED_H */
