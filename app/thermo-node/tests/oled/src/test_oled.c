/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief oled.c の単体テスト
 *
 * 表示装置を偽のもの (vnd,test-display) に置き換えて, CFB の関数を FFF のモックにして,
 * 次を確認する.
 *  - oled_init() が, 正しい順序で初期化して, 各段階の失敗を返すこと
 *  - oled_show() が, 温度, 湿度, 日付, 時刻の行を, 正しい文字列と位置で描いて, 画面を更新すること
 *    (負の温度, 測定値なし, 湿度なし, 時刻なし, 日時なし, 各段階の失敗)
 *  - 温度と湿度のグラフのページが, 履歴の最小と最大の文字列と, 折れ線 (位置) を描くこと
 *    (履歴なし, 点が 1 つ, 縦軸の最小の幅, 各段階の失敗)
 */

/** 偽の表示装置のドライバが使う compatible (vnd,test-display) */
#define DT_DRV_COMPAT vnd_test_display

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <errno.h>   /* ENODEV EIO ENOMEM ENOTSUP EINVAL */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t */
#include <stdio.h>   /* snprintf */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* memset */

#include "history.h"
#include "node_time.h"
#include "oled.h"
#include "thermo_ble_uuid.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, cfb_framebuffer_init, const struct device *)
FAKE_VALUE_FUNC(int, cfb_framebuffer_set_font, const struct device *, uint8_t)
FAKE_VALUE_FUNC(int, cfb_framebuffer_clear, const struct device *, bool)
FAKE_VALUE_FUNC(int, cfb_print, const struct device *, const char *, uint16_t, uint16_t)
FAKE_VALUE_FUNC(int, cfb_framebuffer_finalize, const struct device *)
FAKE_VALUE_FUNC(int, cfb_draw_point, const struct device *, const struct cfb_position *)
FAKE_VALUE_FUNC(int, cfb_draw_line, const struct device *, const struct cfb_position *,
                const struct cfb_position *)

/** 描いた文字列を記録する行の数 */
#define PRINT_LINES     4U
/** 描いた文字列を記録する大きさ [バイト] */
#define PRINT_SIZE      32U
/** 1 行の高さ [ドット] (oled.c の OLED_LINE_HEIGHT) */
#define LINE_HEIGHT     16U
/** テストの温度 [℃ の 10 倍] (23.5 ℃) */
#define TEMP_23_5_X10   235
/** テストの温度 [℃ の 10 倍] (-3.5 ℃) */
#define TEMP_M3_5_X10   (-35)
/** テストの温度 [℃ の 10 倍] (-0.5 ℃. 整数部が 0 でも, 符号が付くこと) */
#define TEMP_M0_5_X10   (-5)
/** テストの湿度 [% の 10 倍] (45.0 %) */
#define HUMIDITY_45_X10 450U
/** テストの年 */
#define TEST_YEAR       2026U
/** テストの月 */
#define TEST_MONTH      10U
/** テストの日 */
#define TEST_DAY        8U
/** テストの時 */
#define TEST_HOUR       14U
/** テストの分 */
#define TEST_MINUTE     5U
/** テストの秒 */
#define TEST_SECOND     9U

/** グラフの 1 点の時間 [ms] (history_add() の区切りの長さ. テストの値) */
#define STEP_MS   10000
/** グラフの点の最大の数 */
#define MAX_DRAWN HISTORY_SIZE

/** 偽の表示装置の画素の形式の設定の戻り値 */
static int pixel_format_ret;
/** 偽の表示装置が最後に設定された画素の形式 */
static enum display_pixel_format pixel_format_set;
/** 偽の表示装置の表示の開始 (blanking_off) の戻り値 */
static int blanking_ret;
/** cfb_print() が記録した文字列 */
static char printed[PRINT_LINES][PRINT_SIZE];
/** cfb_draw_point() が描いた点 */
static struct cfb_position drawn_point;
/** cfb_draw_line() が描いた線の始点 (呼ばれた順) */
static struct cfb_position line_from[MAX_DRAWN];
/** cfb_draw_line() が描いた線の終点 (呼ばれた順) */
static struct cfb_position line_to[MAX_DRAWN];
/** cfb_print() が, 1 回目から 4 回目までに返す値 */
static int print_ret[PRINT_LINES];
/** 設定済みの日時 (2026-10-08 14:05:09) */
static const struct node_datetime time_valid = {
    .valid = true,
    .year = TEST_YEAR,
    .month = TEST_MONTH,
    .day = TEST_DAY,
    .hour = TEST_HOUR,
    .minute = TEST_MINUTE,
    .second = TEST_SECOND,
};
/** 未設定の日時 (valid が false) */
static const struct node_datetime time_unset = {.valid = false};

