#ifndef THERMO_NODE_SENSOR_H
#define THERMO_NODE_SENSOR_H

#include <stdint.h>

int sensor_init(void);
int sensor_read_temperature(uint16_t *value);

#endif /* THERMO_NODE_SENSOR_H */
