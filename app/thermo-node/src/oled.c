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
#include <stdbool.h> /* bool */
#include <errno.h>   /* ENODEV */
#include <stddef.h>  /* size_t */
#include <stdio.h>   /* snprintf */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* strlen */

#ifdef CONFIG_THERMO_BOOT_SCREEN
#include "boot_assets.h"
#endif
#include "history.h"
#include "node_time.h"
#include "oled.h"
#include "thermo_ble_uuid.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(oled_thermo_node, THERMO_LOG_LEVEL);

/** 表示する文字のフォントの番号 (CFB の既定のフォントの 0 番: 幅 10 x 高さ 16 ドット) */
#define OLED_FONT_INDEX    0U
/** 1 行の高さ [ドット] (フォントの高さ) */
#define OLED_LINE_HEIGHT   16U
/** 日付の行の位置 (上から) [行] */
#define OLED_ROW_DATE      0U
/** 時刻の行の位置 (上から) [行] */
#define OLED_ROW_TIME      1U
/** 日付と時刻の行の数 (温度と湿度の行は, その下) */
#define OLED_ROWS_DATETIME 2U
/** 温度の行の位置 (日付と時刻の行の下から) [行] */
#define OLED_ROW_TEMP      0U
/** 湿度の行の位置 (日付と時刻の行の下から) [行] */
#define OLED_ROW_HUMIDITY  1U
/** 温度と湿度の行の数 */
#define OLED_ROWS_SAMPLE   2U
/** 表示する行の最大数 (画面の高さ 64 ドット / 16 ドット) */
#define OLED_ROWS          4U
/** 1 行の文字列の大きさ [バイト] (幅 128 ドット / 10 ドット = 12 文字 + 終端. 余裕を持たせる) */
#define OLED_LINE_SIZE     24U
/** 数値の文字列 (10 分の 1 の単位を, 小数点付きにしたもの. 最長は -3276.8) の大きさ [バイト] */
#define OLED_NUM_SIZE      8U
#ifdef CONFIG_THERMO_BOOT_SCREEN
/** 起動画面: 画面の左端の x 座標 [ドット] */
#define OLED_LEFT_X       0
/** 起動画面: 絵を走らせる行の, 上端の y 座標 [ドット] */
#define BOOT_LANE_Y       24
/** 起動画面: 1 フレームで, 走る絵が進む距離 [ドット] */
#define BOOT_STEP_PX      3
/** 起動画面: 走る絵の, 最初の x 座標 (画面の左の外. 絵の幅だけ, 左) */
#define BOOT_START_X      (-(int)BOOT_SPRITE_SIZE)
/** 起動画面: 追いかける敵の絵が, 走る絵から, 遅れる距離 [ドット] */
#define BOOT_CHASER_GAP   28
/** 起動画面: ドットの間隔 [ドット] */
#define BOOT_DOT_PITCH    8
/** 起動画面: 最初のドットの x 座標 [ドット] */
#define BOOT_DOT_START_X  4
/** 起動画面: ドットの数 */
#define BOOT_DOT_COUNT    16
/** 起動画面: ドットの 1 辺の大きさ [ドット] */
#define BOOT_DOT_SIZE     2
/** 起動画面: ドットの y 座標の, 絵の上端からの距離 (絵の真ん中あたり) [ドット] */
#define BOOT_DOT_OFFSET_Y 7
/** 起動画面: 文字 1 つの幅 [ドット] (フォント 0 は, 幅 10 ドット) */
#define BOOT_FONT_WIDTH   10U
/** 起動画面: 口の開き方の, 1 周の長さ (閉じる, 半分, 開く, 半分) */
#define BOOT_MOUTH_PERIOD 4U
#endif
/** 画面の幅 [ドット] (グラフの点の数と同じ) */
#define OLED_WIDTH               128U
/** グラフの, 一番上の y 座標 (上の文字の行の下. 16 ドットの文字の行と, 少し間をあける) */
#define OLED_GRAPH_TOP           20
/** グラフの, 一番下の y 座標 (画面の一番下) */
#define OLED_GRAPH_BOTTOM        63
/** グラフの縦軸の, 最小の幅 [10 分の 1 の単位] (1.0 ℃. 小さな変化を, 拡大しすぎない) */
#define OLED_GRAPH_MIN_RANGE_X10 10
/** データがないときの文字を出す行 (上から) [行] */
#define OLED_ROW_NO_DATA         1U
/** 10 分の 1 の単位から, 整数部と小数部を取り出すための値 */
#define OLED_X10_DIVISOR         10U