/**
 * 偽の表示装置の set_pixel_format (設定した形式を記録して, 設定した戻り値を返す)
 *
 * @param[in] dev 表示装置 (使用しない)
 * @param[in] pf  画素の形式
 * @return pixel_format_ret
 */
static int fake_set_pixel_format(const struct device *dev, const enum display_pixel_format pf)
{
    ARG_UNUSED(dev);
    pixel_format_set = pf;

    return pixel_format_ret;
}

/**
 * 偽の表示装置の blanking_off (設定した戻り値を返す)
 *
 * @param[in] dev 表示装置 (使用しない)
 * @return blanking_ret
 */
static int fake_blanking_off(const struct device *dev)
{
    ARG_UNUSED(dev);

    return blanking_ret;
}

/** 偽の表示装置のドライバ API */
static DEVICE_API(display, fake_api) = {
    .set_pixel_format = fake_set_pixel_format,
    .blanking_off = fake_blanking_off,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,
                      &fake_api)

/** 偽の表示装置 */
static const struct device *const fake_dev = DEVICE_DT_GET(DT_ALIAS(thermo_display));

/**
 * cfb_print() のモック動作 (描いた文字列を記録して, 設定した戻り値を返す)
 *
 * @param[in] dev 表示装置 (使用しない)
 * @param[in] str 描く文字列
 * @param[in] x   x 座標 (使用しない)
 * @param[in] y   y 座標 (使用しない)
 * @return 呼ばれた順 (1 回目から 4 回目) の print_ret
 */
static int fake_print(const struct device *dev, const char *str, uint16_t x, uint16_t y)
{
    unsigned int idx = cfb_print_fake.call_count - 1U; /* 今回の呼び出しの番号 (0 始まり) */

    ARG_UNUSED(dev);
    ARG_UNUSED(x);
    ARG_UNUSED(y);
    (void)snprintf(printed[idx], PRINT_SIZE, "%s", str);

    return print_ret[idx];
}

/**
 * cfb_draw_point() のモック動作 (描いた点を記録する)
 *
 * @param[in] dev 表示装置 (使用しない)
 * @param[in] pos 点の位置
 * @return 0
 */
static int fake_draw_point(const struct device *dev, const struct cfb_position *pos)
{
    ARG_UNUSED(dev);
    drawn_point = *pos;

    return 0;
}

/**
 * cfb_draw_line() のモック動作 (描いた線の始点と終点を記録する)
 *
 * @param[in] dev   表示装置 (使用しない)
 * @param[in] start 始点
 * @param[in] end   終点
 * @return 0
 */
static int fake_draw_line(const struct device *dev, const struct cfb_position *start,
                          const struct cfb_position *end)
{
    unsigned int idx = cfb_draw_line_fake.call_count - 1U; /* 今回の呼び出しの番号 (0 始まり) */

    ARG_UNUSED(dev);
    line_from[idx] = *start;
    line_to[idx] = *end;

    return 0;
}

/**
 * 各テストの前に, モックと偽の表示装置を, 正常な状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    struct device_state *state = fake_dev->state; /* 表示装置の状態 */

    ARG_UNUSED(fixture);
    RESET_FAKE(cfb_framebuffer_init);
    RESET_FAKE(cfb_framebuffer_set_font);
    RESET_FAKE(cfb_framebuffer_clear);
    RESET_FAKE(cfb_print);
    RESET_FAKE(cfb_framebuffer_finalize);
    RESET_FAKE(cfb_draw_point);
    RESET_FAKE(cfb_draw_line);
    cfb_draw_point_fake.custom_fake = fake_draw_point;
    cfb_draw_line_fake.custom_fake = fake_draw_line;
    (void)memset(&drawn_point, 0, sizeof(drawn_point));
    (void)memset(line_from, 0, sizeof(line_from));
    (void)memset(line_to, 0, sizeof(line_to));
    FFF_RESET_HISTORY();
    cfb_print_fake.custom_fake = fake_print;
    state->initialized = true;
    pixel_format_ret = 0;
    pixel_format_set = PIXEL_FORMAT_ARGB_8888;
    blanking_ret = 0;
    (void)memset(print_ret, 0, sizeof(print_ret));
    (void)memset(printed, 0, sizeof(printed));
}

