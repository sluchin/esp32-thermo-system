/**
 * @file ble.c
 * @brief thermo-gateway の BLE スキャン実装
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>

#include "ble.h"

LOG_MODULE_REGISTER(ble_thermo_gateway);

/** スキャン間隔 [ms] */
#define SCAN_INTERVAL_MS 100
/** スキャン窓 [ms] (スキャン間隔以下であること) */
#define SCAN_WINDOW_MS   50

/**
 * @brief アドバタイズ受信時のスキャンコールバック。
 *
 * 検出したデバイスのアドレスと RSSI をログ出力する。
 *
 * @param[in] addr     送信元デバイスのアドレス
 * @param[in] rssi     受信信号強度 [dBm]
 * @param[in] adv_type アドバタイズの種別 (未使用)
 * @param[in] adv_data アドバタイズデータ (未使用)
 */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *adv_data)
{
	char addr_str[BT_ADDR_LE_STR_LEN] = {0};

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_INF("Device found: %s (RSSI %d)", addr_str, rssi);
}

int ble_init(void)
{
	int err = EXIT_SUCCESS;

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	LOG_INF("Bluetooth initialized");
	return EXIT_SUCCESS;
}

int ble_scan(void)
{
	int err = EXIT_SUCCESS;
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_MS_TO_SCAN_INTERVAL(SCAN_INTERVAL_MS),
		.window = BT_GAP_MS_TO_SCAN_WINDOW(SCAN_WINDOW_MS),
	};

	err = bt_le_scan_start(&scan_param, scan_cb);
	if (err != 0) {
		LOG_ERR("Starting scan failed (err %d)", err);
		return err;
	}

	LOG_INF("BLE scan started");
	return EXIT_SUCCESS;
}