/** Devicetree (alias thermo-display) から取得した OLED */
static const struct device *const oled_dev = DEVICE_DT_GET(DT_ALIAS(thermo_display));

#ifdef CONFIG_THERMO_BOOT_SCREEN
static int draw_sprite(const struct boot_sprite *sprite, int x, int y);

static int draw_dots(int eaten_x);

static bool is_in_range(int value, int min, int max);

static int draw_boot_frame(unsigned int frame);
#endif

static int draw_now(const struct oled_view *view);

static int draw_graph(const struct oled_view *view);

static int draw_polyline(const int16_t *points, unsigned int count, int16_t low, int range);

static unsigned int format_lines(const struct oled_view *view, char lines[][OLED_LINE_SIZE]);

static void format_x10(char *out, size_t size, int16_t value_x10);

static int draw_line(const char *text, uint16_t row);

/**
 * @brief OLED を初期化する
 *
 * 画素の形式を設定して, 文字のフレームバッファを初期化し, 画面を消して, 表示を始める.
 * 失敗したときは, 表示しないだけで, ノードのほかの動作は続けられる (呼び出し側で判断する).
 * 失敗したあとに, もう一度呼んでもよい (確保したフレームバッファは, 解放してからやり直す).
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

    /* やり直しのときに, 前のフレームバッファを解放する (cfb_framebuffer_init() は, 解放しない) */
    cfb_framebuffer_deinit(oled_dev);

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
 * @brief 指定したページを OLED に表示する
 *
 * 現在値のページ (OLED_PAGE_NOW) は, 1 行目に日付 (例: 2026-10-08), 2 行目に時刻
 * (例: 14:05:09), 3 行目に温度 (例: Temp 23.5 C), 4 行目に湿度 (例: Humi 45.0 %) を表示する.
 * 測定値がないとき, 湿度を測れないとき (THERMO_HUMIDITY_NONE), 時刻が未設定のときは, その値を
 * --.- (日付と時刻は, ---- と --) にする. 日時が NULL のときは, 日付と時刻の行を出さない.
 * グラフのページ (OLED_PAGE_TEMP_GRAPH, OLED_PAGE_HUMIDITY_GRAPH) は, 1 行目に, 履歴の最小と
 * 最大 (例: 19.7~25.1 C) を表示して, その下に, 履歴の折れ線グラフ (右端が, 最新) を描く.
 * 履歴がないときは, No data を表示する.
 * oled_init() が成功してから呼ぶ (初期化していないと, CFB が -ENODEV を返す).
 *
 * @param[in] view 表示する内容 (NULL 不可)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno. 初期化していないときは -ENODEV)
 */
int oled_show(const struct oled_view *view)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    err = cfb_framebuffer_clear(oled_dev, false);
    if (err != 0) {
        LOG_ERR("Could not clear the framebuffer of the OLED (%d)", err);
        return err;
    }

    if (view->page == OLED_PAGE_NOW) {
        err = draw_now(view);
    } else {
        err = draw_graph(view);
    }
    if (err != 0) {
        return err;
    }

    err = cfb_framebuffer_finalize(oled_dev);
    if (err != 0) {
        LOG_ERR("Could not update the OLED (%d)", err);
        return err;
    }

    return EXIT_SUCCESS;
}

#ifdef CONFIG_THERMO_BOOT_SCREEN
/**
 * @brief 起動画面の, 1 フレームを OLED に表示する
 *
 * 画面の上に, 題名 (boot_title) を出して, その下の行を, 口を開け閉めする丸い絵 (boot_chomper) が,
 * ドットを食べながら, 左から右へ走る. 少し後ろから, 敵の絵 (boot_chaser) が追う. frame は 0 から
 * OLED_BOOT_FRAMES - 1 まで (それ以上は, 最後のフレーム). OLED_BOOT_FRAME_MS ごとに, 進める.
 * oled_init() が成功してから呼ぶ.
 *
 * @param[in] frame フレームの番号 (0 から始まる)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno. 初期化していないときは -ENODEV)
 */
