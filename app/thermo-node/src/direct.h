/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_DIRECT_H
#define THERMO_NODE_DIRECT_H

/**
 * @file
 * @brief ゲートウェイにつながらないときの, WiFi での AWS IoT Core への直接の送信 (MQTT) と,
 * SD カードへの保存, あとからの送信
 *
 * BLE がゲートウェイにつながっていない測定が, DIRECT_BLE_FAIL_LIMIT 回続いたら, WiFi と MQTT
 * (cloud.h) を動かして, 測定値を, DIRECT_PUBLISH_EVERY 回の測定ごと (10 秒) に送る.
 * ゲートウェイにつながったら, 止めて, WiFi を切る (ゲートウェイが送るので, 重複させない).
 * WiFi か MQTT の接続が, DIRECT_ATTEMPT_LIMIT 回続けて失敗したら, あきらめて, ゲートウェイに
 * つながるまで, やり直さない.
 *
 * ゲートウェイにつながっていない間の測定値 (10 秒ごと) は, まだ MQTT につながっていないとき,
 * または, 先に送る記録があるときは, SD カードに溜める (backlog.h. 測った UNIX 時刻つき).
 * SD カードに送っていない記録があるときは, ゲートウェイにつながっていても, WiFi と MQTT を
 * 動かして, 古い順に送る (測った時刻を, そのまま payload に入れる). 送り終えて,
 * ゲートウェイにつながっていれば, WiFi を切る.
 *
 * 使わないときは, CONFIG_THERMO_DIRECT_MQTT=n でビルドする (main.c は, このファイルの関数を
 * 呼ばない). SD カードを使わないときは, CONFIG_THERMO_BACKLOG=n (溜めない).
 */

#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t */

/** BLE がゲートウェイにつながっていない測定が, この回数続いたら, 直接の送信を始める */
#define DIRECT_BLE_FAIL_LIMIT 5U
/** 直接の送信を始めたあと, この回数の測定ごとに, 1 回送る (測定は 5 秒ごと. 10 秒) */
#define DIRECT_PUBLISH_EVERY  2U
/** WiFi か MQTT の接続が, この回数続けて失敗したら, あきらめる */
#define DIRECT_ATTEMPT_LIMIT  5U

int direct_init(void);

void direct_sample(int16_t temp_x10, uint16_t humidity_x10);

bool direct_active(void);

#endif /* THERMO_NODE_DIRECT_H */
