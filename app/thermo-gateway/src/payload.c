/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief MQTT のトピックと, ペイロード (JSON) の生成
 */

#include <errno.h>    /* ENOSPC */
#include <inttypes.h> /* PRId64 PRIu32 */
#include <stdio.h>    /* snprintf */

#include "payload.h"

/** アドレスの文字列 ("00:AA:01:00:00:42") の最大長 (NUL を含む) */
#define ADDR_STR_SIZE        18U
/** UNIX 時刻の項目 (",\"timestamp\":9223372036854775807") の最大長 (NUL を含む) */
#define TIMESTAMP_FIELD_SIZE 33U
/** 電池残量の項目 (",\"battery\":100") の最大長 (NUL を含む) */
#define BATTERY_FIELD_SIZE   16U

static void format_addr(char *out, const bt_addr_le_t *addr);
static int check_length(int written, size_t size);
static void format_timestamp(char *field, size_t size, int64_t unix_s);

/**
 * @brief トピックを生成する
 *
 * 形式は `thermo/<クライアント ID>/<ノードのアドレス>/temperature`
 * (例: `thermo/gateway-01/00:AA:01:00:00:42/temperature`).
 *
 * @param[out] buf       出力先 (NUL で終わる)
 * @param[in]  size      buf のサイズ
 * @param[in]  client_id ゲートウェイのクライアント ID
 * @param[in]  addr      ノードのアドレス
 * @return 文字列の長さ (NUL を除く). buf が小さければ -ENOSPC
 */
int payload_format_topic(char *buf, size_t size, const char *client_id, const bt_addr_le_t *addr)
{
    char node[ADDR_STR_SIZE] = {0}; /* ノードのアドレスの文字列 */

    format_addr(node, addr);

    return check_length(snprintf(buf, size, "thermo/%s/%s/temperature", client_id, node), size);
}

/**
 * @brief ペイロード (JSON) を生成する
 *
 * 形式は `{"node":"<アドレス>","raw":<ADC の生値>,"uptime_ms":<稼働時間 [ms]>,"timestamp":<UNIX
 * 時刻 [s]>}`. UNIX 時刻がわからないとき (unix_s が負) は, "timestamp" を出力しない. 温度 (℃)
 * への変換はしていない (TODO.md を参照).
 *
 * @param[out] buf       出力先 (NUL で終わる)
 * @param[in]  size      buf のサイズ
 * @param[in]  addr      ノードのアドレス
 * @param[in]  raw       温度 (ADC の生値)
 * @param[in]  uptime_ms 温度を受信したときのゲートウェイの稼働時間 [ms]
 * @param[in]  unix_s    温度を受信したときの UNIX 時刻 [s] (負なら, わからない)
 * @return 文字列の長さ (NUL を除く). buf が小さければ -ENOSPC
 */
int payload_format_temperature(char *buf, size_t size, const bt_addr_le_t *addr, uint16_t raw,
                               uint32_t uptime_ms, int64_t unix_s)
{
    char node[ADDR_STR_SIZE] = {0};             /* ノードのアドレスの文字列 */
    char timestamp[TIMESTAMP_FIELD_SIZE] = {0}; /* 時刻の項目 */

    format_addr(node, addr);
    format_timestamp(timestamp, sizeof(timestamp), unix_s);
    return check_length(snprintf(buf, size,
                                 "{\"node\":\"%s\",\"raw\":%u,\"uptime_ms\":%" PRIu32 "%s}", node,
                                 (unsigned int)raw, uptime_ms, timestamp),
                        size);
}

/**
 * @brief SwitchBot のトピックを生成する
 *
 * 形式は `thermo/<クライアント ID>/switchbot/<機器のアドレス>`
 * (例: `thermo/gateway-01/switchbot/B0:E9:FE:12:34:56`).
 *
 * @param[out] buf       出力先 (NUL で終わる)
 * @param[in]  size      buf のサイズ
 * @param[in]  client_id ゲートウェイのクライアント ID
 * @param[in]  addr      機器のアドレス
 * @return 文字列の長さ (NUL を除く). buf が小さければ -ENOSPC
 */
int payload_format_switchbot_topic(char *buf, size_t size, const char *client_id,
                                   const bt_addr_le_t *addr)
{
    char node[ADDR_STR_SIZE] = {0}; /* ノードのアドレスの文字列 */

    format_addr(node, addr);

    return check_length(snprintf(buf, size, "thermo/%s/switchbot/%s", client_id, node), size);
}