int oled_show_boot(unsigned int frame)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    err = cfb_framebuffer_clear(oled_dev, false);
    if (err != 0) {
        LOG_ERR("Could not clear the framebuffer of the OLED (%d)", err);
        return err;
    }

    err = draw_boot_frame((frame < OLED_BOOT_FRAMES) ? frame : (OLED_BOOT_FRAMES - 1U));
    if (err != 0) {
        return err;
    }

    err = cfb_framebuffer_finalize(oled_dev);
    if (err != 0) {
        LOG_ERR("Could not update the OLED (%d)", err);
        return err;
    }

    return EXIT_SUCCESS;
}
#endif

/**
 * 現在値のページを, フレームバッファに描く (画面は, まだ更新しない)
 *
 * @param[in] view 表示する内容 (NULL 不可)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno)
 */
static int draw_now(const struct oled_view *view)
{
    char lines[OLED_ROWS][OLED_LINE_SIZE] = {{0}}; /* 行ごとの文字列 */
    unsigned int rows = 0U;                        /* 表示する行の数 */
    unsigned int row = 0U;                         /* 行の位置 (上から 0 始まり) */
    int err = EXIT_SUCCESS;                        /* エラーコード */

    rows = format_lines(view, lines);
    for (row = 0U; row < rows; row++) {
        err = draw_line(lines[row], (uint16_t)row);
        if (err != 0) {
            return err;
        }
    }

    return EXIT_SUCCESS;
}

/**
 * グラフのページを, フレームバッファに描く (画面は, まだ更新しない)
 *
 * @param[in] view 表示する内容 (NULL 不可. page が, 温度か湿度のグラフ)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno)
 */
static int draw_graph(const struct oled_view *view)
{
    const bool is_temp = (view->page == OLED_PAGE_TEMP_GRAPH); /* 温度のグラフか */
    const struct history *history =
            (is_temp ? view->temp_history : view->humidity_history); /* 描く履歴 */
    int16_t points[HISTORY_SIZE] = {0};                              /* 履歴の点 (古い順) */
    unsigned int count = 0U;                                         /* 点の数 */
    int16_t low = 0;                                                 /* 最小 [10 分の 1 の単位] */
    int16_t high = 0;                                                /* 最大 [10 分の 1 の単位] */
    char low_text[OLED_NUM_SIZE] = {0};                              /* 最小の文字列 */
    char high_text[OLED_NUM_SIZE] = {0};                             /* 最大の文字列 */
    char line[OLED_LINE_SIZE] = {0};                                 /* 1 行目の文字列 */
    unsigned int i = 0U;                                             /* 点の番号 */
    int err = EXIT_SUCCESS;                                          /* エラーコード */

    if (history != NULL) {
        count = history_get(history, points, HISTORY_SIZE);
    }

    if (count == 0U) {
        return draw_line("No data", (uint16_t)OLED_ROW_NO_DATA);
    }

    low = points[0];
    high = points[0];
    for (i = 1U; i < count; i++) {
        if (points[i] < low) {
            low = points[i];
        }
        if (points[i] > high) {
            high = points[i];
        }
    }

    format_x10(low_text, sizeof(low_text), low);
    format_x10(high_text, sizeof(high_text), high);
    (void)snprintf(line, sizeof(line), "%s~%s %s", low_text, high_text, (is_temp ? "C" : "%"));
    err = draw_line(line, 0U);
    if (err != 0) {
        return err;
    }

    /* 縦軸は, 最小から最大 (最小の幅は, OLED_GRAPH_MIN_RANGE_X10) */
    return draw_polyline(
            points, count, low,
            (((high - low) > OLED_GRAPH_MIN_RANGE_X10) ? (high - low) : OLED_GRAPH_MIN_RANGE_X10));
}

/**
 * 折れ線を, フレームバッファに描く (右端が, 最新の点)
 *
 * @param[in] points 点 (古い順. NULL 不可)
 * @param[in] count  点の数 (1 以上, OLED_WIDTH 以下)
 * @param[in] low    縦軸の下端の値 (グラフの下端の y が, この値)
 * @param[in] range  縦軸の幅 (上端の値 - 下端の値. 1 以上)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno)
 */
