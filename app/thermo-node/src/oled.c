/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の OLED の表示の実装 (Zephyr の CFB: 文字のフレームバッファ)
 */

#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <errno.h>  /* ENODEV */
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "node_time.h"
#include "oled.h"
#include "thermo_ble_uuid.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(oled_thermo_node, THERMO_LOG_LEVEL);

/** 表示する文字のフォントの番号 (CFB の既定のフォントの 0 番: 幅 10 x 高さ 16 ドット) */
#define OLED_FONT_INDEX   0U
/** 1 行の高さ [ドット] (フォントの高さ) */
#define OLED_LINE_HEIGHT  16U
/** 温度の行の位置 (上から) [行] */
#define OLED_ROW_TEMP     0U
/** 湿度の行の位置 (上から) [行] */
#define OLED_ROW_HUMIDITY 1U
/** 日付の行の位置 (上から) [行] */
#define OLED_ROW_DATE     2U
/** 時刻の行の位置 (上から) [行] */
#define OLED_ROW_TIME     3U
/** 表示する行の数 (画面の高さ 64 ドット / 16 ドット) */
#define OLED_ROWS         4U
/** 日時を表示しないときの行の数 (温度と湿度) */
#define OLED_ROWS_SAMPLE  2U
/** 1 行の文字列の大きさ [バイト] (幅 128 ドット / 10 ドット = 12 文字 + 終端) */
#define OLED_LINE_SIZE    16U
/** 10 分の 1 の単位から, 整数部と小数部を取り出すための値 */
#define OLED_X10_DIVISOR  10U

/** Devicetree (alias thermo-display) から取得した OLED */
static const struct device *const oled_dev = DEVICE_DT_GET(DT_ALIAS(thermo_display));

static unsigned int format_lines(const struct oled_view *view, char lines[][OLED_LINE_SIZE]);

static int draw_line(const char *text, uint16_t row);

/**
 * @brief OLED を初期化する
 *
 * 画素の形式を設定して, 文字のフレームバッファを初期化し, 画面を消して, 表示を始める.
 * 失敗したときは, 表示しないだけで, ノードのほかの動作は続けられる (呼び出し側で判断する).
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      OLED が利用可能でない
 * @retval negative     ドライバまたは CFB の失敗 (負の errno)
 */
int oled_init(void)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    if (!device_is_ready(oled_dev)) {
        LOG_ERR("OLED is not ready");
        return -ENODEV;
    }

    err = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO10);
    if (err != 0) {
        LOG_ERR("Could not set the pixel format of the OLED (%d)", err);
        return err;
    }

    err = cfb_framebuffer_init(oled_dev);
    if (err != 0) {
        LOG_ERR("Could not initialize the framebuffer of the OLED (%d)", err);
        return err;
    }

    err = cfb_framebuffer_set_font(oled_dev, OLED_FONT_INDEX);
    if (err != 0) {
        LOG_ERR("Could not set the font of the OLED (%d)", err);
        return err;
    }

    err = cfb_framebuffer_clear(oled_dev, true);
    if (err != 0) {
        LOG_ERR("Could not clear the OLED (%d)", err);
        return err;
    }

    err = display_blanking_off(oled_dev);
    if (err != 0) {
        LOG_ERR("Could not turn on the OLED (%d)", err);
        return err;
    }

    LOG_INF("OLED initialized");

    return EXIT_SUCCESS;
}

/**
 * @brief 温度, 湿度, 日付, 時刻を OLED に表示する
 *
 * 1 行目に温度 (例: Temp 23.5 C), 2 行目に湿度 (例: Humi 45.0 %), 3 行目に日付 (例: 2026-10-08),
 * 4 行目に時刻 (例: 14:05:09) を表示する. 測定値がないとき, 湿度を測れないとき
 * (THERMO_HUMIDITY_NONE), 時刻が未設定のときは, その値を --.- (日付と時刻は, ---- と --) に
 * する. 日時が NULL のときは, 日付と時刻の行を出さない.
 * oled_init() が成功してから呼ぶ (初期化していないと, CFB が -ENODEV を返す).
 *
 * @param[in] view 表示する内容 (NULL 不可)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno. 初期化していないときは -ENODEV)
 */
