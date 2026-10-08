/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node のエントリポイント
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h> /* true */
#include <stdint.h>  /* int16_t uint16_t */
#include <stdlib.h>  /* EXIT_SUCCESS EXIT_FAILURE */

#include "ble.h"
#ifdef CONFIG_THERMO_RTC
#include "node_time.h"
#endif
#ifdef CONFIG_THERMO_DISPLAY
#include "oled.h"
#endif
#include "sensor.h"
#include "thermo_ble_uuid.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(thermo_node, THERMO_LOG_LEVEL);

static void log_sample(int16_t temp_x10, uint16_t humidity_x10);

static int measure(int16_t *temp_x10, uint16_t *humidity_x10);

/** 温度を測定する間隔 [s] */
#define SAMPLE_INTERVAL_S 5U

/** メインループの周期 [s] (OLED の時刻を, 1 秒ごとに更新する) */
#define TICK_S 1U

/**
 * @brief ノードのメイン関数
 *
 * センサと BLE を初期化してアドバタイズを開始し, その後は 1 秒ごとのループで, 一定間隔
 * (SAMPLE_INTERVAL_S) で温度を読み取って, ログ出力して, 接続しているゲートウェイへ通知
 * (GATT の notify) する. ゲートウェイから時刻を受け取ったら (GATT の write), RTC に設定する
 * (CONFIG_THERMO_RTC). OLED があれば (CONFIG_THERMO_DISPLAY), 温度, 湿度, 時刻を, 1 秒ごとに
 * 表示する. 時刻は, RTC (CONFIG_THERMO_RTC) から読む. OLED と RTC の初期化に失敗しても, 表示と
 * 時刻を諦めるだけで, 測定と通知は続ける.
 *
 * @retval EXIT_FAILURE 初期化またはアドバタイズ開始に失敗した場合
 *                      (正常時はループから戻らない)
 */
int main(void)
{
    int ret = EXIT_SUCCESS;                     /* 戻り値 */
    unsigned int elapsed_s = SAMPLE_INTERVAL_S; /* 前回の測定からの経過 [s] (初回は, すぐ測る) */
    int16_t temp_x10 = 0;                       /* 温度 [℃ の 10 倍] */
    uint16_t humidity_x10 = 0;                  /* 湿度 [% の 10 倍] */
#ifdef CONFIG_THERMO_RTC
    struct node_datetime datetime = {0}; /* RTC のローカルタイム */
    bool rtc_ready = false;              /* RTC を使えるか */
#endif
#ifdef CONFIG_THERMO_DISPLAY
    struct oled_view view = {0}; /* OLED に表示する内容 */
    bool display_ready = false;  /* OLED を使えるか */
#endif

/* ビルド構成に応じて起動ログを切り替える */
#ifdef CONFIG_SIMULATOR
    LOG_INF("Thermo Node started (SIMULATOR MODE)");
#else
    LOG_INF("Thermo Node started on ESP32C3");
#endif

    /* センサを初期化する */
    ret = sensor_init();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to initialize sensor");
        return EXIT_FAILURE;
    }

    /* BLE を初期化する */
    ret = ble_init();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to initialize BLE");
        return EXIT_FAILURE;
    }

#ifdef CONFIG_THERMO_RTC
    /* ゲートウェイが SNTP で得た時刻を書き込んできたら, RTC に設定する */
    ble_set_time_callback(node_time_set);
#endif

    /* ゲートウェイから検出されるようアドバタイズを開始する */
    ret = ble_advertise();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to start BLE advertising");
        return EXIT_FAILURE;
    }

#ifdef CONFIG_THERMO_RTC
    /* RTC を初期化する (失敗しても, 時刻を諦めるだけで, 測定と通知は続ける) */
    ret = node_time_init();
    rtc_ready = (ret == EXIT_SUCCESS);
    if (!rtc_ready) {
        LOG_ERR("Failed to initialize the RTC (err %d), continuing without the time", ret);
    }
#endif

#ifdef CONFIG_THERMO_DISPLAY
    /* OLED を初期化する (失敗しても, 表示を諦めるだけで, 測定と通知は続ける) */
    ret = oled_init();
    display_ready = (ret == EXIT_SUCCESS);
    if (!display_ready) {
        LOG_ERR("Failed to initialize the OLED (err %d), continuing without it", ret);
    }
