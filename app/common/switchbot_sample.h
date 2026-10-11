/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_COMMON_SWITCHBOT_SAMPLE_H
#define THERMO_COMMON_SWITCHBOT_SAMPLE_H

/**
 * @file
 * @brief SwitchBot 屋外用温湿度計の, 送信する値 (cloud.h と payload.h が使う)
 *
 * アドバタイズの解析は, thermo-gateway の switchbot.h にある (thermo-node は, 使わない).
 */

#include <stdint.h> /* uint8_t int16_t int8_t */

/** 電池残量がわからないことを示す値 */
#define SWITCHBOT_BATTERY_UNKNOWN (-1)

/** 送信する温湿度計の値 */
struct switchbot_sample {
    int16_t temp_x10; /**< 温度 [℃ の 10 倍] */
    uint8_t humidity; /**< 湿度 [%] */
    int8_t battery;   /**< 電池残量 [%]. わからなければ SWITCHBOT_BATTERY_UNKNOWN */
};

#endif /* THERMO_COMMON_SWITCHBOT_SAMPLE_H */
