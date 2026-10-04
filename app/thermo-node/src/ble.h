/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BLE_H
#define THERMO_NODE_BLE_H

/**
 * @file
 * @brief thermo-node の BLE 制御 (アドバタイズと、GATT での温度の配信)
 */

#include <stdint.h>

/**
 * @brief Bluetooth スタックを初期化して、Thermo サービス (GATT) を登録する。
 *
 * 接続のコールバックも登録する。接続が切れたら、アドバタイズを再開する。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_init(void);

/**
 * @brief 接続可能なアドバタイズを開始する。
 *
 * ゲートウェイから検出されるよう、デバイス名を含むアドバタイズデータと、
 * Thermo サービスの UUID を含むスキャン応答データを送信する。
 * 事前に ble_init() を呼び出しておくこと。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_advertise(void);

/**
 * @brief 温度を更新して、接続しているゲートウェイへ通知 (notify) する。
 *
 * 通知を購読しているゲートウェイがいなくても、値は更新される (読み取り (read) で取得できる)。
 * 接続しているゲートウェイがいないときは、成功を返す。
 *
 * @param[in] raw 温度 (ADC の生値)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     通知に失敗 (負の errno)
 */
int ble_notify_temperature(uint16_t raw);

#endif /* THERMO_NODE_BLE_H */