/**
 * @brief SwitchBot のペイロード (JSON) を生成する
 *
 * 形式は `{"node":"<アドレス>","type":"switchbot","temperature_c":<℃>,"humidity":<%>,
 * "battery":<%>,"uptime_ms":<稼働時間 [ms]>,"timestamp":<UNIX 時刻 [s]>}`. 電池残量が
 * わからないときは "battery" を, UNIX 時刻がわからないとき (unix_s が負) は "timestamp" を,
 * 出力しない. 温度は 10 分の 1 ℃ の桁まで (例: -3.5, 23.4).
 *
 * @param[out] buf       出力先 (NUL で終わる)
 * @param[in]  size      buf のサイズ
 * @param[in]  addr      機器のアドレス
 * @param[in]  sample    温度, 湿度, 電池残量
 * @param[in]  uptime_ms 受信したときのゲートウェイの稼働時間 [ms]
 * @param[in]  unix_s    受信したときの UNIX 時刻 [s] (負なら, わからない)
 * @return 文字列の長さ (NUL を除く). buf が小さければ -ENOSPC
 */
int payload_format_switchbot(char *buf, size_t size, const bt_addr_le_t *addr,
                             const struct switchbot_sample *sample, uint32_t uptime_ms,
                             int64_t unix_s)
{
    char node[ADDR_STR_SIZE] = {0};             /* ノードのアドレスの文字列 */
    char battery[BATTERY_FIELD_SIZE] = {0};     /* 電池残量の項目 */
    char timestamp[TIMESTAMP_FIELD_SIZE] = {0}; /* 時刻の項目 */
    int temp = sample->temp_x10;                /* 温度 [℃ の 10 倍] */
    /* 0 ℃ 未満は整数部が 0 でも (-0.5 など) 符号を出すため, 符号を別に出力する */
    const char *sign = ((temp < 0) ? "-" : "");                         /* 符号 */
    unsigned int magnitude = (unsigned int)((temp < 0) ? -temp : temp); /* 絶対値 */

    format_addr(node, addr);
    format_timestamp(timestamp, sizeof(timestamp), unix_s);
    if (sample->battery >= 0) {
        (void)snprintf(battery, sizeof(battery), ",\"battery\":%d", (int)sample->battery);
    }
    return check_length(snprintf(buf, size,
                                 "{\"node\":\"%s\",\"type\":\"switchbot\","
                                 "\"temperature_c\":%s%u.%u,\"humidity\":%u%s,"
                                 "\"uptime_ms\":%" PRIu32 "%s}",
                                 node, sign, magnitude / 10U, magnitude % 10U,
                                 (unsigned int)sample->humidity, battery, uptime_ms, timestamp),
                        size);
}

/**
 * アドレスを"00:AA:01:00:00:42" の形式の文字列にする
 *
 * bt_addr_le_to_str() は末尾に " (public)" などが付くので, 使わない.
 *
 * @param[out] out  出力先 (ADDR_STR_SIZE 以上)
 * @param[in]  addr アドレス (val[0] が下位の byte)
 */
static void format_addr(char *out, const bt_addr_le_t *addr)
{
    const uint8_t *v = addr->a.val; /* アドレスのバイト列 */

    (void)snprintf(out, ADDR_STR_SIZE, "%02X:%02X:%02X:%02X:%02X:%02X", v[5], v[4], v[3], v[2],
                   v[1], v[0]);
}

/**
 * snprintf() の戻り値をこのモジュールの戻り値にする
 *
 * @param[in] written snprintf() の戻り値
 * @param[in] size    出力先のサイズ
 * @return written. 出力が切り捨てられたら -ENOSPC (snprintf() の失敗 (負の値) も同じ扱い)
 */
static int check_length(int written, size_t size)
{
    if ((size_t)written >= size) {
        return -ENOSPC;
    }

    return written;
}

/**
 * UNIX 時刻の項目 (",\"timestamp\":<秒>") を作る
 *
 * @param[out] field  出力先 (NUL で終わる. unix_s が負なら, 空の文字列)
 * @param[in]  size   field のサイズ
 * @param[in]  unix_s UNIX 時刻 [s] (負なら, わからない)
 */
static void format_timestamp(char *field, size_t size, int64_t unix_s)
{
    field[0] = '\0';
    if (unix_s >= 0) {
        (void)snprintf(field, size, ",\"timestamp\":%" PRId64, unix_s);
    }
}
