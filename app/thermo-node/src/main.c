#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(thermo_node);

extern int ble_init(void);
extern int ble_advertise(void);
extern int sensor_init(void);
extern int sensor_read_temperature(uint16_t *value);

int main(void)
{
#ifdef CONFIG_SIMULATOR
	LOG_INF("Thermo Node started (SIMULATOR MODE)");
#else
	LOG_INF("Thermo Node started on ESP32C3");
#endif

	if (sensor_init() != 0) {
		LOG_ERR("Failed to initialize sensor");
		return -1;
	}

	if (ble_init() != 0) {
		LOG_ERR("Failed to initialize BLE");
		return -1;
	}

	if (ble_advertise() != 0) {
		LOG_ERR("Failed to start BLE advertising");
		return -1;
	}

	while (1) {
		uint16_t temp_raw;
		if (sensor_read_temperature(&temp_raw) == 0) {
			LOG_INF("Temperature: %u (raw ADC value)", temp_raw);
		}
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
