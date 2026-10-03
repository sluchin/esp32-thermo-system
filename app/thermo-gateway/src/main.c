#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>
#include <stdlib.h>

#include "ble.h"

LOG_MODULE_REGISTER(thermo_gateway);

#define STATUS_INTERVAL_S 10

int main(void)
{
	int ret = EXIT_SUCCESS;

#ifdef CONFIG_SIMULATOR
	LOG_INF("Thermo Gateway started (SIMULATOR MODE)");
#else
	LOG_INF("Thermo Gateway started on ESP32C3");
#endif

	ret = ble_init();
	if (ret != EXIT_SUCCESS) {
		LOG_ERR("Failed to initialize BLE");
		return EXIT_FAILURE;
	}

	ret = ble_scan();
	if (ret != EXIT_SUCCESS) {
		LOG_ERR("Failed to start BLE scan");
		return EXIT_FAILURE;
	}

	while (true) {
		LOG_INF("Gateway scanning for nodes...");
		k_sleep(K_SECONDS(STATUS_INTERVAL_S));
	}

	return EXIT_SUCCESS;
}
