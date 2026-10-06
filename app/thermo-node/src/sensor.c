/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の温度センサ (ADC) 実装
 */

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <stdlib.h> /* EXIT_SUCCESS */

#include "sensor.h"

/* シミュレータでなく, Devicetree に ADC チャンネルがある場合のみ実機の ADC を使用する */
#if !defined(CONFIG_SIMULATOR) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#include <zephyr/drivers/adc.h>
/** 実機の ADC を使うビルドであることを示す (ADC のコードを有効にする) */
#define HAVE_ADC 1
#endif

LOG_MODULE_REGISTER(sensor_thermo_node);

#ifndef HAVE_ADC
/** 12 bit ADC の生値の取り得る範囲 (0 .. 4095). シミュレーション値の生成に使用する */
#define ADC_RAW_RANGE 4096U
#else
/** ADC の分解能 [bit] */
#define ADC_RESOLUTION_BITS 12U

/** Devicetree (zephyr,user の io-channels) から取得した ADC チャンネル仕様 */
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#endif

/**
 * @brief センサを初期化する
 *
 * Devicetree に ADC チャンネルが無い場合 (シミュレータ等) は警告をログ出力するのみで成功とする.
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      ADC コントローラが利用可能でない
 * @retval negative     ADC チャンネル設定の失敗 (負の errno)
 */
int sensor_init(void)
{
#ifdef HAVE_ADC
    int err = EXIT_SUCCESS; /* エラーコード */

    /* ADC のドライバが初期化されていること */
    if (!adc_is_ready_dt(&adc_channel)) {
        LOG_ERR("ADC controller not ready");
        return -ENODEV;
    }

    /* Devicetree のチャンネルの設定 (ゲイン, 基準電圧, 分解能など) を ADC に反映する */
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

/**
 * @brief 温度の生値 (ADC カウント) を読み取る
 *
 * ADC 未設定の場合は乱数によるシミュレーション値を返す.
 *
 * @param[out] value 読み取った生値の格納先 (NULL 不可)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     ADC 読み取りの失敗 (負の errno)
 */
int sensor_read_temperature(uint16_t *value)
{
#ifdef HAVE_ADC
    int err = EXIT_SUCCESS; /* エラーコード */

    /* 1 回だけ, 1 チャンネルを読む (生値をそのまま value に書き込む) */
    struct adc_sequence sequence = {
        .buffer = value,
        .buffer_size = sizeof(*value),
        .channels = (uint32_t)BIT(adc_channel.channel_id),
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
