/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_GATEWAY_CLOUD_H
#define THERMO_GATEWAY_CLOUD_H

/**
 * @file
 * @brief AWS IoT Core への、MQTT (TLS) による温度の送信
 *
 * 専用のスレッドが、WiFi と MQTT の接続を保ち (切れたら、間隔をあけて、つなぎ直す)、
 * 受信した温度を、順に publish する。設定 (cfg.h) が揃うまでは、待つ。
 */

#include <zephyr/bluetooth/addr.h>
#include <stdint.h> /* uint16_t */

#include "switchbot.h"

/**
 * @brief 設定を読み込んで、WiFi と MQTT のスレッドを開始する。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int cloud_init(void);

/**
 * @brief 温度を、送信のキューに入れる (待たずに、すぐ戻る)。
 *
 * Bluetooth のスレッドから呼べる。キューが満杯のときは、新しい温度を捨てる。
 *
 * @param[in] addr 温度を送ったノードのアドレス
 * @param[in] raw  温度 (ADC の生値)
 * @retval EXIT_SUCCESS 成功
 * @retval -ENOMSG      キューが満杯で、温度を捨てた
 */
int cloud_publish_temperature(const bt_addr_le_t *addr, uint16_t raw);

/**
 * @brief SwitchBot の温湿度計の値を、送信のキューに入れる (待たずに、すぐ戻る)。
 *
 * トピックは `thermo/<クライアント ID>/switchbot/<機器のアドレス>`。Bluetooth のスレッドから
 * 呼べる。キューが満杯のときは、新しい値を捨てる。
 *
 * @param[in] addr   機器のアドレス
 * @param[in] sample 温度、湿度、電池残量
 * @retval EXIT_SUCCESS 成功
 * @retval -ENOMSG      キューが満杯で、値を捨てた
 */
int cloud_publish_switchbot(const bt_addr_le_t *addr, const struct switchbot_sample *sample);

/**
 * @brief 設定を変えたあとに、接続をやり直させる。
 *
 * 今の MQTT の接続を閉じて、新しい設定で、つなぎ直す。
 */
void cloud_reconnect(void);

/**
 * @brief 接続と送信を 1 回分だけ行う (スレッドが、繰り返し呼ぶ)。
 *
 * 設定が揃うまで待ち、WiFi と MQTT に接続して、接続が切れるまで、キューの温度を publish する。
 * 接続に失敗したときは、間隔をあけてから (待つ時間は、失敗のたびに倍にする)、戻る。
 *
 * @retval EXIT_SUCCESS 接続して、設定の変更 (cloud_reconnect()) で、正常に終わった
 * @retval -EAGAIN      設定が揃っていない
 * @retval negative     接続の失敗、または、接続が切れた理由 (負の errno)
 */
int cloud_step(void);

/**
 * @brief WiFi と MQTT のスレッドを止める (MQTT は、切断しない)。
 *
 * 通常は呼ばない (ゲートウェイは、電源を切るまで動く)。単体テストが、スレッドを残さないために使う。
 */
void cloud_stop(void);

#endif /* THERMO_GATEWAY_CLOUD_H */
