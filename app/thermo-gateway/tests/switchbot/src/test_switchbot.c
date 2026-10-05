/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief switchbot.c の単体テスト
 *
 * SwitchBot 屋外用温湿度計のアドバタイズデータの解析 (サービスデータと製造者データ) と、
 * 機器ごとの記録と送信の間引きを確認する.
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/ztest.h>
#include <stdlib.h>
#include <string.h> /* memcpy */

#include "switchbot.h"

/** 送信の間隔 [ms] (CMakeLists.txt の CONFIG_THERMO_SWITCHBOT_INTERVAL_MS) */
#define INTERVAL_MS 1000u

/** サービスデータ: 屋外用温湿度計 ('w')、状態 0、電池残量 100 % */
static const uint8_t service_outdoor[] = {0x3D, 0xFD, 0x77, 0x00, 0x64};
/** サービスデータ: 別の機種 ('T': 温湿度計)、電池残量 80 % */
static const uint8_t service_other[] = {0x3D, 0xFD, 0x54, 0x00, 0x50};
/** 製造者データ: 23.5 ℃、湿度 55 % (MAC、不明な 2 byte、温度、湿度) */
static const uint8_t mfr_plus[] = {0x69, 0x09, 0xB0, 0xE9, 0xFE, 0x12, 0x34,
                                   0x56, 0x00, 0x00, 0x05, 0x97, 0x37};

