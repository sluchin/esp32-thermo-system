/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief button.c の単体テスト
 *
 * GPIO のコントローラを偽のもの (vnd,test-gpio) に置き換えて, 次を確認する.
 *  - button_init() が, ピンを入力に設定して, 失敗を返すこと
 *  - button_pressed() が, 押されていない状態から, 押された状態に変わったときだけ, true を返すこと
 *    (押し続けても 1 回. 起動のときに押されていても, 押された動作ではない)
 */

/** 偽の GPIO コントローラのドライバが使う compatible (vnd,test-gpio) */
#define DT_DRV_COMPAT vnd_test_gpio

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>  /* ENODEV EIO */
#include <stdint.h> /* uint32_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "button.h"

/** ボタンをつなぐピンの番号 (app.overlay) */
#define BUTTON_PIN 3U
/** 押されていないときの, 3 番ピンの入力の値 (プルアップ. 押すと 0) */
#define LEVEL_UP   BIT(BUTTON_PIN)
/** 押されているときの, 3 番ピンの入力の値 */
#define LEVEL_DOWN 0U

/** 偽の GPIO コントローラの, ピンの設定の戻り値 */
static int configure_ret;
/** 偽の GPIO コントローラが, 最後に設定されたフラグ */
static gpio_flags_t configured_flags;
/** 偽の GPIO コントローラの, ポートの入力の値 */
static uint32_t port_level;
/** 偽の GPIO コントローラの, 共通のデータ (反転の設定を, gpio_pin_configure() が書く) */
static struct gpio_driver_data fake_data;
/** 偽の GPIO コントローラの, 共通の設定 (全てのピンを使える) */
static const struct gpio_driver_config fake_config = {.port_pin_mask = UINT32_MAX};

/**
 * 偽の GPIO コントローラの pin_configure (フラグを記録して, 設定した戻り値を返す)
 *
 * @param[in] port  コントローラ (使用しない)
 * @param[in] pin   ピンの番号 (使用しない)
 * @param[in] flags フラグ
 * @return configure_ret
 */
static int fake_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
    ARG_UNUSED(port);
    ARG_UNUSED(pin);
    configured_flags = flags;

    return configure_ret;
}

/**
 * 偽の GPIO コントローラの port_get_raw (設定した入力の値を返す)
 *
 * @param[in]  port  コントローラ (使用しない)
 * @param[out] value ポートの入力の値
 * @return 0
 */
static int fake_port_get_raw(const struct device *port, gpio_port_value_t *value)
{
    ARG_UNUSED(port);
    *value = port_level;

    return 0;
}

/** 偽の GPIO コントローラのドライバ API */
static DEVICE_API(gpio, fake_api) = {
    .pin_configure = fake_pin_configure,
    .port_get_raw = fake_port_get_raw,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, &fake_data, &fake_config, POST_KERNEL,
                      CONFIG_GPIO_INIT_PRIORITY, &fake_api)

/** 偽の GPIO コントローラ */
static const struct device *const fake_port = DEVICE_DT_GET(DT_NODELABEL(test_gpio));

/**
 * 各テストの前に, 偽の GPIO コントローラを, ボタンが押されていない状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    struct device_state *state = fake_port->state; /* コントローラの状態 */

    ARG_UNUSED(fixture);
    state->initialized = true;
    configure_ret = 0;
    configured_flags = 0U;
    port_level = LEVEL_UP;
}

/** ピンを入力に設定する (プルアップと, 押すと 0 になる設定は, Devicetree から) */
ZTEST(button, test_init_success)
{
    zassert_equal(button_init(), EXIT_SUCCESS);
    zassert_true((configured_flags & GPIO_INPUT) != 0U);
    zassert_true((configured_flags & GPIO_PULL_UP) != 0U);
}

/** GPIO のコントローラが準備できていなければ (初期化されていない), -ENODEV を返す */
ZTEST(button, test_init_not_ready)
{
    struct device_state *state = fake_port->state; /* コントローラの状態 */

    state->initialized = false;
    zassert_equal(button_init(), -ENODEV);
}

/** ピンの設定に失敗したら, その値を返す */
ZTEST(button, test_init_configure_failure)
{
    configure_ret = -EIO;

    zassert_equal(button_init(), -EIO);
}

/** 押された動作は 1 回だけ検出する (押し続けても, 離すまで 1 回. 離して, もう一度押すと, もう 1 回)
 */
ZTEST(button, test_pressed_detects_edge_once)
{
    zassert_equal(button_init(), EXIT_SUCCESS);
    zassert_false(button_pressed());

    port_level = LEVEL_DOWN;
    zassert_true(button_pressed());
    /* 押し続けている間は, 押された動作ではない */
    zassert_false(button_pressed());
    zassert_false(button_pressed());

    port_level = LEVEL_UP;
    zassert_false(button_pressed());

    port_level = LEVEL_DOWN;
    zassert_true(button_pressed());
}

/** 起動のときに押されていても (初期化のとき), それは, 押された動作ではない */
ZTEST(button, test_held_at_init_is_not_a_press)
{
    port_level = LEVEL_DOWN;
    zassert_equal(button_init(), EXIT_SUCCESS);
    zassert_false(button_pressed());

    port_level = LEVEL_UP;
    zassert_false(button_pressed());
    port_level = LEVEL_DOWN;
    zassert_true(button_pressed());
}

ZTEST_SUITE(button, NULL, NULL, before, NULL, NULL);
