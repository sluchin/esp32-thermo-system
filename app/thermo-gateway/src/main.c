/**
 * @file main.c
 * @brief thermo-gateway のエントリポイント
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>
#include <stdlib.h>

#include "ble.h"

LOG_MODULE_REGISTER(thermo_gateway);

/** 稼働状況をログ出力する間隔 [s] */
#define STATUS_INTERVAL_S 10

/**
 * @brief ゲートウェイのメイン関数。
 *
 * BLE を初期化して周辺ノードのスキャンを開始し、その後は定期的に稼働状況を出力する。
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

	/* 周辺ノードのスキャンを開始する */
	ret = ble_scan();
	if (ret != EXIT_SUCCESS) {
		LOG_ERR("Failed to start BLE scan");
		return EXIT_FAILURE;
	}

	/* 受信処理はコールバック側で行うため、ここでは定期的に稼働状況を出力するだけ */
	while (true) {
		LOG_INF("Gateway scanning for nodes...");
		k_sleep(K_SECONDS(STATUS_INTERVAL_S));
	}

	return EXIT_SUCCESS;
}