#endif

    /* 1 秒ごとに, 一定間隔で温度と湿度を測って, 時刻を表示する */
    while (true) {
        if (elapsed_s >= SAMPLE_INTERVAL_S) {
            elapsed_s = 0U;
            ret = measure(&temp_x10, &humidity_x10);
#ifdef CONFIG_THERMO_DISPLAY
            /* 読み取りに失敗したときは, 前回の値を表示し続ける */
            if (ret == EXIT_SUCCESS) {
                view.has_sample = true;
                view.temp_x10 = temp_x10;
                view.humidity_x10 = humidity_x10;
            }
#endif
        }

#ifdef CONFIG_THERMO_RTC
        if (rtc_ready) {
            /* 未設定や読み取りの失敗のときは, datetime.valid が false (時刻なしと表示する) */
            (void)node_time_get(&datetime);
#ifdef CONFIG_THERMO_DISPLAY
            view.time = &datetime;
#endif
        }
#endif

#ifdef CONFIG_THERMO_DISPLAY
        /* OLED に表示する (失敗しても次回に再試行する) */
        if (display_ready) {
            ret = oled_show(&view);
            if (ret != EXIT_SUCCESS) {
                LOG_ERR("Failed to show on the OLED (err %d)", ret);
            }
        }
#endif

        (void)k_sleep(K_SECONDS(TICK_S));
        elapsed_s += TICK_S;
    }
}

/**
 * 温度と湿度を読み取って, ログに出力して, ゲートウェイへ通知する
 *
 * 読み取りに失敗したときは, 今回の値を捨てて (通知しない), 次回に再試行する. 通知に失敗しても,
 * 次回に再試行する.
 *
 * @param[out] temp_x10     温度 [℃ の 10 倍] の格納先 (NULL 不可)
 * @param[out] humidity_x10 湿度 [% の 10 倍] の格納先 (NULL 不可)
 *
 * @retval EXIT_SUCCESS 読み取りに成功 (通知の失敗は, 戻り値に含めない)
 * @retval negative     センサの読み取りの失敗 (負の errno)
 */
static int measure(int16_t *temp_x10, uint16_t *humidity_x10)
{
    int ret = EXIT_SUCCESS;        /* 戻り値 */
    int notify_ret = EXIT_SUCCESS; /* 通知の戻り値 */

    ret = sensor_read(temp_x10, humidity_x10);
    if (ret != EXIT_SUCCESS) {
        return ret;
    }

    log_sample(*temp_x10, *humidity_x10);

    /* 接続しているゲートウェイへ通知する (失敗しても次回に再試行する) */
    notify_ret = ble_notify_temperature(*temp_x10, *humidity_x10);
    if (notify_ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to notify the temperature (err %d)", notify_ret);
    }

    return EXIT_SUCCESS;
}

/**
 * 温度と湿度をログに出力する
 *
 * 10 分の 1 の単位の整数を, 小数点付きの形 (例: -3.5 ℃, 45.0 %) で出力する.
 * 湿度を測れないとき (THERMO_HUMIDITY_NONE) は, 温度だけを出力する.
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍]
 */
static void log_sample(int16_t temp_x10, uint16_t humidity_x10)
{
    int temp = temp_x10; /* 温度 [℃ の 10 倍] */
    /* 0 ℃ 未満は整数部が 0 でも (-0.5 など) 符号を出すため, 符号を別に出力する */
    const char *sign = ((temp < 0) ? "-" : "");                         /* 符号 */
    unsigned int magnitude = (unsigned int)((temp < 0) ? -temp : temp); /* 絶対値 */

    if (humidity_x10 == THERMO_HUMIDITY_NONE) {
        LOG_INF("Temperature: %s%u.%u C", sign, magnitude / 10U, magnitude % 10U);
    } else {
        LOG_INF("Temperature: %s%u.%u C, humidity: %u.%u %%", sign, magnitude / 10U,
                magnitude % 10U, humidity_x10 / 10U, humidity_x10 % 10U);
    }
}