/** 初期化に成功したら, 画素の形式, CFB の初期化, フォント, 消去, 表示の開始の順に行う */
ZTEST(oled, test_init_success)
{
    /* 期待: 成功して, 各段階を 1 回ずつ行う */
    zassert_equal(oled_init(), EXIT_SUCCESS);
    zassert_equal(pixel_format_set, PIXEL_FORMAT_MONO10);
    zassert_equal(cfb_framebuffer_init_fake.call_count, 1U);
    zassert_equal(cfb_framebuffer_set_font_fake.call_count, 1U);
    zassert_equal(cfb_framebuffer_set_font_fake.arg1_val, 0U);
    zassert_equal(cfb_framebuffer_clear_fake.call_count, 1U);
    /* 画面も消す (true) */
    zassert_true(cfb_framebuffer_clear_fake.arg1_val);
}

/** 表示装置が準備できていなければ (初期化されていない), -ENODEV を返す */
ZTEST(oled, test_init_not_ready)
{
    struct device_state *state = fake_dev->state; /* 表示装置の状態 */

    state->initialized = false;
    /* 期待: -ENODEV. CFB には進まない */
    zassert_equal(oled_init(), -ENODEV);
    zassert_equal(cfb_framebuffer_init_fake.call_count, 0U);
}

/** 画素の形式の設定に失敗したら, その値を返し, CFB には進まない */
ZTEST(oled, test_init_pixel_format_failure)
{
    pixel_format_ret = -ENOTSUP;

    zassert_equal(oled_init(), -ENOTSUP);
    zassert_equal(cfb_framebuffer_init_fake.call_count, 0U);
}

/** CFB の初期化に失敗したら, その値を返し, フォントの設定には進まない */
ZTEST(oled, test_init_framebuffer_failure)
{
    cfb_framebuffer_init_fake.return_val = -ENOMEM;

    zassert_equal(oled_init(), -ENOMEM);
    zassert_equal(cfb_framebuffer_set_font_fake.call_count, 0U);
}

/** フォントの設定に失敗したら, その値を返し, 消去には進まない */
ZTEST(oled, test_init_font_failure)
{
    cfb_framebuffer_set_font_fake.return_val = -EINVAL;

    zassert_equal(oled_init(), -EINVAL);
    zassert_equal(cfb_framebuffer_clear_fake.call_count, 0U);
}

/** 画面の消去に失敗したら, その値を返す */
ZTEST(oled, test_init_clear_failure)
{
    cfb_framebuffer_clear_fake.return_val = -EIO;

    zassert_equal(oled_init(), -EIO);
}

/** 表示の開始に失敗したら, その値を返す */
ZTEST(oled, test_init_blanking_failure)
{
    blanking_ret = -EIO;

    zassert_equal(oled_init(), -EIO);
}

/**
 * 測定値のある表示内容 (日時なし) で, oled_show() を呼ぶ
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍]
 * @return oled_show() の戻り値
 */
static int show_sample(int16_t temp_x10, uint16_t humidity_x10)
{
    const struct oled_view view = {
        .has_sample = true,
        .temp_x10 = temp_x10,
        .humidity_x10 = humidity_x10,
        .time = NULL,
    }; /* 表示内容 */

    return oled_show(&view);
}

/**
 * 測定値と日時のある表示内容で, oled_show() を呼ぶ
 *
 * @param[in] datetime 日時 (NULL 不可)
 * @return oled_show() の戻り値
 */
static int show_with_time(const struct node_datetime *datetime)
{
    const struct oled_view view = {
        .has_sample = true,
        .temp_x10 = TEMP_23_5_X10,
        .humidity_x10 = HUMIDITY_45_X10,
        .time = datetime,
    }; /* 表示内容 */

    return oled_show(&view);
}

