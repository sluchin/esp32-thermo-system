/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_BLE_UUID_H
#define THERMO_BLE_UUID_H

/**
 * @file
 * @brief thermo-node と thermo-gateway で共有する BLE の UUID と, 温度と湿度のデータの形式
 *
 * ノードは Thermo サービス (GATT) を持ち, その中の温度の特性で温度を配信する.
 * ゲートウェイはスキャン応答に Thermo サービスの UUID を持つノードに接続して,
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
 * 値は, 温度 (int16. 10 分の 1 ℃ の単位. 例: 23.5 ℃ は 235) と, 湿度 (uint16. 10 分の 1 % の
 * 単位. 例: 45.0 % は 450) を, それぞれ 2 byte のリトルエンディアンで, この順に並べたもの.
 */
#define THERMO_TEMPERATURE_SIZE 4U

/** 湿度を測れない (センサに湿度がない) ことを示す値 (湿度の uint16 の最大値) */
#define THERMO_HUMIDITY_NONE 0xFFFFU

#endif /* THERMO_BLE_UUID_H */
