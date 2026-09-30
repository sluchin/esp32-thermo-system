#include <zephyr/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sensor_thermo_node);

static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

int sensor_init(void)
{
	int err;

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
	return 0;
}

int sensor_read_temperature(uint16_t *value)
{
	int err;
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

	return 0;
}
