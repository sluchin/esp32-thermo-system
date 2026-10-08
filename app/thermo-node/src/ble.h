/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BLE_H
#define THERMO_NODE_BLE_H

/**
 * @file
 * @brief thermo-node の BLE 制御 (アドバタイズと, GATT での温度の配信と, 時刻の受信)
 *
 * 時刻の受信 (ble_set_time_callback() と, 時刻の特性) は, CONFIG_THERMO_RTC=y のときだけ,
 * ビルドされる.
 */

#include <stdint.h> /* int16_t uint16_t int64_t */

/**
 * @brief ゲートウェイから時刻を受け取ったときのコールバックの型
 *
 * Bluetooth のスレッドから呼ばれる.
 *
 * @param[in] unix_s UTC の UNIX 時刻 [s]
 *
 * @retval EXIT_SUCCESS 時刻を設定できた
 * @retval -ERANGE      設定できない範囲の時刻 (書き込みを拒否する)
 * @retval negative     そのほかの失敗 (負の errno. 書き込みを拒否する)
 */
typedef int (*ble_time_cb_t)(int64_t unix_s);

int ble_init(void);

void ble_set_time_callback(ble_time_cb_t cb);

int ble_advertise(void);

int ble_notify_temperature(int16_t temp_x10, uint16_t humidity_x10);

#endif /* THERMO_NODE_BLE_H */