/** 温度と湿度を, 1 行目と 2 行目に描いて, 最後に画面を更新する (日時なしなら 2 行だけ) */
ZTEST(oled, test_show_success)
{
    /* 期待: 成功して, 画面を消さずに (false), 2 行を描いて, 更新する */
    zassert_equal(show_sample(TEMP_23_5_X10, HUMIDITY_45_X10), EXIT_SUCCESS);
    zassert_false(cfb_framebuffer_clear_fake.arg1_val);
    zassert_equal(cfb_print_fake.call_count, 2U);
    zassert_str_equal(printed[0], "Temp 23.5 C");
    zassert_equal(cfb_print_fake.arg2_history[0], 0U);
    zassert_equal(cfb_print_fake.arg3_history[0], 0U);
    zassert_str_equal(printed[1], "Humi 45.0 %");
    zassert_equal(cfb_print_fake.arg2_history[1], 0U);
    zassert_equal(cfb_print_fake.arg3_history[1], LINE_HEIGHT);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 1U);
}

/** 0 ℃ 未満の温度は, 符号を付けて描く */
ZTEST(oled, test_show_negative_temperature)
{
    zassert_equal(show_sample(TEMP_M3_5_X10, HUMIDITY_45_X10), EXIT_SUCCESS);
    zassert_str_equal(printed[0], "Temp -3.5 C");
}

/** 整数部が 0 の, 0 ℃ 未満の温度 (-0.5 ℃) も, 符号を落とさずに描く */
ZTEST(oled, test_show_negative_fraction)
{
    zassert_equal(show_sample(TEMP_M0_5_X10, HUMIDITY_45_X10), EXIT_SUCCESS);
    zassert_str_equal(printed[0], "Temp -0.5 C");
}

/** 湿度を測れないとき (THERMO_HUMIDITY_NONE) は, 湿度を --.- と描く */
ZTEST(oled, test_show_no_humidity)
{
    zassert_equal(show_sample(TEMP_23_5_X10, THERMO_HUMIDITY_NONE), EXIT_SUCCESS);
    zassert_str_equal(printed[1], "Humi --.- %");
}

/** 測定値がまだないときは, 温度と湿度を --.- と描く */
ZTEST(oled, test_show_no_sample)
{
    const struct oled_view view = {
        .has_sample = false,
        .temp_x10 = TEMP_23_5_X10,
        .humidity_x10 = HUMIDITY_45_X10,
        .time = NULL,
    }; /* 表示内容 (値は, 使われない) */

    zassert_equal(oled_show(&view), EXIT_SUCCESS);
    zassert_str_equal(printed[0], "Temp --.- C");
    zassert_str_equal(printed[1], "Humi --.- %");
}

/** 設定済みの日時は, 1 行目に日付, 2 行目に時刻, その下に, 温度と湿度を描く */
ZTEST(oled, test_show_time)
{
    zassert_equal(show_with_time(&time_valid), EXIT_SUCCESS);
    zassert_equal(cfb_print_fake.call_count, 4U);
    zassert_str_equal(printed[0], "2026-10-08");
    zassert_equal(cfb_print_fake.arg3_history[0], 0U);
    zassert_str_equal(printed[1], "14:05:09");
    zassert_equal(cfb_print_fake.arg3_history[1], LINE_HEIGHT);
    zassert_str_equal(printed[2], "Temp 23.5 C");
    zassert_equal(cfb_print_fake.arg3_history[2], 2U * LINE_HEIGHT);
    zassert_str_equal(printed[3], "Humi 45.0 %");
    zassert_equal(cfb_print_fake.arg3_history[3], 3U * LINE_HEIGHT);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 1U);
}

/** 未設定の日時 (valid が false) は, 日付と時刻を --- で描く (温度と湿度は, その下) */
ZTEST(oled, test_show_time_unset)
{
    zassert_equal(show_with_time(&time_unset), EXIT_SUCCESS);
    zassert_equal(cfb_print_fake.call_count, 4U);
    zassert_str_equal(printed[0], "----/--/--");
    zassert_str_equal(printed[1], "--:--:--");
    zassert_str_equal(printed[2], "Temp 23.5 C");
    zassert_str_equal(printed[3], "Humi 45.0 %");
}

/** フレームバッファの消去に失敗したら (初期化していないときなど), その値を返し, 何も描かない */
ZTEST(oled, test_show_clear_failure)
{
    cfb_framebuffer_clear_fake.return_val = -ENODEV;

    zassert_equal(show_sample(TEMP_23_5_X10, HUMIDITY_45_X10), -ENODEV);
    zassert_equal(cfb_print_fake.call_count, 0U);
}

