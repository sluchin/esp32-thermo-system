#ifndef THERMO_NODE_BLE_H
#define THERMO_NODE_BLE_H

/* Bluetooth スタックを初期化する。 */
int ble_init(void);
/* アドバタイズを開始する。 */
int ble_advertise(void);

#endif /* THERMO_NODE_BLE_H */
