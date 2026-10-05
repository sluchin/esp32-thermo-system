/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_BLE_UUID_H
#define THERMO_BLE_UUID_H

/**
 * @file
 * @brief thermo-node と thermo-gateway で共有する BLE の UUID と, 温度データの形式
 *
 * ノードは, Thermo サービス (GATT) を持ち, その中の温度の特性で, 温度を配信する.
 * ゲートウェイは, スキャン応答に Thermo サービスの UUID を持つノードに接続して,
 * 温度の特性の通知 (notify) を購読する.
 */

#include <zephyr/bluetooth/uuid.h>

/** Thermo サービスの UUID (128 bit) の値 */
#define THERMO_UUID_SERVICE_VAL                                                                    \
    BT_UUID_128_ENCODE(0x9F3C1A00, 0x7B6E, 0x4C3A, 0x9D5E, 0x2A6F0B1C8D01)

/** 温度の特性の UUID (128 bit) の値 */
#define THERMO_UUID_TEMPERATURE_VAL                                                                \
    BT_UUID_128_ENCODE(0x9F3C1A01, 0x7B6E, 0x4C3A, 0x9D5E, 0x2A6F0B1C8D01)

/** Thermo サービスの UUID */
#define THERMO_UUID_SERVICE BT_UUID_DECLARE_128(THERMO_UUID_SERVICE_VAL)

/** 温度の特性の UUID */
#define THERMO_UUID_TEMPERATURE BT_UUID_DECLARE_128(THERMO_UUID_TEMPERATURE_VAL)

/**
 * 温度の特性の値の大きさ [byte]
 *
 * 値は, ADC の生値 (0 .. 4095) を, 2 byte のリトルエンディアン (uint16) で表したもの.
 */
#define THERMO_TEMPERATURE_SIZE 2U

#endif /* THERMO_BLE_UUID_H */
