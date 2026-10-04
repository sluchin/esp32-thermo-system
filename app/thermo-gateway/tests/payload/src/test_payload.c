/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief payload.c の単体テスト
 *
 * トピックと JSON のペイロードの形式と、バッファが小さいときの -ENOSPC を確認する.
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "payload.h"

/** テストのノードのアドレス (表示は、00:AA:01:00:00:42。val[0] が下位の byte) */
static const bt_addr_le_t node = {
        .type = BT_ADDR_LE_PUBLIC,
        .a = {.val = {0x42, 0x00, 0x00, 0x01, 0xAA, 0x00}},
};

/** 期待するトピック */
#define EXPECTED_TOPIC   "thermo/gateway-01/00:AA:01:00:00:42/temperature"
/** 期待するペイロード */
#define EXPECTED_PAYLOAD "{\"node\":\"00:AA:01:00:00:42\",\"raw\":2568,\"uptime_ms\":123456}"

/** トピックは、クライアント ID と、ノードのアドレスから作る */
ZTEST(payload, test_topic)
{
    char buf[64] = {0};
    int len = payload_format_topic(buf, sizeof(buf), "gateway-01", &node);

    /* 期待: thermo/<クライアント ID>/<アドレス>/temperature の形式で、長さは、NUL を除く */
    zassert_equal(len, (int)strlen(EXPECTED_TOPIC));
    zassert_str_equal(buf, EXPECTED_TOPIC);
}

/** バッファがちょうどの大きさ (文字列 + NUL) なら、トピックを作れる */
ZTEST(payload, test_topic_exact_size)
{
    char buf[sizeof(EXPECTED_TOPIC)] = {0};

    /* 期待: バッファが、文字列 + NUL でちょうどなら、作れる */
    zassert_equal(payload_format_topic(buf, sizeof(buf), "gateway-01", &node),
                  (int)strlen(EXPECTED_TOPIC));
    zassert_str_equal(buf, EXPECTED_TOPIC);
}

/** バッファが 1 byte でも足りなければ、トピックは -ENOSPC */
ZTEST(payload, test_topic_no_space)
{
    char buf[sizeof(EXPECTED_TOPIC) - 1u] = {0};

    /* 期待: 1 byte でも足りなければ、-ENOSPC */
    zassert_equal(payload_format_topic(buf, sizeof(buf), "gateway-01", &node), -ENOSPC);
}

/** ペイロードは、アドレス、生値、稼働時間の JSON */
ZTEST(payload, test_payload)
{
    char buf[96] = {0};
    int len = payload_format_temperature(buf, sizeof(buf), &node, 2568u, 123456u);

    /* 期待: アドレス、生値、稼働時間を持つ JSON になる */
    zassert_equal(len, (int)strlen(EXPECTED_PAYLOAD));
    zassert_str_equal(buf, EXPECTED_PAYLOAD);
}

/** 値が最大 (生値 65535、稼働時間 4294967295) でも、そのまま出力する */
ZTEST(payload, test_payload_max_values)
{
    char buf[96] = {0};

    /* 期待: 生値と稼働時間が最大でも、桁を切らずに出力する */
    zassert_true(payload_format_temperature(buf, sizeof(buf), &node, UINT16_MAX, UINT32_MAX) > 0);
    zassert_not_null(strstr(buf, "\"raw\":65535,"));
    zassert_not_null(strstr(buf, "\"uptime_ms\":4294967295}"));
}

/** バッファが足りなければ、ペイロードは -ENOSPC (途中で切れた JSON を返さない) */
ZTEST(payload, test_payload_no_space)
{
    char buf[sizeof(EXPECTED_PAYLOAD) - 1u] = {0};

    /* 期待: 足りなければ -ENOSPC (途中で切れた JSON を返さない) */
    zassert_equal(payload_format_temperature(buf, sizeof(buf), &node, 2568u, 123456u), -ENOSPC);
}

ZTEST_SUITE(payload, NULL, NULL, NULL, NULL, NULL);
