/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_COMMON_CLOUD_H
#define THERMO_COMMON_CLOUD_H

/**
 * @file
 * @brief AWS IoT Core への MQTT (TLS) による温度の送信
 *
 * 専用のスレッドが WiFi と MQTT の接続を保ち (切れたら, 間隔をあけて, つなぎ直す),
 * 受信した温度を順に publish する. 設定 (cfg.h) が揃うまでは, 待つ. ゲートウェイは, 起動のときから,
 * 動かし続ける. ノードは, ゲートウェイにつながらないときだけ, cloud_set_enabled() で, 動かして,
 * 止めて, WiFi を切る (接続の失敗が, 決まった回数 (cloud_set_attempt_limit()) 続いたら,
 * あきらめる).
 */

#include <zephyr/bluetooth/addr.h>
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t int64_t */

#include "switchbot_sample.h"

int cloud_init(bool start_enabled);

int cloud_publish_temperature(const bt_addr_le_t *addr, int16_t temp_x10, uint16_t humidity_x10);

int cloud_publish_temperature_at(const bt_addr_le_t *addr, int16_t temp_x10, uint16_t humidity_x10,
                                 int64_t unix_s);

int cloud_publish_switchbot(const bt_addr_le_t *addr, const struct switchbot_sample *sample);

void cloud_set_enabled(bool enabled);

void cloud_set_attempt_limit(unsigned int max_attempts);

bool cloud_has_given_up(void);

bool cloud_is_connected(void);

unsigned int cloud_pending(void);

uint32_t cloud_sent_count(void);

void cloud_reconnect(void);

int cloud_step(void);

void cloud_stop(void);

#endif /* THERMO_COMMON_CLOUD_H */