/** 温度の行を描くのに失敗したら, その値を返し, 次の行には進まない */
ZTEST(oled, test_show_print_first_failure)
{
    print_ret[0] = -EINVAL;

    zassert_equal(show_sample(TEMP_23_5_X10, HUMIDITY_45_X10), -EINVAL);
    zassert_equal(cfb_print_fake.call_count, 1U);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 0U);
}

/** 時刻の行 (最後の行) を描くのに失敗したら, その値を返し, 画面は更新しない */
ZTEST(oled, test_show_print_last_failure)
{
    print_ret[3] = -EINVAL;

    zassert_equal(show_with_time(&time_valid), -EINVAL);
    zassert_equal(cfb_print_fake.call_count, 4U);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 0U);
}

/** 画面の更新に失敗したら, その値を返す */
ZTEST(oled, test_show_finalize_failure)
{
    cfb_framebuffer_finalize_fake.return_val = -EIO;

    zassert_equal(show_sample(TEMP_23_5_X10, HUMIDITY_45_X10), -EIO);
}

/**
 * 履歴に, 区切りごとに 1 点ずつ, 値を加える
 *
 * @param[out] h      履歴 (0 で初期化する)
 * @param[in]  values 値 (古い順)
 * @param[in]  count  値の数
 */
static void fill_history(struct history *h, const int16_t *values, unsigned int count)
{
    unsigned int i = 0U; /* 値の番号 */

    (void)memset(h, 0, sizeof(*h));
    for (i = 0U; i < count; i++) {
        history_add(h, values[i], (int64_t)i * STEP_MS, STEP_MS);
    }
}

/**
 * グラフのページで, oled_show() を呼ぶ
 *
 * @param[in] page     ページ (OLED_PAGE_TEMP_GRAPH か OLED_PAGE_HUMIDITY_GRAPH)
 * @param[in] history  表示する履歴 (NULL なら, 履歴なし)
 * @return oled_show() の戻り値
 */
static int show_graph(unsigned int page, const struct history *history)
{
    const struct oled_view view = {
        .page = page,
        .temp_history = (page == OLED_PAGE_TEMP_GRAPH) ? history : NULL,
        .humidity_history = (page == OLED_PAGE_HUMIDITY_GRAPH) ? history : NULL,
    }; /* 表示内容 */

    return oled_show(&view);
}

/** 温度のグラフは, 1 行目に最小と最大, その下に, 右端が最新の折れ線を描く */
ZTEST(oled, test_graph_temperature)
{
    static struct history h; /* 履歴 */
    const int16_t values[] = {200, 250, 225};

    fill_history(&h, values, ARRAY_SIZE(values));
    zassert_equal(show_graph(OLED_PAGE_TEMP_GRAPH, &h), EXIT_SUCCESS);

    zassert_equal(cfb_print_fake.call_count, 1U);
    zassert_str_equal(printed[0], "20.0~25.0 C");
    /* 3 点は, 右端の 125 から 127. 縦は, 最小 (20.0) が 63, 最大 (25.0) が 20 */
    zassert_equal(drawn_point.x, 125U);
    zassert_equal(drawn_point.y, 63U);
    zassert_equal(cfb_draw_line_fake.call_count, 2U);
    zassert_equal(line_from[0].x, 125U);
    zassert_equal(line_from[0].y, 63U);
    zassert_equal(line_to[0].x, 126U);
    zassert_equal(line_to[0].y, 20U);
    zassert_equal(line_from[1].x, 126U);
    zassert_equal(line_from[1].y, 20U);
    zassert_equal(line_to[1].x, 127U);
    zassert_equal(line_to[1].y, 42U);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 1U);
}

/** 湿度のグラフは, 湿度の履歴を描いて, 単位は % */
ZTEST(oled, test_graph_humidity)
{
    static struct history h; /* 履歴 */
    const int16_t values[] = {450, 520};

    fill_history(&h, values, ARRAY_SIZE(values));
    zassert_equal(show_graph(OLED_PAGE_HUMIDITY_GRAPH, &h), EXIT_SUCCESS);

    zassert_str_equal(printed[0], "45.0~52.0 %");
    zassert_equal(cfb_draw_line_fake.call_count, 1U);
}

