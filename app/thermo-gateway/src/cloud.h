/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_GATEWAY_CLOUD_H
#define THERMO_GATEWAY_CLOUD_H

/**
 * @file
 * @brief AWS IoT Core への MQTT (TLS) による温度の送信
 *
 * 専用のスレッドが WiFi と MQTT の接続を保ち (切れたら, 間隔をあけて, つなぎ直す),
 * 受信した温度を順に publish する. 設定 (cfg.h) が揃うまでは, 待つ.
 */

#include <zephyr/bluetooth/addr.h>
#include <stdint.h> /* uint16_t */

#include "switchbot.h"

int cloud_init(void);

int cloud_publish_temperature(const bt_addr_le_t *addr, uint16_t raw);

int cloud_publish_switchbot(const bt_addr_le_t *addr, const struct switchbot_sample *sample);

void cloud_reconnect(void);

int cloud_step(void);

void cloud_stop(void);

#endif /* THERMO_GATEWAY_CLOUD_H */
