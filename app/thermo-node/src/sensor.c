#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <stdbool.h>
#include <stdlib.h>

#include "sensor.h"

/* シミュレータでなく、Devicetree に ADC チャンネルがある場合のみ実機の ADC を使用する */
#if !defined(CONFIG_SIMULATOR) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#include <zephyr/drivers/adc.h>
#define HAVE_ADC 1
#endif

LOG_MODULE_REGISTER(sensor_thermo_node);

/* ADC の分解能 [bit] */
#define ADC_RESOLUTION_BITS 12U
/* 12 bit ADC の生値の取り得る範囲 (0 .. 4095)。シミュレーション値の生成にも使用する */
#define ADC_RAW_RANGE       4096U

#ifdef HAVE_ADC
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#endif

/*
 * センサを初期化する。
 * ADC 未設定 (シミュレータ等) の場合は警告のみで成功とする。
 * 成功時は EXIT_SUCCESS、失敗時は負の errno を返す。
 */
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

/*
 * 温度の生値 (ADC カウント) を読み取り、value に格納する。
 * ADC 未設定の場合は乱数によるシミュレーション値を返す。
 * 成功時は EXIT_SUCCESS、失敗時は負の errno を返す。
 */
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
