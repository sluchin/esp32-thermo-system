#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(thermo_gateway);

int main(void)
{
	LOG_INF("Thermo Gateway started on ESP32C3");

	while (1) {
		LOG_INF("Gateway scanning for nodes...");
		k_sleep(K_SECONDS(10));
	}

	return 0;
}
