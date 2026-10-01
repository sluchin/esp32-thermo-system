#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(thermo_gateway);

extern int ble_init(void);
extern int ble_scan(void);

int main(void)
{
#ifdef CONFIG_SIMULATOR
	LOG_INF("Thermo Gateway started (SIMULATOR MODE)");
#else
	LOG_INF("Thermo Gateway started on ESP32C3");
#endif

	if (ble_init() != 0) {
		LOG_ERR("Failed to initialize BLE");
		return -1;
	}

	if (ble_scan() != 0) {
		LOG_ERR("Failed to start BLE scan");
		return -1;
	}

	while (1) {
		LOG_INF("Gateway scanning for nodes...");
		k_sleep(K_SECONDS(10));
	}

	return 0;
}
