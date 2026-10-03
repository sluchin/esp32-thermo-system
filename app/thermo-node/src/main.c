#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "ble.h"
#include "sensor.h"

LOG_MODULE_REGISTER(thermo_node);

/* 温度を測定する間隔 [s] */
#define SAMPLE_INTERVAL_S 5

int main(void)
{
	int ret = EXIT_SUCCESS;

/* ビルド構成に応じて起動ログを切り替える */
#ifdef CONFIG_SIMULATOR
	LOG_INF("Thermo Node started (SIMULATOR MODE)");
#else
	LOG_INF("Thermo Node started on ESP32C3");
#endif

	/* センサ (ADC) を初期化する */
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

	/* 一定間隔で温度を読み取ってログ出力する */
	while (true) {
		uint16_t temp_raw = 0U;
		int read_ret = EXIT_SUCCESS;

		read_ret = sensor_read_temperature(&temp_raw);
		/* 読み取りに失敗した場合は今回の値を捨てて次回に再試行する */
		if (read_ret == EXIT_SUCCESS) {
			LOG_INF("Temperature: %u (raw ADC value)", temp_raw);
		}
		k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
	}

	return EXIT_SUCCESS;
}
