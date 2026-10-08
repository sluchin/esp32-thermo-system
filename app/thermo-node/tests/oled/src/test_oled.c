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
 *  - oled_show_sample() が, 温度と湿度の行を, 正しい文字列と位置で描いて, 画面を更新すること
 *    (負の温度, 湿度なし, 各段階の失敗)
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

#include "oled.h"
#include "thermo_ble_uuid.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, cfb_framebuffer_init, const struct device *)
FAKE_VALUE_FUNC(int, cfb_framebuffer_set_font, const struct device *, uint8_t)
FAKE_VALUE_FUNC(int, cfb_framebuffer_clear, const struct device *, bool)
FAKE_VALUE_FUNC(int, cfb_print, const struct device *, const char *, uint16_t, uint16_t)
FAKE_VALUE_FUNC(int, cfb_framebuffer_finalize, const struct device *)

/** 描いた文字列を記録する行の数 */
#define PRINT_LINES     2U
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

/** 偽の表示装置の画素の形式の設定の戻り値 */
static int pixel_format_ret;
/** 偽の表示装置が最後に設定された画素の形式 */
static enum display_pixel_format pixel_format_set;
/** 偽の表示装置の表示の開始 (blanking_off) の戻り値 */
static int blanking_ret;
/** cfb_print() が記録した文字列 */
static char printed[PRINT_LINES][PRINT_SIZE];
/** cfb_print() が, 1 回目と 2 回目に返す値 */
static int print_ret[PRINT_LINES];

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
 * @return 呼ばれた順 (1 回目, 2 回目) の print_ret
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
    FFF_RESET_HISTORY();
    cfb_print_fake.custom_fake = fake_print;
    state->initialized = true;
    pixel_format_ret = 0;
    pixel_format_set = PIXEL_FORMAT_ARGB_8888;
    blanking_ret = 0;
    print_ret[0] = 0;
    print_ret[1] = 0;
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

/** 温度と湿度を, 1 行目と 2 行目に描いて, 最後に画面を更新する */
ZTEST(oled, test_show_sample_success)
{
    /* 期待: 成功して, 画面を消さずに (false), 2 行を描いて, 更新する */
    zassert_equal(oled_show_sample(TEMP_23_5_X10, HUMIDITY_45_X10), EXIT_SUCCESS);
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
ZTEST(oled, test_show_sample_negative_temperature)
{
    zassert_equal(oled_show_sample(TEMP_M3_5_X10, HUMIDITY_45_X10), EXIT_SUCCESS);
    zassert_str_equal(printed[0], "Temp -3.5 C");
}

/** 整数部が 0 の, 0 ℃ 未満の温度 (-0.5 ℃) も, 符号を落とさずに描く */
ZTEST(oled, test_show_sample_negative_fraction)
{
    zassert_equal(oled_show_sample(TEMP_M0_5_X10, HUMIDITY_45_X10), EXIT_SUCCESS);
    zassert_str_equal(printed[0], "Temp -0.5 C");
}

/** 湿度を測れないとき (THERMO_HUMIDITY_NONE) は, 湿度を --.- と描く */
ZTEST(oled, test_show_sample_no_humidity)
{
    zassert_equal(oled_show_sample(TEMP_23_5_X10, THERMO_HUMIDITY_NONE), EXIT_SUCCESS);
    zassert_str_equal(printed[1], "Humi --.- %");
}

/** フレームバッファの消去に失敗したら (初期化していないときなど), その値を返し, 何も描かない */
ZTEST(oled, test_show_sample_clear_failure)
{
    cfb_framebuffer_clear_fake.return_val = -ENODEV;

    zassert_equal(oled_show_sample(TEMP_23_5_X10, HUMIDITY_45_X10), -ENODEV);
    zassert_equal(cfb_print_fake.call_count, 0U);
}

/** 温度の行を描くのに失敗したら, その値を返し, 湿度の行には進まない */
ZTEST(oled, test_show_sample_print_temperature_failure)
{
    print_ret[0] = -EINVAL;

    zassert_equal(oled_show_sample(TEMP_23_5_X10, HUMIDITY_45_X10), -EINVAL);
    zassert_equal(cfb_print_fake.call_count, 1U);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 0U);
}

/** 湿度の行を描くのに失敗したら, その値を返し, 画面は更新しない */
ZTEST(oled, test_show_sample_print_humidity_failure)
{
    print_ret[1] = -EINVAL;

    zassert_equal(oled_show_sample(TEMP_23_5_X10, HUMIDITY_45_X10), -EINVAL);
    zassert_equal(cfb_print_fake.call_count, 2U);
    zassert_equal(cfb_framebuffer_finalize_fake.call_count, 0U);
}

/** 画面の更新に失敗したら, その値を返す */
ZTEST(oled, test_show_sample_finalize_failure)
{
    cfb_framebuffer_finalize_fake.return_val = -EIO;

    zassert_equal(oled_show_sample(TEMP_23_5_X10, HUMIDITY_45_X10), -EIO);
}

ZTEST_SUITE(oled, NULL, NULL, before, NULL, NULL);
