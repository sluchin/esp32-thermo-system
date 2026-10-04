/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief MQTT のトピックと、ペイロード (JSON) の生成
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

#include "payload.h"

/** アドレスの文字列 ("00:AA:01:00:00:42") の最大長 (NUL を含む) */
#define ADDR_STR_SIZE 18u

/**
 * アドレスを、"00:AA:01:00:00:42" の形式の文字列にする
 *
 * bt_addr_le_to_str() は、末尾に " (public)" などが付くので、使わない.
 *
 * @param[out] out  出力先 (ADDR_STR_SIZE 以上)
 * @param[in]  addr アドレス (val[0] が、下位の byte)
 */
static void format_addr(char *out, const bt_addr_le_t *addr)
{
    const uint8_t *v = addr->a.val;

    (void)snprintf(out, ADDR_STR_SIZE, "%02X:%02X:%02X:%02X:%02X:%02X", v[5], v[4], v[3], v[2],
                   v[1], v[0]);
}

/**
 * snprintf() の戻り値を、このモジュールの戻り値にする
 *
 * @param[in] written snprintf() の戻り値
 * @param[in] size    出力先のサイズ
 * @return written。出力が切り捨てられたら -ENOSPC (snprintf() の失敗 (負の値) も、同じ扱い)
 */
static int check_length(int written, size_t size)
{
    if ((size_t)written >= size) {
        return -ENOSPC;
    }
    return written;
}

int payload_format_topic(char *buf, size_t size, const char *client_id, const bt_addr_le_t *addr)
{
    char node[ADDR_STR_SIZE] = {0};

    format_addr(node, addr);
    return check_length(snprintf(buf, size, "thermo/%s/%s/temperature", client_id, node), size);
}

int payload_format_temperature(char *buf, size_t size, const bt_addr_le_t *addr, uint16_t raw,
                               uint32_t uptime_ms)
{
    char node[ADDR_STR_SIZE] = {0};

    format_addr(node, addr);
    return check_length(snprintf(buf, size,
                                 "{\"node\":\"%s\",\"raw\":%u,\"uptime_ms\":%" PRIu32 "}", node,
                                 (unsigned int)raw, uptime_ms),
                        size);
}