static int draw_polyline(const int16_t *points, unsigned int count, int16_t low, int range)
{
    struct cfb_position prev = {0}; /* 前の点の位置 */
    struct cfb_position cur = {0};  /* 今の点の位置 */
    unsigned int i = 0U;            /* 点の番号 */
    int err = EXIT_SUCCESS;         /* エラーコード */

    for (i = 0U; i < count; i++) {
        cur.x = (uint16_t)((OLED_WIDTH - count) + i);
        cur.y = (uint16_t)(OLED_GRAPH_BOTTOM -
                           (((points[i] - low) * (OLED_GRAPH_BOTTOM - OLED_GRAPH_TOP)) / range));
        if (i == 0U) {
            err = cfb_draw_point(oled_dev, &cur);
        } else {
            err = cfb_draw_line(oled_dev, &prev, &cur);
        }
        if (err != 0) {
            LOG_ERR("Could not draw the graph on the OLED (%d)", err);
            return err;
        }
        prev = cur;
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
    char temp_text[OLED_NUM_SIZE] = {0}; /* 温度の文字列 (例: -3.5) */
    unsigned int first = 0U;             /* 温度の行の位置 (日付と時刻があれば, その下) */

    /* 日付と時刻は, 一番上 */
    if (view->time != NULL) {
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
        first = OLED_ROWS_DATETIME;
    }

    if (view->has_sample) {
        format_x10(temp_text, sizeof(temp_text), view->temp_x10);
        (void)snprintf(lines[first + OLED_ROW_TEMP], OLED_LINE_SIZE, "Temp %s C", temp_text);
    } else {
        (void)snprintf(lines[first + OLED_ROW_TEMP], OLED_LINE_SIZE, "Temp --.- C");
    }

    if (view->has_sample && (view->humidity_x10 != THERMO_HUMIDITY_NONE)) {
        (void)snprintf(lines[first + OLED_ROW_HUMIDITY], OLED_LINE_SIZE, "Humi %u.%u %%",
                       view->humidity_x10 / OLED_X10_DIVISOR,
                       view->humidity_x10 % OLED_X10_DIVISOR);
    } else {
        (void)snprintf(lines[first + OLED_ROW_HUMIDITY], OLED_LINE_SIZE, "Humi --.- %%");
    }

    return first + OLED_ROWS_SAMPLE;
}

/**
 * 10 分の 1 の単位の値を, 小数点付きの文字列にする (例: -35 は -3.5)
 *
 * 0 未満は整数部が 0 でも (-0.5 など) 符号を出すため, 符号を別に出力する.
 *
 * @param[out] out        文字列の格納先
 * @param[in]  size       格納先の大きさ [バイト]
 * @param[in]  value_x10  値 [10 分の 1 の単位]
 */
static void format_x10(char *out, size_t size, int16_t value_x10)
{
    int value = value_x10;                                                 /* 値 */
    const char *sign = ((value < 0) ? "-" : "");                           /* 符号 */
    unsigned int magnitude = (unsigned int)((value < 0) ? -value : value); /* 絶対値 */

    (void)snprintf(out, size, "%s%u.%u", sign, magnitude / OLED_X10_DIVISOR,
                   magnitude % OLED_X10_DIVISOR);
}

#ifdef CONFIG_THERMO_BOOT_SCREEN
/**
 * 起動画面の 1 フレームを, フレームバッファに描く (画面は, まだ更新しない)
 *
 * @param[in] frame フレームの番号 (0 から OLED_BOOT_FRAMES - 1)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno)
 */
static int draw_boot_frame(unsigned int frame)
{
    /* 口の開き方: 閉じる (0), 半分 (1), 開く (2), 半分 (1) の順に, 繰り返す */
    const unsigned int phase = frame % BOOT_MOUTH_PERIOD;                   /* 口の位相 */
    const unsigned int mouth = (phase <= 2U) ? phase : 1U;                  /* 口の種類 */
    const int chomper_x = BOOT_START_X + ((int)frame * BOOT_STEP_PX);       /* 走る絵の x */
    const unsigned int title_chars = (unsigned int)strlen(boot_title.text); /* 題名の文字数 */
    const unsigned int title_x = (OLED_WIDTH - (title_chars * BOOT_FONT_WIDTH)) / 2U; /* 題名の x */
    int err = EXIT_SUCCESS; /* エラーコード */

    err = cfb_print(oled_dev, boot_title.text, (uint16_t)title_x, 0U);
    if (err != 0) {
        LOG_ERR("Could not print the title to the OLED (%d)", err);
        return err;
    }

    err = draw_dots(chomper_x + ((int)BOOT_SPRITE_SIZE / 2));
    if (err != 0) {
        return err;
    }

    err = draw_sprite(&boot_chomper[mouth], chomper_x, BOOT_LANE_Y);
    if (err != 0) {
        return err;
    }

    return draw_sprite(&boot_chaser, chomper_x - BOOT_CHASER_GAP, BOOT_LANE_Y);
}

/**
 * まだ食べられていないドットを描く (走る絵より右にあるドットだけ)
 *
 * @param[in] eaten_x これより左のドットは, 食べられた (x 座標 [ドット])
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno)
 */
static int draw_dots(int eaten_x)
{
    struct cfb_position pos = {0}; /* 点の位置 */
    int dot_x = 0;                 /* ドットの x 座標 */
    int dx = 0;                    /* ドットの中の x のずれ */
    int dy = 0;                    /* ドットの中の y のずれ */
    int i = 0;                     /* ドットの番号 */
    int err = EXIT_SUCCESS;        /* エラーコード */

    for (i = 0; i < BOOT_DOT_COUNT; i++) {
        dot_x = BOOT_DOT_START_X + (i * BOOT_DOT_PITCH);
        for (dy = 0; (dy < BOOT_DOT_SIZE) && (eaten_x < dot_x); dy++) {
            for (dx = 0; dx < BOOT_DOT_SIZE; dx++) {
                pos.x = (uint16_t)(dot_x + dx);
                pos.y = (uint16_t)(BOOT_LANE_Y + BOOT_DOT_OFFSET_Y + dy);
                err = cfb_draw_point(oled_dev, &pos);
                if (err != 0) {
                    LOG_ERR("Could not draw a dot on the OLED (%d)", err);
                    return err;
                }
            }
        }
    }

    return EXIT_SUCCESS;
}

/**
 * 絵を, フレームバッファに描く (画面の外にはみ出した部分は, 描かない)
 *
 * @param[in] sprite 絵 (NULL 不可)
 * @param[in] x      絵の左上の x 座標 [ドット] (画面の外でもよい)
 * @param[in] y      絵の左上の y 座標 [ドット] (画面の中)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno)
 */
static int draw_sprite(const struct boot_sprite *sprite, int x, int y)
{
    struct cfb_position pos = {0}; /* 点の位置 */
    int px = 0;                    /* 点の x 座標 */
    unsigned int row = 0U;         /* 絵の中の行 */
    unsigned int col = 0U;         /* 絵の中の列 */
    unsigned int bits = 0U;        /* 絵の 1 行ぶん (最上位ビットが左端) */
    int err = EXIT_SUCCESS;        /* エラーコード */

    for (row = 0U; row < BOOT_SPRITE_SIZE; row++) {
        bits = sprite->rows[row];
        for (col = 0U; col < BOOT_SPRITE_SIZE; col++) {
            px = x + (int)col;
            if ((((bits >> (BOOT_SPRITE_SIZE - 1U - col)) & 1U) != 0U) &&
                is_in_range(px, OLED_LEFT_X, (int)OLED_WIDTH - 1)) {
                pos.x = (uint16_t)px;
                pos.y = (uint16_t)(y + (int)row);
                err = cfb_draw_point(oled_dev, &pos);
                if (err != 0) {
                    LOG_ERR("Could not draw a sprite on the OLED (%d)", err);
                    return err;
                }
            }
        }
    }

    return EXIT_SUCCESS;
}
/**
 * 値が範囲に入っているかを調べる (数直線の順に, min <= value <= max と書く)
 *
 * @param[in] value 調べる値
 * @param[in] min   範囲の最小 (この値を含む)
 * @param[in] max   範囲の最大 (この値を含む)
 *
 * @retval true  min 以上 max 以下
 * @retval false 範囲外
 */
static bool is_in_range(int value, int min, int max)
{
    return (min <= value) && (value <= max);
}
#endif
