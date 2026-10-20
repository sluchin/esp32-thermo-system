/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_SENSOR_H
#define THERMO_NODE_SENSOR_H

/**
 * @file
 * @brief thermo-node の温湿度センサと気圧センサの制御
 *
 * センサは Devicetree で選ぶ (実機のとき).
 *  - alias thermo-sensor のセンサ (AHT20, DHT11 など): Zephyr のセンサ API で読む.
 *    alias thermo-pressure のセンサ (BMP280 など) があれば, 気圧も読む
 *  - zephyr,user の io-channels (ADC): ADC を読んで, 温度に換算する
 *  - どちらもなければ (シミュレータなど): 乱数のシミュレーション値
 */

#include <stdint.h> /* int16_t uint16_t */

int sensor_init(void);

int sensor_read(int16_t *temp_x10, uint16_t *humidity_x10, uint16_t *pressure_x10);

#endif /* THERMO_NODE_SENSOR_H */
