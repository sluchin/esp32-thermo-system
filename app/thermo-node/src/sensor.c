#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <stdbool.h>
#include <stdlib.h>

#include "sensor.h"

#if !defined(CONFIG_SIMULATOR) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#include <zephyr/drivers/adc.h>
#define HAVE_ADC 1
#endif

LOG_MODULE_REGISTER(sensor_thermo_node);

#define ADC_RESOLUTION_BITS 12U
#define ADC_RAW_RANGE       4096U

#ifdef HAVE_ADC
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#endif

int sensor_init(void)
{
#ifdef HAVE_ADC
	int err = EXIT_SUCCESS;
	bool ready = false;

	ready = adc_is_ready_dt(&adc_channel);
	if (ready == false) {
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
	LOG_WRN("ADC not configured in device tree, using simulated values");
#endif
	return EXIT_SUCCESS;
}

int sensor_read_temperature(uint16_t *value)
{
#ifdef HAVE_ADC
	int err = EXIT_SUCCESS;

	struct adc_sequence sequence = {
		.buffer = value,
		.buffer_size = sizeof(*value),
		.channels = BIT(adc_channel.channel_id),
		.resolution = ADC_RESOLUTION_BITS,
	};

	err = adc_read_dt(&adc_channel, &sequence);
	if (err < 0) {
		LOG_ERR("Could not read ADC (%d)", err);
		return err;
	}
#else
	*value = (uint16_t)(sys_rand32_get() % ADC_RAW_RANGE);
#endif

	return EXIT_SUCCESS;
}