/** 履歴の途中に, 最小と最大があっても, 最小と最大を, 正しく見つける (負の温度も) */
ZTEST(oled, test_graph_extremes_and_negative)
{
    static struct history h; /* 履歴 */
    const int16_t values[] = {-10, -35, 15, -5};

    fill_history(&h, values, ARRAY_SIZE(values));
    zassert_equal(show_graph(OLED_PAGE_TEMP_GRAPH, &h), EXIT_SUCCESS);

    zassert_str_equal(printed[0], "-3.5~1.5 C");
}

/** 履歴がなければ (履歴が NULL, または, 空), グラフは描かずに No data を表示する */
ZTEST(oled, test_graph_no_data)
{
    static struct history h; /* 空の履歴 */

    (void)memset(&h, 0, sizeof(h));
    zassert_equal(show_graph(OLED_PAGE_TEMP_GRAPH, NULL), EXIT_SUCCESS);
    zassert_str_equal(printed[0], "No data");
    zassert_equal(cfb_print_fake.arg3_history[0], LINE_HEIGHT);
    zassert_equal(cfb_draw_point_fake.call_count, 0U);

    zassert_equal(show_graph(OLED_PAGE_TEMP_GRAPH, &h), EXIT_SUCCESS);
    zassert_equal(cfb_print_fake.call_count, 2U);
    zassert_equal(cfb_draw_point_fake.call_count, 0U);
}

/** 点が 1 つなら, 線は描かずに, 右端に点を描く */
ZTEST(oled, test_graph_single_point)
{
    static struct history h; /* 履歴 */
    const int16_t values[] = {235};

    fill_history(&h, values, ARRAY_SIZE(values));
    zassert_equal(show_graph(OLED_PAGE_TEMP_GRAPH, &h), EXIT_SUCCESS);

    zassert_str_equal(printed[0], "23.5~23.5 C");
    zassert_equal(drawn_point.x, 127U);
    zassert_equal(drawn_point.y, 63U);
    zassert_equal(cfb_draw_line_fake.call_count, 0U);
}

/** 変化が小さいとき (1.0 ℃ 未満) は, 縦軸の幅を 1.0 ℃ にして, 拡大しすぎない */
ZTEST(oled, test_graph_small_range_is_not_magnified)
{
    static struct history h; /* 履歴 */
    const int16_t values[] = {200, 203};

    fill_history(&h, values, ARRAY_SIZE(values));
    zassert_equal(show_graph(OLED_PAGE_TEMP_GRAPH, &h), EXIT_SUCCESS);

    /* 幅 3 (0.3 ℃) でなく, 幅 10 (1.0 ℃) を 43 ドットにする: 3 * 43 / 10 = 12 ドット上 */
    zassert_equal(line_to[0].y, 63U - 12U);
}

/** 最小と最大の行を描くのに失敗したら, その値を返し, 折れ線は描かない */
ZTEST(oled, test_graph_print_failure)
{
    static struct history h; /* 履歴 */
    const int16_t values[] = {200, 250};

    fill_history(&h, values, ARRAY_SIZE(values));
    print_ret[0] = -EINVAL;

    zassert_equal(show_graph(OLED_PAGE_TEMP_GRAPH, &h), -EINVAL);
    zassert_equal(cfb_draw_point_fake.call_count, 0U);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 0U);
}

/** 最初の点を描くのに失敗したら, その値を返し, 画面は更新しない */
ZTEST(oled, test_graph_draw_point_failure)
{
    static struct history h; /* 履歴 */
    const int16_t values[] = {200, 250};

    fill_history(&h, values, ARRAY_SIZE(values));
    cfb_draw_point_fake.custom_fake = NULL;
    cfb_draw_point_fake.return_val = -EIO;

    zassert_equal(show_graph(OLED_PAGE_TEMP_GRAPH, &h), -EIO);
    zassert_equal(cfb_draw_line_fake.call_count, 0U);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 0U);
}

/** 線を描くのに失敗したら, その値を返し, 画面は更新しない */
ZTEST(oled, test_graph_draw_line_failure)
{
    static struct history h; /* 履歴 */
    const int16_t values[] = {200, 250, 300};

    fill_history(&h, values, ARRAY_SIZE(values));
    cfb_draw_line_fake.custom_fake = NULL;
    cfb_draw_line_fake.return_val = -EIO;

    zassert_equal(show_graph(OLED_PAGE_TEMP_GRAPH, &h), -EIO);
    zassert_equal(cfb_draw_line_fake.call_count, 1U);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 0U);
}

ZTEST_SUITE(oled, NULL, NULL, before, NULL, NULL);
