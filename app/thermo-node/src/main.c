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
#include "sensor.h"
#include "thermo_ble_uuid.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(thermo_node, THERMO_LOG_LEVEL);

static void log_sample(int16_t temp_x10, uint16_t humidity_x10);

/** 温度を測定する間隔 [s] */
#define SAMPLE_INTERVAL_S 5

/**
 * @brief ノードのメイン関数
 *
 * センサと BLE を初期化してアドバタイズを開始し, その後は一定間隔で温度を読み取って,
 * ログ出力して, 接続しているゲートウェイへ通知 (GATT の notify) する.
 *
 * @retval EXIT_FAILURE 初期化またはアドバタイズ開始に失敗した場合
 *                      (正常時はループから戻らない)
 */
int main(void)
{
    int ret = EXIT_SUCCESS; /* 戻り値 */

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

    /* ゲートウェイから検出されるようアドバタイズを開始する */
    ret = ble_advertise();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to start BLE advertising");
        return EXIT_FAILURE;
    }

    /* 一定間隔で温度と湿度を読み取ってログ出力する */
    while (true) {
        int16_t temp_x10 = 0;      /* 温度 [℃ の 10 倍] */
        uint16_t humidity_x10 = 0; /* 湿度 [% の 10 倍] */
        int read_ret = EXIT_SUCCESS;

        read_ret = sensor_read(&temp_x10, &humidity_x10);
        /* 読み取りに失敗した場合は今回の値を捨てて次回に再試行する */
        if (read_ret == EXIT_SUCCESS) {
            log_sample(temp_x10, humidity_x10);

            /* 接続しているゲートウェイへ通知する (失敗しても次回に再試行する) */
            read_ret = ble_notify_temperature(temp_x10, humidity_x10);
            if (read_ret != EXIT_SUCCESS) {
                LOG_ERR("Failed to notify the temperature (err %d)", read_ret);
            }
        }
        (void)k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    }
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