int oled_show(const struct oled_view *view)
{
    char lines[OLED_ROWS][OLED_LINE_SIZE] = {{0}}; /* 行ごとの文字列 */
    unsigned int rows = 0U;                        /* 表示する行の数 */
    unsigned int row = 0U;                         /* 行の位置 (上から 0 始まり) */
    int err = EXIT_SUCCESS;                        /* エラーコード */

    rows = format_lines(view, lines);

    err = cfb_framebuffer_clear(oled_dev, false);
    if (err != 0) {
        LOG_ERR("Could not clear the framebuffer of the OLED (%d)", err);
        return err;
    }

    for (row = 0U; row < rows; row++) {
        err = draw_line(lines[row], (uint16_t)row);
        if (err != 0) {
            return err;
        }
    }

    err = cfb_framebuffer_finalize(oled_dev);
    if (err != 0) {
        LOG_ERR("Could not update the OLED (%d)", err);
        return err;
    }

    return EXIT_SUCCESS;
}

/**
 * 1 行の文字列を, フレームバッファに書く (画面は, まだ更新しない)
 *
 * @param[in] text 文字列
 * @param[in] row  行の位置 (上から 0 始まり)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno)
 */
static int draw_line(const char *text, uint16_t row)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    err = cfb_print(oled_dev, text, 0U, (uint16_t)(row * OLED_LINE_HEIGHT));
    if (err != 0) {
        LOG_ERR("Could not print to the OLED (%d)", err);
        return err;
    }

    return EXIT_SUCCESS;
}

/**
 * 表示する内容を, 行ごとの文字列にする
 *
 * @param[in]  view  表示する内容 (NULL 不可)
 * @param[out] lines 行ごとの文字列の格納先 (OLED_ROWS 行)
 *
 * @return 表示する行の数 (日時が NULL のときは 2, あるときは 4)
 */
static unsigned int format_lines(const struct oled_view *view, char lines[][OLED_LINE_SIZE])
{
    int temp = view->temp_x10; /* 温度 [℃ の 10 倍] */
    /* 0 ℃ 未満は整数部が 0 でも (-0.5 など) 符号を出すため, 符号を別に出力する */
    const char *sign = ((temp < 0) ? "-" : "");                         /* 符号 */
    unsigned int magnitude = (unsigned int)((temp < 0) ? -temp : temp); /* 絶対値 */

    if (view->has_sample) {
        (void)snprintf(lines[OLED_ROW_TEMP], OLED_LINE_SIZE, "Temp %s%u.%u C", sign,
                       magnitude / OLED_X10_DIVISOR, magnitude % OLED_X10_DIVISOR);
    } else {
        (void)snprintf(lines[OLED_ROW_TEMP], OLED_LINE_SIZE, "Temp --.- C");
    }

    if (view->has_sample && (view->humidity_x10 != THERMO_HUMIDITY_NONE)) {
        (void)snprintf(lines[OLED_ROW_HUMIDITY], OLED_LINE_SIZE, "Humi %u.%u %%",
                       view->humidity_x10 / OLED_X10_DIVISOR,
                       view->humidity_x10 % OLED_X10_DIVISOR);
    } else {
        (void)snprintf(lines[OLED_ROW_HUMIDITY], OLED_LINE_SIZE, "Humi --.- %%");
    }

    if (view->time == NULL) {
        return OLED_ROWS_SAMPLE;
    }

    if (view->time->valid) {
        (void)snprintf(lines[OLED_ROW_DATE], OLED_LINE_SIZE, "%04u-%02u-%02u",
                       (unsigned int)view->time->year, (unsigned int)view->time->month,
                       (unsigned int)view->time->day);
        (void)snprintf(lines[OLED_ROW_TIME], OLED_LINE_SIZE, "%02u:%02u:%02u",
                       (unsigned int)view->time->hour, (unsigned int)view->time->minute,
                       (unsigned int)view->time->second);
    } else {
        (void)snprintf(lines[OLED_ROW_DATE], OLED_LINE_SIZE, "----/--/--");
        (void)snprintf(lines[OLED_ROW_TIME], OLED_LINE_SIZE, "--:--:--");
    }

    return OLED_ROWS;
}