/** 機器 A のアドレス */
static const bt_addr_le_t addr_a = {
        .type = BT_ADDR_LE_RANDOM,
        .a = {.val = {0x56, 0x34, 0x12, 0xFE, 0xE9, 0xB0}},
};
/** 機器 B のアドレス */
static const bt_addr_le_t addr_b = {
        .type = BT_ADDR_LE_RANDOM,
        .a = {.val = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
};

/**
 * 製造者データ (23.5 ℃、湿度 55 %) の、温度と湿度の 3 byte を、書き換えたものを作る
 *
 * @param[out] out 13 byte の製造者データ
 * @param[in] dec 小数部の byte (下位 4 bit が、10 分の 1)
 * @param[in] integer 整数部の byte (最上位の bit が 1 なら 0 ℃ 以上)
 * @param[in] humidity 湿度の byte
 */
static void make_mfr(uint8_t *out, uint8_t dec, uint8_t integer, uint8_t humidity)
{
    (void)memcpy(out, mfr_plus, sizeof(mfr_plus));
    out[10] = dec;
    out[11] = integer;
    out[12] = humidity;
}

/**
 * 各テストの前に, 機器ごとの記録を消す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    switchbot_reset();
}

/** サービスデータ: 機種と、電池残量を取り出す */
ZTEST(switchbot, test_parse_service_data)
{
    struct switchbot_ad ad = {.kind = SWITCHBOT_NONE}; /* 広告データ */

    zassert_true(
            switchbot_parse(BT_DATA_SVC_DATA16, service_outdoor, sizeof(service_outdoor), &ad));

    /* 期待: 機種は 'w' (屋外用)、電池残量は 100 % */
    zassert_equal(ad.kind, SWITCHBOT_INFO);
    zassert_equal(ad.model, SWITCHBOT_MODEL_OUTDOOR);
    zassert_equal(ad.battery, 100u);
}

/** サービスデータ: 各 byte の最上位の bit は、別の意味なので、取り除く */
ZTEST(switchbot, test_parse_service_data_masks_high_bit)
{
    const uint8_t data[] = {0x3D, 0xFD, 0xF7, 0x00, 0xE4}; /* 入力データ */
    struct switchbot_ad ad = {.kind = SWITCHBOT_NONE};     /* 広告データ */

    zassert_true(switchbot_parse(BT_DATA_SVC_DATA16, data, sizeof(data), &ad));

    /* 期待: 0xF7 -> 0x77、0xE4 -> 0x64 (100) */
    zassert_equal(ad.model, 0x77u);
    zassert_equal(ad.battery, 100u);
}

/** 製造者データ: 0 ℃ 以上の温度 (整数部の最上位の bit が 1) と、湿度 */
ZTEST(switchbot, test_parse_manufacturer_positive)
{
    struct switchbot_ad ad = {.kind = SWITCHBOT_NONE}; /* 広告データ */

    zassert_true(switchbot_parse(BT_DATA_MANUFACTURER_DATA, mfr_plus, sizeof(mfr_plus), &ad));

    /* 期待: 23.5 ℃ (235)、湿度 55 % */
    zassert_equal(ad.kind, SWITCHBOT_ENV);
    zassert_equal(ad.temp_x10, 235);
    zassert_equal(ad.humidity, 55u);
}

/** 製造者データ: 0 ℃ 未満の温度 (整数部の最上位の bit が 0) */
ZTEST(switchbot, test_parse_manufacturer_negative)
{
    uint8_t data[sizeof(mfr_plus)];                    /* 入力データ */
    struct switchbot_ad ad = {.kind = SWITCHBOT_NONE}; /* 広告データ */

    make_mfr(data, 0x03u, 0x05u, 0x37u);

    zassert_true(switchbot_parse(BT_DATA_MANUFACTURER_DATA, data, sizeof(data), &ad));

    /* 期待: -5.3 ℃ (-53) */
    zassert_equal(ad.temp_x10, -53);
}

/** 製造者データ: 小数部の上位 4 bit は、別の意味なので、取り除く。0.0 ℃ は、符号なしで 0 */
ZTEST(switchbot, test_parse_manufacturer_decimal_mask_and_zero)
{
    uint8_t data[sizeof(mfr_plus)];                    /* 入力データ */
    struct switchbot_ad ad = {.kind = SWITCHBOT_NONE}; /* 広告データ */

    make_mfr(data, 0xA7u, 0x80u, 0x37u);

    zassert_true(switchbot_parse(BT_DATA_MANUFACTURER_DATA, data, sizeof(data), &ad));

    /* 期待: 小数部は 0x07 だけ使う -> 0.7 ℃ (7) */
    zassert_equal(ad.temp_x10, 7);

    make_mfr(data, 0x00u, 0x80u, 0x37u);
    zassert_true(switchbot_parse(BT_DATA_MANUFACTURER_DATA, data, sizeof(data), &ad));
    zassert_equal(ad.temp_x10, 0);
}

/** 製造者データ: 湿度は、下位 7 bit で、100 % まで。それを超えたら、壊れたデータとして捨てる */
ZTEST(switchbot, test_parse_manufacturer_humidity_limit)
{
    uint8_t data[sizeof(mfr_plus)];                    /* 入力データ */
    struct switchbot_ad ad = {.kind = SWITCHBOT_NONE}; /* 広告データ */

    make_mfr(data, 0x05u, 0x97u, 0x64u);
    zassert_true(switchbot_parse(BT_DATA_MANUFACTURER_DATA, data, sizeof(data), &ad));
    zassert_equal(ad.humidity, 100u);

    /* 期待: 101 % は、拒否して、出力を変えない */
    ad.kind = SWITCHBOT_NONE;
    make_mfr(data, 0x05u, 0x97u, 0x65u);
    zassert_false(switchbot_parse(BT_DATA_MANUFACTURER_DATA, data, sizeof(data), &ad));
    zassert_equal(ad.kind, SWITCHBOT_NONE);

    /* 最上位の bit は、取り除く (0xB7 -> 0x37 = 55 %) */
    make_mfr(data, 0x05u, 0x97u, 0xB7u);
    zassert_true(switchbot_parse(BT_DATA_MANUFACTURER_DATA, data, sizeof(data), &ad));
    zassert_equal(ad.humidity, 55u);
}

/** 長さが足りなければ、どちらのデータも、拒否する (ちょうどの長さは、受け付ける) */
ZTEST(switchbot, test_parse_short_data)
{
    struct switchbot_ad ad = {.kind = SWITCHBOT_NONE}; /* 広告データ */

    zassert_false(switchbot_parse(BT_DATA_SVC_DATA16, service_outdoor, 4u, &ad));
    zassert_true(switchbot_parse(BT_DATA_SVC_DATA16, service_outdoor, 5u, &ad));
    zassert_false(switchbot_parse(BT_DATA_MANUFACTURER_DATA, mfr_plus, 12u, &ad));
    zassert_true(switchbot_parse(BT_DATA_MANUFACTURER_DATA, mfr_plus, 13u, &ad));
}

/** UUID や会社 ID の 2 byte すら、ない短いデータは、拒否する */
ZTEST(switchbot, test_parse_too_short_for_id)
{
    struct switchbot_ad ad = {.kind = SWITCHBOT_NONE}; /* 広告データ */

    zassert_false(switchbot_parse(BT_DATA_SVC_DATA16, service_outdoor, 1u, &ad));
    zassert_false(switchbot_parse(BT_DATA_MANUFACTURER_DATA, mfr_plus, 1u, &ad));
}

/** UUID、会社 ID、AD の種類が違うデータは、SwitchBot のものではないので、拒否する */
ZTEST(switchbot, test_parse_other_data)
{
    uint8_t data[sizeof(mfr_plus)];                    /* 入力データ */
    struct switchbot_ad ad = {.kind = SWITCHBOT_NONE}; /* 広告データ */

    /* サービスデータ: UUID の下位、上位の byte が、それぞれ違う */
    (void)memcpy(data, service_outdoor, sizeof(service_outdoor));
    data[0] = 0x3Eu;
    zassert_false(switchbot_parse(BT_DATA_SVC_DATA16, data, sizeof(service_outdoor), &ad));
    data[0] = 0x3Du;
    data[1] = 0xFCu;
    zassert_false(switchbot_parse(BT_DATA_SVC_DATA16, data, sizeof(service_outdoor), &ad));

    /* 製造者データ: 会社 ID の下位、上位の byte が、それぞれ違う */
    (void)memcpy(data, mfr_plus, sizeof(mfr_plus));
    data[0] = 0x6Au;
    zassert_false(switchbot_parse(BT_DATA_MANUFACTURER_DATA, data, sizeof(data), &ad));
    data[0] = 0x69u;
    data[1] = 0x0Au;
    zassert_false(switchbot_parse(BT_DATA_MANUFACTURER_DATA, data, sizeof(data), &ad));

    /* AD の種類が違う (名前) */
    zassert_false(
            switchbot_parse(BT_DATA_NAME_COMPLETE, service_outdoor, sizeof(service_outdoor), &ad));
}

/**
 * 解析した結果を作る
 *
 * @param[in] kind 種類
 * @param[in] model 機種 (SWITCHBOT_INFO)
 * @param[in] battery 電池残量 (SWITCHBOT_INFO)
 * @param[in] temp_x10 温度 (SWITCHBOT_ENV)
 * @param[in] humidity 湿度 (SWITCHBOT_ENV)
 * @return 解析した結果
 */
static struct switchbot_ad make_ad(enum switchbot_kind kind, uint8_t model, uint8_t battery,
                                   int16_t temp_x10, uint8_t humidity)
{
    struct switchbot_ad ad = {.kind = kind,
                              .model = model,
                              .battery = battery,
                              .temp_x10 = temp_x10,
                              .humidity = humidity};

    return ad;
}

/** 機種と電池残量を受け取ったあとの、最初の温湿度は、すぐ送る (電池残量を添える) */
ZTEST(switchbot, test_accept_first_report)
{
    struct switchbot_sample sample = {0}; /* サンプル */
    /* 機器の情報の広告データ */
    struct switchbot_ad info = make_ad(SWITCHBOT_INFO, SWITCHBOT_MODEL_OUTDOOR, 87u, 0, 0u);
    struct switchbot_ad env = make_ad(SWITCHBOT_ENV, 0u, 0u, -53, 55u); /* 環境データ */

    /* 期待: 機種と電池残量だけでは、送らない */
    zassert_false(switchbot_accept(&addr_a, &info, 0u, &sample));

    zassert_true(switchbot_accept(&addr_a, &env, 10u, &sample));
    zassert_equal(sample.temp_x10, -53);
    zassert_equal(sample.humidity, 55u);
    zassert_equal(sample.battery, 87);
}

/** 機種がわからない機器の温湿度は、送らない (サービスデータを受け取ってから、送る) */
ZTEST(switchbot, test_accept_requires_model)
{
    struct switchbot_sample sample = {0}; /* サンプル */
    /* 機器の情報の広告データ */
    struct switchbot_ad info = make_ad(SWITCHBOT_INFO, SWITCHBOT_MODEL_OUTDOOR, 50u, 0, 0u);
    struct switchbot_ad env = make_ad(SWITCHBOT_ENV, 0u, 0u, 235, 55u); /* 環境データ */

    zassert_false(switchbot_accept(&addr_a, &env, 0u, &sample));

    (void)switchbot_accept(&addr_a, &info, 100u, &sample);
    zassert_true(switchbot_accept(&addr_a, &env, 200u, &sample));
}

/** 屋外用温湿度計ではない機種 (別のレイアウトの製造者データ) の値は、送らない */
ZTEST(switchbot, test_accept_ignores_other_model)
{
    struct switchbot_sample sample = {0};                               /* サンプル */
    struct switchbot_ad info = {.kind = SWITCHBOT_NONE};                /* 機器の情報の広告データ */
    struct switchbot_ad env = make_ad(SWITCHBOT_ENV, 0u, 0u, 235, 55u); /* 環境データ */

    /* 機種 'T' のサービスデータを、解析して、記録する */
    zassert_true(switchbot_parse(BT_DATA_SVC_DATA16, service_other, sizeof(service_other), &info));
    (void)switchbot_accept(&addr_a, &info, 0u, &sample);

    zassert_false(switchbot_accept(&addr_a, &env, 1000u, &sample));
}

/** 解析の結果が、どちらでもなければ (NONE)、送らない */
ZTEST(switchbot, test_accept_ignores_none)
{
    struct switchbot_sample sample = {0};                              /* サンプル */
    struct switchbot_ad none = make_ad(SWITCHBOT_NONE, 0u, 0u, 0, 0u); /* 種別のない広告データ */

    zassert_false(switchbot_accept(&addr_a, &none, 0u, &sample));
}

/** 間隔 (1000 ms) 未満の値は捨てて、間隔がたてば、送る (ちょうど間隔でも、送る) */
ZTEST(switchbot, test_accept_interval)
{
    struct switchbot_sample sample = {0}; /* サンプル */
    /* 機器の情報の広告データ */
    struct switchbot_ad info = make_ad(SWITCHBOT_INFO, SWITCHBOT_MODEL_OUTDOOR, 50u, 0, 0u);
    struct switchbot_ad env = make_ad(SWITCHBOT_ENV, 0u, 0u, 235, 55u); /* 環境データ */

    (void)switchbot_accept(&addr_a, &info, 0u, &sample);

    zassert_true(switchbot_accept(&addr_a, &env, 5000u, &sample));
    zassert_false(switchbot_accept(&addr_a, &env, 5000u + INTERVAL_MS - 1u, &sample));
    zassert_true(switchbot_accept(&addr_a, &env, 5000u + INTERVAL_MS, &sample));

    /* 次の間隔は、送った時刻から数える (捨てた時刻からではない) */
    zassert_false(switchbot_accept(&addr_a, &env, 5000u + INTERVAL_MS + 500u, &sample));
    zassert_true(switchbot_accept(&addr_a, &env, 5000u + (2u * INTERVAL_MS), &sample));
}

/** 稼働時間のカウンタが、一周 (UINT32_MAX を超える) しても、間隔を正しく測る */
ZTEST(switchbot, test_accept_interval_wraps)
{
    struct switchbot_sample sample = {0}; /* サンプル */
    /* 機器の情報の広告データ */
    struct switchbot_ad info = make_ad(SWITCHBOT_INFO, SWITCHBOT_MODEL_OUTDOOR, 50u, 0, 0u);
    struct switchbot_ad env = make_ad(SWITCHBOT_ENV, 0u, 0u, 235, 55u); /* 環境データ */

    (void)switchbot_accept(&addr_a, &info, 0u, &sample);

    zassert_true(switchbot_accept(&addr_a, &env, UINT32_MAX - 99u, &sample));

    /* 期待: 一周して 100 ms 後 (差は 200 ms) は、まだ捨てる。1000 ms 後 (差は 1100 ms) は、送る */
    zassert_false(switchbot_accept(&addr_a, &env, 100u, &sample));
    zassert_true(switchbot_accept(&addr_a, &env, 1000u, &sample));
}

/** 機器ごとに、別々に記録して、間引く (電池残量も、機器ごと) */
ZTEST(switchbot, test_accept_devices_are_independent)
{
    struct switchbot_sample sample = {0}; /* サンプル */
    /* 機器の情報の広告データ (A) */
    struct switchbot_ad info_a = make_ad(SWITCHBOT_INFO, SWITCHBOT_MODEL_OUTDOOR, 90u, 0, 0u);
    /* 機器の情報の広告データ (B) */
    struct switchbot_ad info_b = make_ad(SWITCHBOT_INFO, SWITCHBOT_MODEL_OUTDOOR, 40u, 0, 0u);
    struct switchbot_ad env = make_ad(SWITCHBOT_ENV, 0u, 0u, 235, 55u); /* 環境データ */

    (void)switchbot_accept(&addr_a, &info_a, 0u, &sample);
    (void)switchbot_accept(&addr_b, &info_b, 0u, &sample);

    zassert_true(switchbot_accept(&addr_a, &env, 5000u, &sample));
    zassert_equal(sample.battery, 90);

    /* A を送った直後でも、B は、最初の 1 回なので、送る */
    zassert_true(switchbot_accept(&addr_b, &env, 5100u, &sample));
    zassert_equal(sample.battery, 40);
    zassert_false(switchbot_accept(&addr_a, &env, 5200u, &sample));
}

/** 扱える機器の数 (4 台) を超えた機器は、無視する。すでにある機器は、扱える */
ZTEST(switchbot, test_accept_device_table_full)
{
    struct switchbot_sample sample = {0}; /* サンプル */
    /* 機器の情報の広告データ */
    struct switchbot_ad info = make_ad(SWITCHBOT_INFO, SWITCHBOT_MODEL_OUTDOOR, 50u, 0, 0u);
    struct switchbot_ad env = make_ad(SWITCHBOT_ENV, 0u, 0u, 235, 55u); /* 環境データ */
    bt_addr_le_t addr = addr_a;                                         /* アドレス */
    unsigned int i = 0u;                                                /* ループ用の添字 */

    for (i = 0u; i < SWITCHBOT_MAX_DEVICES; i++) {
        addr.a.val[0] = (uint8_t)i;
        (void)switchbot_accept(&addr, &info, 0u, &sample);
    }

    /* 5 台目 */
    addr.a.val[0] = (uint8_t)SWITCHBOT_MAX_DEVICES;
    zassert_false(switchbot_accept(&addr, &info, 0u, &sample));
    zassert_false(switchbot_accept(&addr, &env, 5000u, &sample));

    /* 4 台目までは、扱える */
    addr.a.val[0] = (uint8_t)(SWITCHBOT_MAX_DEVICES - 1u);
    zassert_true(switchbot_accept(&addr, &env, 5000u, &sample));
}

/** 記録を消すと、機種が、わからない状態に戻って、枠も空く */
ZTEST(switchbot, test_reset)
{
    struct switchbot_sample sample = {0}; /* サンプル */
    /* 機器の情報の広告データ */
    struct switchbot_ad info = make_ad(SWITCHBOT_INFO, SWITCHBOT_MODEL_OUTDOOR, 50u, 0, 0u);
    struct switchbot_ad env = make_ad(SWITCHBOT_ENV, 0u, 0u, 235, 55u); /* 環境データ */

    (void)switchbot_accept(&addr_a, &info, 0u, &sample);
    zassert_true(switchbot_accept(&addr_a, &env, 5000u, &sample));

    switchbot_reset();

    zassert_false(switchbot_accept(&addr_a, &env, 5000u, &sample));
}

ZTEST_SUITE(switchbot, NULL, NULL, before, NULL, NULL);
