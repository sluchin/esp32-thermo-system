/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_SENSOR_H
#define THERMO_NODE_SENSOR_H

/**
 * @file
 * @brief thermo-node の温度センサ (ADC) 制御
 */

#include <stdint.h> /* uint16_t */

int sensor_init(void);

int sensor_read_temperature(uint16_t *value);

#endif /* THERMO_NODE_SENSOR_H */
