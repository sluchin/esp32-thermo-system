#ifndef THERMO_NODE_SENSOR_H
#define THERMO_NODE_SENSOR_H

#include <stdint.h>

/* センサを初期化する。成功時は EXIT_SUCCESS、失敗時は負の errno。 */
int sensor_init(void);
/* 温度の生値 (ADC カウント) を *value に読み取る。 */
int sensor_read_temperature(uint16_t *value);

#endif /* THERMO_NODE_SENSOR_H */
