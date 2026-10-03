#ifndef THERMO_GATEWAY_BLE_H
#define THERMO_GATEWAY_BLE_H

/* Bluetooth スタックを初期化する。 */
int ble_init(void);
/* 周辺ノードのスキャンを開始する。 */
int ble_scan(void);

#endif /* THERMO_GATEWAY_BLE_H */
