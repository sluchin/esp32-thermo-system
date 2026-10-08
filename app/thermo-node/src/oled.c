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
/** 1 行の文字列の大きさ [バイト] (幅 128 ドット / 10 ドット = 12 文字 + 終端) */
#define OLED_LINE_SIZE    16U
/** 10 分の 1 の単位から, 整数部と小数部を取り出すための値 */
#define OLED_X10_DIVISOR  10U

/** Devicetree (alias thermo-display) から取得した OLED */
static const struct device *const oled_dev = DEVICE_DT_GET(DT_ALIAS(thermo_display));

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
 * @brief 温度と湿度を OLED に表示する
 *
 * 1 行目に温度 (例: Temp 23.5 C), 2 行目に湿度 (例: Humi 45.0 %) を表示する.
 * 湿度を測れないとき (THERMO_HUMIDITY_NONE) は, 湿度を --.- と表示する.
 * oled_init() が成功してから呼ぶ (初期化していないと, CFB が -ENODEV を返す).
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍]
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     CFB の失敗 (負の errno. 初期化していないときは -ENODEV)
 */
int oled_show_sample(int16_t temp_x10, uint16_t humidity_x10)
{
    char line[OLED_LINE_SIZE] = {0}; /* 1 行の文字列 */
    int temp = temp_x10;             /* 温度 [℃ の 10 倍] */
    /* 0 ℃ 未満は整数部が 0 でも (-0.5 など) 符号を出すため, 符号を別に出力する */
    const char *sign = ((temp < 0) ? "-" : "");                         /* 符号 */
    unsigned int magnitude = (unsigned int)((temp < 0) ? -temp : temp); /* 絶対値 */
    int err = EXIT_SUCCESS;                                             /* エラーコード */

    err = cfb_framebuffer_clear(oled_dev, false);
    if (err != 0) {
        LOG_ERR("Could not clear the framebuffer of the OLED (%d)", err);
        return err;
    }

    (void)snprintf(line, sizeof(line), "Temp %s%u.%u C", sign, magnitude / OLED_X10_DIVISOR,
                   magnitude % OLED_X10_DIVISOR);
    err = draw_line(line, OLED_ROW_TEMP);
    if (err != 0) {
        return err;
    }

    if (humidity_x10 == THERMO_HUMIDITY_NONE) {
        (void)snprintf(line, sizeof(line), "Humi --.- %%");
    } else {
        (void)snprintf(line, sizeof(line), "Humi %u.%u %%", humidity_x10 / OLED_X10_DIVISOR,
                       humidity_x10 % OLED_X10_DIVISOR);
    }
    err = draw_line(line, OLED_ROW_HUMIDITY);
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
