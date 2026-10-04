/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-gateway のエントリポイント
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>
#include <stdlib.h>

#include "ble.h"
#ifdef CONFIG_THERMO_CLOUD
#include "cloud.h"
#endif

LOG_MODULE_REGISTER(thermo_gateway);

/** 稼働状況をログ出力する間隔 [s] */
#define STATUS_INTERVAL_S 10

/**
 * 温度を受信したときのコールバック (ログに出力して、AWS IoT Core への送信のキューに入れる)
 *
 * Bluetooth のスレッドから呼ばれる.
 *
 * @param[in] addr 温度を送ったノードのアドレス
 * @param[in] raw 温度 (ADC の生値)
 */
static void on_temperature(const bt_addr_le_t *addr, uint16_t raw)
{
    char addr_str[BT_ADDR_LE_STR_LEN] = {0};
#ifdef CONFIG_THERMO_CLOUD
    int ret = EXIT_SUCCESS;
#endif

    (void)bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    LOG_INF("Temperature from %s: %u (raw ADC value)", addr_str, raw);

#ifdef CONFIG_THERMO_CLOUD
    ret = cloud_publish_temperature(addr, raw);
    if (ret != EXIT_SUCCESS) {
        LOG_WRN("The temperature was not queued for AWS IoT Core (err %d)", ret);
    }
#endif
}

/**
 * SwitchBot の温湿度計のアドバタイズを受信したときのコールバック
 * (間引いて、ログに出力して、AWS IoT Core への送信のキューに入れる)
 *
 * Bluetooth のスレッドから呼ばれる.
 *
 * @param[in] addr 機器のアドレス
 * @param[in] ad 解析した結果 (機種と電池残量、または、温度と湿度)
 */
static void on_switchbot(const bt_addr_le_t *addr, const struct switchbot_ad *ad)
{
    char addr_str[BT_ADDR_LE_STR_LEN] = {0};
    struct switchbot_sample sample = {0};
#ifdef CONFIG_THERMO_CLOUD
    int ret = EXIT_SUCCESS;
#endif

    /* 送信の間隔 (CONFIG_THERMO_SWITCHBOT_INTERVAL_MS) より短い間の値は、捨てる */
    if (!switchbot_accept(addr, ad, k_uptime_get_32(), &sample)) {
        return;
    }

    (void)bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    LOG_INF("SwitchBot %s: %d.%u C, %u %%, battery %d %%", addr_str, sample.temp_x10 / 10,
            (unsigned int)((sample.temp_x10 < 0) ? -(sample.temp_x10 % 10)
                                                 : (sample.temp_x10 % 10)),
            sample.humidity, sample.battery);

#ifdef CONFIG_THERMO_CLOUD
    ret = cloud_publish_switchbot(addr, &sample);
    if (ret != EXIT_SUCCESS) {
        LOG_WRN("The SwitchBot value was not queued for AWS IoT Core (err %d)", ret);
    }
#endif
}

/**
 * @brief ゲートウェイのメイン関数。
 *
 * BLE を初期化して、(CONFIG_THERMO_CLOUD が有効なら) AWS IoT Core への送信を開始して、
 * 周辺ノードのスキャンを開始し (見つけたノードには、接続して、温度の通知を購読する。
 * SwitchBot の温湿度計は、接続せずに、アドバタイズの値を受け取る)、
 * その後は定期的に稼働状況を出力する。
 *
 * @retval EXIT_FAILURE 初期化またはスキャン開始に失敗した場合
 *                      (正常時はループから戻らない)
 */
int main(void)
{
    int ret = EXIT_SUCCESS;

/* ビルド構成に応じて起動ログを切り替える */
#ifdef CONFIG_SIMULATOR
    LOG_INF("Thermo Gateway started (SIMULATOR MODE)");
#else
    LOG_INF("Thermo Gateway started on ESP32C3");
#endif

    /* BLE を初期化する */
    ret = ble_init();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to initialize BLE");
        return EXIT_FAILURE;
    }

#ifdef CONFIG_THERMO_CLOUD
    /* 設定を読み込んで、AWS IoT Core への送信のスレッドを開始する */
    ret = cloud_init();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to initialize the cloud connection");
        return EXIT_FAILURE;
    }
#endif

    /* 温度を受信したら、ログに出力する (クラウドが有効なら、送信のキューにも入れる) */
    ble_set_temperature_callback(on_temperature);

    /* SwitchBot の温湿度計 (接続しない) の値も、同じ流れで扱う */
    ble_set_switchbot_callback(on_switchbot);

    /* 周辺ノードのスキャンを開始する */
    ret = ble_scan();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to start BLE scan");
        return EXIT_FAILURE;
    }

    /* 受信処理はコールバック側で行うため、ここでは定期的に稼働状況を出力するだけ */
    while (true) {
        LOG_INF("Gateway scanning for nodes...");
        (void)k_sleep(K_SECONDS(STATUS_INTERVAL_S));
    }
}
