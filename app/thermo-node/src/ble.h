/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BLE_H
#define THERMO_NODE_BLE_H

/**
 * @file
 * @brief thermo-node の BLE 制御 (アドバタイズ)
 */

/**
 * @brief Bluetooth スタックを初期化する。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_init(void);

/**
 * @brief 接続可能なアドバタイズを開始する。
 *
 * ゲートウェイから検出されるよう、デバイス名を含むアドバタイズデータを送信する。
 * 事前に ble_init() を呼び出しておくこと。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_advertise(void);

#endif /* THERMO_NODE_BLE_H */
