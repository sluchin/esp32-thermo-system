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
#define EXPECTED_TOPIC    "thermo/gateway-01/00:AA:01:00:00:42/temperature"
/** 期待する SwitchBot のトピック */
#define EXPECTED_SB_TOPIC "thermo/gateway-01/switchbot/00:AA:01:00:00:42"
/** 期待する SwitchBot のペイロードの前半 (温度の前まで) */
#define SB_PAYLOAD_HEAD  "{\"node\":\"00:AA:01:00:00:42\",\"type\":\"switchbot\",\"temperature_c\":"
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
    int len = payload_format_temperature(buf, sizeof(buf), &node, 2568u, 123456u, -1);

    /* 期待: アドレス、生値、稼働時間を持つ JSON になる */
    zassert_equal(len, (int)strlen(EXPECTED_PAYLOAD));
    zassert_str_equal(buf, EXPECTED_PAYLOAD);
}

/** 値が最大 (生値 65535、稼働時間 4294967295) でも、そのまま出力する */
ZTEST(payload, test_payload_max_values)
{
    char buf[96] = {0};

    /* 期待: 生値と稼働時間が最大でも、桁を切らずに出力する */
    zassert_true(payload_format_temperature(buf, sizeof(buf), &node, UINT16_MAX, UINT32_MAX, -1) >
                 0);
    zassert_not_null(strstr(buf, "\"raw\":65535,"));
    zassert_not_null(strstr(buf, "\"uptime_ms\":4294967295}"));
}

/** バッファが足りなければ、ペイロードは -ENOSPC (途中で切れた JSON を返さない) */
ZTEST(payload, test_payload_no_space)
{
    char buf[sizeof(EXPECTED_PAYLOAD) - 1u] = {0};

    /* 期待: 足りなければ -ENOSPC (途中で切れた JSON を返さない) */
    zassert_equal(payload_format_temperature(buf, sizeof(buf), &node, 2568u, 123456u, -1), -ENOSPC);
}

/** SwitchBot のトピックは、クライアント ID と、機器のアドレスから作る (switchbot の階層を挟む) */
ZTEST(payload, test_switchbot_topic)
{
    char buf[64] = {0};
    int len = payload_format_switchbot_topic(buf, sizeof(buf), "gateway-01", &node);

    /* 期待: thermo/<クライアント ID>/switchbot/<アドレス> */
    zassert_equal(len, (int)strlen(EXPECTED_SB_TOPIC));
    zassert_str_equal(buf, EXPECTED_SB_TOPIC);
}

/** SwitchBot のトピック: ちょうどの大きさなら作れて、1 byte 足りなければ -ENOSPC */
ZTEST(payload, test_switchbot_topic_size_limit)
{
    char exact[sizeof(EXPECTED_SB_TOPIC)] = {0};
    char small[sizeof(EXPECTED_SB_TOPIC) - 1u] = {0};

    zassert_equal(payload_format_switchbot_topic(exact, sizeof(exact), "gateway-01", &node),
                  (int)strlen(EXPECTED_SB_TOPIC));
    zassert_equal(payload_format_switchbot_topic(small, sizeof(small), "gateway-01", &node),
                  -ENOSPC);
}

/** SwitchBot のペイロードは、温度 (℃)、湿度、電池残量、稼働時間を持つ JSON */
ZTEST(payload, test_switchbot_payload)
{
    char buf[160] = {0};
    const struct switchbot_sample sample = {.temp_x10 = 235, .humidity = 55u, .battery = 87};

    zassert_true(payload_format_switchbot(buf, sizeof(buf), &node, &sample, 123456u, -1) > 0);

    /* 期待: 温度は小数点以下 1 桁 (23.5)。電池残量が、湿度のあとに続く */
    zassert_str_equal(buf, SB_PAYLOAD_HEAD "23.5,\"humidity\":55,\"battery\":87,"
                                           "\"uptime_ms\":123456}");
}

/** 0 ℃ 未満の温度は、符号を付ける (整数部が 0 の -0.5 でも、符号を落とさない) */
ZTEST(payload, test_switchbot_payload_negative_temperature)
{
    char buf[160] = {0};
    struct switchbot_sample sample = {.temp_x10 = -53, .humidity = 55u, .battery = 87};

    zassert_true(payload_format_switchbot(buf, sizeof(buf), &node, &sample, 1u, -1) > 0);
    zassert_not_null(strstr(buf, "\"temperature_c\":-5.3,"));

    sample.temp_x10 = -5;
    zassert_true(payload_format_switchbot(buf, sizeof(buf), &node, &sample, 1u, -1) > 0);
    zassert_not_null(strstr(buf, "\"temperature_c\":-0.5,"));
}

