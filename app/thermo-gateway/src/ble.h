/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_GATEWAY_BLE_H
#define THERMO_GATEWAY_BLE_H

/**
 * @file
 * @brief thermo-gateway の BLE 制御 (ノードのスキャンと接続、GATT での温度の受信)
 */

#include <zephyr/bluetooth/addr.h>
#include <stdint.h>

/**
 * @brief 温度を受信したときのコールバックの型。
 *
 * Bluetooth のスレッドから呼ばれるので、時間のかかる処理は、書かないこと。
 *
 * @param[in] addr 温度を送ったノードのアドレス
 * @param[in] raw  温度 (ADC の生値)
 */
typedef void (*ble_temperature_cb_t)(const bt_addr_le_t *addr, uint16_t raw);

/**
 * @brief Bluetooth スタックを初期化して、接続のコールバックを登録する。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_init(void);

/**
 * @brief 温度を受信したときのコールバックを設定する。
 *
 * ble_scan() の前に設定しておくこと。NULL で、解除する。
 *
 * @param[in] cb コールバック
 */
void ble_set_temperature_callback(ble_temperature_cb_t cb);

/**
 * @brief 周辺ノードのアクティブスキャンを開始する。
 *
 * スキャン応答に Thermo サービスの UUID を持つノードを見つけたら、接続して、温度の特性の
 * 通知 (notify) を購読する。温度を受信するたびに、ble_set_temperature_callback() で設定した
 * コールバックを呼ぶ。接続できる台数は、CONFIG_BT_MAX_CONN まで。接続が切れたら、
 * スキャンを再開して、再び接続する。
 *
 * 事前に ble_init() を呼び出しておくこと。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_scan(void);

#endif /* THERMO_GATEWAY_BLE_H */
