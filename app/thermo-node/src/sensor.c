#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#ifdef CONFIG_SIMULATOR
#include <zephyr/kernel.h>
#else
#include <zephyr/adc.h>
#include <zephyr/devicetree.h>
#endif

LOG_MODULE_REGISTER(sensor_thermo_node);

#ifndef CONFIG_SIMULATOR
#if DT_NODE_EXISTS(DT_PATH(zephyr_user))
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static int adc_available = 1;
#else
static int adc_available = 0;
#endif
#endif

int sensor_init(void)
{
#ifndef CONFIG_SIMULATOR
	int err;

	if (!adc_available) {
		LOG_WRN("ADC not configured in device tree, using simulated mode");
		return 0;
	}

	if (!adc_is_ready_dt(&adc_channel)) {
		LOG_ERR("ADC controller not ready");
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&adc_channel);
	if (err < 0) {
		LOG_ERR("Could not setup ADC channel (%d)", err);
		return err;
	}

	LOG_INF("ADC sensor initialized");
#else
	LOG_INF("Simulated ADC sensor initialized");
#endif
	return 0;
}

int sensor_read_temperature(uint16_t *value)
{
#ifndef CONFIG_SIMULATOR
	int err;

	if (!adc_available) {
		*value = (uint16_t)(sys_rand32_get() % 4096);
		return 0;
	}

	struct adc_sequence sequence = {
		.buffer = value,
		.buffer_size = sizeof(*value),
		.channels = BIT(adc_channel.channel_id),
		.resolution = 12,
	};

	err = adc_read_dt(&adc_channel, &sequence);
	if (err < 0) {
		LOG_ERR("Could not read ADC (%d)", err);
		return err;
	}
#else
	*value = (uint16_t)(sys_rand32_get() % 4096);
#endif

	return 0;
}
