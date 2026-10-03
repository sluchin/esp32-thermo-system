#ifndef THERMO_GATEWAY_BLE_H
#define THERMO_GATEWAY_BLE_H

/**
 * @file ble.h
 * @brief thermo-gateway の BLE 制御 (スキャン)
 */

/**
 * @brief Bluetooth スタックを初期化する。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_init(void);

/**
 * @brief 周辺ノードのアクティブスキャンを開始する。
 *
 * 事前に ble_init() を呼び出しておくこと。
 * 検出したデバイスはコールバック内でログ出力される。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_scan(void);

#endif /* THERMO_GATEWAY_BLE_H */