/** 0.0 ℃ は、符号なし */
ZTEST(payload, test_switchbot_payload_zero_temperature)
{
    char buf[160] = {0};
    const struct switchbot_sample sample = {.temp_x10 = 0, .humidity = 0u, .battery = 0};

    zassert_true(payload_format_switchbot(buf, sizeof(buf), &node, &sample, 1u, -1) > 0);
    zassert_not_null(strstr(buf, "\"temperature_c\":0.0,\"humidity\":0,\"battery\":0,"));
}

/** 電池残量がわからないときは、battery の項目を出力しない */
ZTEST(payload, test_switchbot_payload_unknown_battery)
{
    char buf[160] = {0};
    const struct switchbot_sample sample = {
            .temp_x10 = 235, .humidity = 55u, .battery = SWITCHBOT_BATTERY_UNKNOWN};

    zassert_true(payload_format_switchbot(buf, sizeof(buf), &node, &sample, 1u, -1) > 0);

    zassert_is_null(strstr(buf, "battery"));
    zassert_not_null(strstr(buf, "\"humidity\":55,\"uptime_ms\":1}"));
}

/** 最大の値 (-3276.7 ℃、湿度 100 %、電池 100 %、稼働時間が最大) でも、123 文字で、160 byte に収まる
 */
ZTEST(payload, test_switchbot_payload_longest)
{
    char buf[160] = {0};
    const struct switchbot_sample sample = {
            .temp_x10 = INT16_MIN + 1, .humidity = 100u, .battery = 100};

    zassert_equal(payload_format_switchbot(buf, sizeof(buf), &node, &sample, UINT32_MAX, -1), 123);
}

/** バッファが足りなければ、SwitchBot のペイロードは -ENOSPC (途中で切れた JSON を返さない) */
ZTEST(payload, test_switchbot_payload_no_space)
{
    char buf[100] = {0};
    const struct switchbot_sample sample = {.temp_x10 = 235, .humidity = 55u, .battery = 87};

    zassert_equal(payload_format_switchbot(buf, sizeof(buf), &node, &sample, 123456u, -1), -ENOSPC);
}

/** UNIX 時刻があれば、"timestamp" を、末尾に出力する */
ZTEST(payload, test_payload_timestamp)
{
    char buf[96] = {0};

    zassert_true(payload_format_temperature(buf, sizeof(buf), &node, 2568u, 123456u, 1790000000) >
                 0);
    zassert_str_equal(buf, "{\"node\":\"00:AA:01:00:00:42\",\"raw\":2568,"
                           "\"uptime_ms\":123456,\"timestamp\":1790000000}");
}

/** UNIX 時刻が 0 (1970 年) でも、出力する (出力しないのは、負のときだけ) */
ZTEST(payload, test_payload_timestamp_zero)
{
    char buf[96] = {0};

    zassert_true(payload_format_temperature(buf, sizeof(buf), &node, 1u, 1u, 0) > 0);
    zassert_not_null(strstr(buf, "\"uptime_ms\":1,\"timestamp\":0}"));
}

/** SwitchBot のペイロードにも、"timestamp" を、末尾に出力する */
ZTEST(payload, test_switchbot_payload_timestamp)
{
    char buf[160] = {0};
    const struct switchbot_sample sample = {.temp_x10 = 235, .humidity = 55u, .battery = 87};

    zassert_true(payload_format_switchbot(buf, sizeof(buf), &node, &sample, 1u, 1790000000) > 0);
    zassert_not_null(strstr(buf, "\"uptime_ms\":1,\"timestamp\":1790000000}"));
}

/** 最大の値 (UNIX 時刻が 19 桁を含む) でも、155 文字で、160 byte に収まる */
ZTEST(payload, test_switchbot_payload_longest_with_timestamp)
{
    char buf[160] = {0};
    const struct switchbot_sample sample = {
            .temp_x10 = INT16_MIN + 1, .humidity = 100u, .battery = 100};

    zassert_equal(payload_format_switchbot(buf, sizeof(buf), &node, &sample, UINT32_MAX, INT64_MAX),
                  155);
}

ZTEST_SUITE(payload, NULL, NULL, NULL, NULL, NULL);
