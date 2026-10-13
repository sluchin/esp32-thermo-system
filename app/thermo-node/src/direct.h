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
 * 測定値 (5 秒ごと) は全て SD カードに保存する (backlog.h. 測った UNIX 時刻と, 送信済み
 * フラグつき). ゲートウェイ (BLE) につながっている間の測定値はゲートウェイが AWS IoT Core へ
 * 送るので, 送信済みとして保存する. BLE がゲートウェイにつながっていない測定が
 * DIRECT_BLE_FAIL_LIMIT 回続いたら, WiFi と MQTT (cloud.h) を動かして, 未送信の記録を
 * 古い順に送る (測った時刻をそのまま payload に入れる). BLE にも WiFi にもつながっていない間も,
 * 保存し続ける (最大 CONFIG_THERMO_BACKLOG_MAX_RECORDS 件. 超えたら, 古い記録から上書きする).
 * ゲートウェイにつながったときに, 未送信の記録があれば, WiFi と MQTT で送って, 送り終えたら,
 * WiFi を切る. WiFi か MQTT の接続が DIRECT_ATTEMPT_LIMIT 回続けて失敗したら, あきらめて,
 * ゲートウェイにつながるまで, やり直さない.
 *
 * SD カードや RTC の時刻がなくて保存できないときは WiFi と MQTT を動かしていて, MQTT に
 * つながっていれば, 測定値をそのまま送る (保存しない. ゲートウェイにつながったら, 止める).
 *
 * 使わないときは CONFIG_THERMO_DIRECT_MQTT=n でビルドする (main.c はこのファイルの関数を
 * 呼ばない). SD カードを使わないときは CONFIG_THERMO_BACKLOG=n (溜めない).
 */

#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t */

/** BLE がゲートウェイにつながっていない測定がこの回数続いたら, 直接の送信を始める */
#define DIRECT_BLE_FAIL_LIMIT 5U
/** WiFi か MQTT の接続がこの回数続けて失敗したら, あきらめる */
#define DIRECT_ATTEMPT_LIMIT  5U

int direct_init(void);

void direct_sample(int16_t temp_x10, uint16_t humidity_x10);

bool direct_active(void);

int direct_sd_check(unsigned int *pending);

int direct_init_status(void);

#endif /* THERMO_NODE_DIRECT_H */
