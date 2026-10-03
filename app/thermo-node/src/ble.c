#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>

#include "ble.h"

LOG_MODULE_REGISTER(ble_thermo_node);

/* アドバタイズデータ: フラグ (LE 一般発見可能、BR/EDR 非対応) とデバイス名 */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Bluetooth スタックを初期化する。成功時は EXIT_SUCCESS、失敗時は負の errno を返す。 */
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

/* 接続可能なアドバタイズを開始する。成功時は EXIT_SUCCESS、失敗時は負の errno を返す。 */
int ble_advertise(void)
{
	int err = EXIT_SUCCESS;

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err != 0) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return err;
	}

	LOG_INF("Advertising started");
	return EXIT_SUCCESS;
}
