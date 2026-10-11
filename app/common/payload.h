/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_COMMON_PAYLOAD_H
#define THERMO_COMMON_PAYLOAD_H

/**
 * @file
 * @brief MQTT のトピックとペイロード (JSON) の生成
 */

#include <zephyr/bluetooth/addr.h>
#include <stddef.h> /* size_t */
#include <stdint.h> /* int16_t uint16_t uint32_t int64_t */

#include "switchbot_sample.h"

int payload_format_topic(char *buf, size_t size, const char *client_id, const bt_addr_le_t *addr);

int payload_format_temperature(char *buf, size_t size, const bt_addr_le_t *addr, int16_t temp_x10,
                               uint16_t humidity_x10, uint32_t uptime_ms, int64_t unix_s);

int payload_format_switchbot_topic(char *buf, size_t size, const char *client_id,
                                   const bt_addr_le_t *addr);

int payload_format_switchbot(char *buf, size_t size, const bt_addr_le_t *addr,
                             const struct switchbot_sample *sample, uint32_t uptime_ms,
                             int64_t unix_s);

#endif /* THERMO_COMMON_PAYLOAD_H */
