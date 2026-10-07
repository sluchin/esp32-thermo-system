/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BLE_H
#define THERMO_NODE_BLE_H

/**
 * @file
 * @brief thermo-node の BLE 制御 (アドバタイズと, GATT での温度の配信)
 */

#include <stdint.h> /* int16_t uint16_t */

int ble_init(void);

int ble_advertise(void);

int ble_notify_temperature(int16_t temp_x10, uint16_t humidity_x10);

#endif /* THERMO_NODE_BLE_H */
