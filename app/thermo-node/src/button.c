/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node のボタンの実装 (GPIO を読む)
 */

#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>   /* ENODEV */
#include <stdbool.h> /* bool true false */
#include <stdlib.h>  /* EXIT_SUCCESS */

#include "button.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(button_thermo_node, THERMO_LOG_LEVEL);

/** Devicetree (alias thermo-button) から取得したボタンの GPIO */
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(thermo_button), gpios);

/** 前回の呼び出しで, 押されていたか */
static bool was_down;

/**
 * @brief ボタンを初期化する (入力に設定する)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      GPIO が利用可能でない
 * @retval negative     GPIO の設定の失敗 (負の errno)
 */
int button_init(void)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    if (!gpio_is_ready_dt(&button)) {
        LOG_ERR("Button GPIO is not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (err != 0) {
        LOG_ERR("Could not configure the button (%d)", err);
        return err;
    }

    /* 起動のときに押されていても, 押された動作とは, みなさない */
    was_down = (gpio_pin_get_dt(&button) > 0);
    LOG_INF("Button initialized");

    return EXIT_SUCCESS;
}

/**
 * @brief ボタンが押されたかを調べる
 *
 * 押されていない状態から, 押された状態に変わった呼び出しだけ, true を返す (押し続けても,
 * 1 回だけ). GPIO を読めないときは, 押されていないとする.
 *
 * @retval true  前回の呼び出しのあとに, 押された
 * @retval false 押されていない (押し続けている場合を含む)
 */
bool button_pressed(void)
{
    bool down = (gpio_pin_get_dt(&button) > 0); /* 今, 押されているか */
    bool pressed = (down && !was_down);         /* 押された動作か */

    was_down = down;

    return pressed;
}
