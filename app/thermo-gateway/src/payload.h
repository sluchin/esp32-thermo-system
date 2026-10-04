/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_GATEWAY_PAYLOAD_H
#define THERMO_GATEWAY_PAYLOAD_H

/**
 * @file
 * @brief MQTT のトピックと、ペイロード (JSON) の生成
 */

#include <zephyr/bluetooth/addr.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief トピックを生成する。
 *
 * 形式は `thermo/<クライアント ID>/<ノードのアドレス>/temperature`
 * (例: `thermo/gateway-01/00:AA:01:00:00:42/temperature`)。
 *
 * @param[out] buf       出力先 (NUL で終わる)
 * @param[in]  size      buf のサイズ
 * @param[in]  client_id ゲートウェイのクライアント ID
 * @param[in]  addr      ノードのアドレス
 * @return 文字列の長さ (NUL を除く)。buf が小さければ -ENOSPC
 */
int payload_format_topic(char *buf, size_t size, const char *client_id, const bt_addr_le_t *addr);

/**
 * @brief ペイロード (JSON) を生成する。
 *
 * 形式は `{"node":"<アドレス>","raw":<ADC の生値>,"uptime_ms":<稼働時間 [ms]>}`。
 * 温度 (℃) への変換は、していない (TODO.md を参照)。
 *
 * @param[out] buf       出力先 (NUL で終わる)
 * @param[in]  size      buf のサイズ
 * @param[in]  addr      ノードのアドレス
 * @param[in]  raw       温度 (ADC の生値)
 * @param[in]  uptime_ms 温度を受信したときの、ゲートウェイの稼働時間 [ms]
 * @return 文字列の長さ (NUL を除く)。buf が小さければ -ENOSPC
 */
int payload_format_temperature(char *buf, size_t size, const bt_addr_le_t *addr, uint16_t raw,
                               uint32_t uptime_ms);

#endif /* THERMO_GATEWAY_PAYLOAD_H */
