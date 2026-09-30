#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(thermo_node);

int main(void)
{
	LOG_INF("Thermo Node started on ESP32C3");

	while (1) {
		LOG_INF("Running...");
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
