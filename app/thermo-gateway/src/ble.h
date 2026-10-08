/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_GATEWAY_BLE_H
#define THERMO_GATEWAY_BLE_H

/**
 * @file
 * @brief thermo-gateway の BLE 制御 (ノードのスキャンと接続, GATT での温度の受信と時刻の書き込み)
 */

#include <zephyr/bluetooth/addr.h>
#include <stdint.h> /* int16_t uint16_t int64_t */

#include "switchbot.h"

/**
 * @brief 温度を受信したときのコールバックの型
 *
 * Bluetooth のスレッドから呼ばれるので, 時間のかかる処理は書かないこと.
 *
 * @param[in] addr         温度を送ったノードのアドレス
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍] (湿度がなければ THERMO_HUMIDITY_NONE)
 */
typedef void (*ble_temperature_cb_t)(const bt_addr_le_t *addr, int16_t temp_x10,
                                     uint16_t humidity_x10);

/**
 * @brief SwitchBot 屋外用温湿度計のアドバタイズを受信したときのコールバックの型
 *
 * Bluetooth のスレッドから呼ばれるので, 時間のかかる処理は書かないこと.
 * サービスデータ (機種, 電池残量) と, 製造者データ (温度, 湿度) は別々に届くので,
 * どちらもこのコールバックに渡す (間引きは switchbot_accept() が行う).
 *
 * @param[in] addr 機器のアドレス
 * @param[in] ad   解析した結果
 */
typedef void (*ble_switchbot_cb_t)(const bt_addr_le_t *addr, const struct switchbot_ad *ad);

/**
 * @brief ノードに書き込む時刻を得る関数の型
 *
 * システムの時計が合っているとき (SNTP で同期したあと) だけ, 時刻を返す.
 *
 * @param[out] unix_s UTC の UNIX 時刻 [s] の格納先
 *
 * @retval EXIT_SUCCESS 時刻を返した
 * @retval negative     時刻がわからない (まだ同期していない. 書き込みを見送る)
 */
typedef int (*ble_time_source_t)(int64_t *unix_s);

int ble_init(void);

void ble_set_temperature_callback(ble_temperature_cb_t cb);

void ble_set_switchbot_callback(ble_switchbot_cb_t cb);

void ble_set_time_source(ble_time_source_t source);

int ble_scan(void);

#endif /* THERMO_GATEWAY_BLE_H */
