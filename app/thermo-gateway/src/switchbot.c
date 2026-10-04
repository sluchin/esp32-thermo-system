/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief SwitchBot 屋外用温湿度計 (Outdoor Meter) のアドバタイズの解析と、送信の間引き
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include "switchbot.h"

/** サービスデータの UUID (16 bit) の、下位の byte (UUID 0xFD3D) */
#define SERVICE_UUID_LO     0x3Du
/** サービスデータの UUID (16 bit) の、上位の byte */
#define SERVICE_UUID_HI     0xFDu
/** 製造者データの会社 ID (SwitchBot: 0x0969) の、下位の byte */
#define COMPANY_ID_LO       0x69u
/** 製造者データの会社 ID の、上位の byte */
#define COMPANY_ID_HI       0x09u
/** サービスデータの、UUID のあとの長さ (機種、状態、電池残量) */
#define SERVICE_PAYLOAD_LEN 3u
/** 製造者データの、会社 ID のあとの長さ (MAC 6 byte、不明 2 byte、温度 3 byte) */
#define MFR_PAYLOAD_LEN     11u
/** 製造者データの、温度の位置 (会社 ID のあとから。MAC と不明な 2 byte の次) */
#define MFR_ENV_OFFSET      8u
/** 下位 7 bit を取り出すマスク (最上位の bit は、符号などの別の意味) */
#define LOW7_MASK           0x7Fu
/** 小数部 (10 分の 1) の、下位 4 bit のマスク */
#define DECIMAL_MASK        0x0Fu
/** 温度の整数部の最上位の bit: 1 なら 0 ℃ 以上、0 なら 0 ℃ 未満 */
#define SIGN_POSITIVE_BIT   0x80u
/** 湿度の最大値 [%] (これを超えたら、壊れたデータ) */
#define HUMIDITY_MAX        100u
/** 送信の間隔 [ms] */
#define INTERVAL_MS         CONFIG_THERMO_SWITCHBOT_INTERVAL_MS

/** 機器ごとの記録 */
struct sb_device {
    bt_addr_le_t addr; /**< アドレス */
    bool used;         /**< 使っているか */
    uint8_t model;     /**< 機種コード (サービスデータを受け取るまでは 0) */
    int8_t battery;    /**< 電池残量 [%]。わからなければ SWITCHBOT_BATTERY_UNKNOWN */
    bool reported;     /**< 1 回でも、送信する値を返したか */
    uint32_t last_ms;  /**< 最後に、送信する値を返した時刻 [ms] */
};

/** 機器ごとの記録 */
static struct sb_device devices[SWITCHBOT_MAX_DEVICES];

/**
 * サービスデータ (機種と電池残量) を解析する
 *
 * @param[in]  data UUID のあとから
 * @param[in]  len  data の長さ
 * @param[out] out  解析の結果
 * @return 長さが足りていれば true
 */
static bool parse_service_data(const uint8_t *data, uint8_t len, struct switchbot_ad *out)
{
    if (len < SERVICE_PAYLOAD_LEN) {
        return false;
    }
    out->kind = SWITCHBOT_INFO;
    out->model = data[0] & LOW7_MASK;
    out->battery = data[2] & LOW7_MASK;
    return true;
}

/**
 * 製造者データ (温度と湿度) を解析する
 *
 * 温度は、整数部 (下位 7 bit。最上位が 1 なら 0 ℃ 以上) と、小数部 (別の byte の下位 4 bit) から
 * 作る. 湿度は、下位 7 bit.
 *
 * @param[in]  data 会社 ID のあとから
 * @param[in]  len  data の長さ
 * @param[out] out  解析の結果
 * @return 長さが足りていて、湿度が範囲内なら true
 */
static bool parse_manufacturer_data(const uint8_t *data, uint8_t len, struct switchbot_ad *out)
{
    const uint8_t *env = &data[MFR_ENV_OFFSET];
    int16_t magnitude = 0;
    uint8_t humidity = 0u;

    if (len < MFR_PAYLOAD_LEN) {
        return false;
    }

    humidity = env[2] & LOW7_MASK;
    if (humidity > HUMIDITY_MAX) {
        return false;
    }

    magnitude = (int16_t)(((env[1] & LOW7_MASK) * 10) + (env[0] & DECIMAL_MASK));
    out->kind = SWITCHBOT_ENV;
    out->temp_x10 = ((env[1] & SIGN_POSITIVE_BIT) != 0u) ? magnitude : (int16_t)-magnitude;
    out->humidity = humidity;
    return true;
}

/* アドバタイズデータの 1 要素を解析する */
bool switchbot_parse(uint8_t type, const uint8_t *data, uint8_t len, struct switchbot_ad *out)
{
    /* UUID または会社 ID の 2 byte を確認して、そのあとの中身を解析する */
    if ((type == BT_DATA_SVC_DATA16) && (len >= 2u) && (data[0] == SERVICE_UUID_LO) &&
        (data[1] == SERVICE_UUID_HI)) {
        return parse_service_data(&data[2], (uint8_t)(len - 2u), out);
    }
    if ((type == BT_DATA_MANUFACTURER_DATA) && (len >= 2u) && (data[0] == COMPANY_ID_LO) &&
        (data[1] == COMPANY_ID_HI)) {
        return parse_manufacturer_data(&data[2], (uint8_t)(len - 2u), out);
    }
    return false;
}

/**
 * アドレスの機器の記録を探す (なければ、使っていない記録を割り当てる)
 *
 * @param[in] addr 機器のアドレス
 * @return 記録。全て使っていて、見つからなければ NULL
 */
static struct sb_device *find_device(const bt_addr_le_t *addr)
{
    struct sb_device *free_slot = NULL;
    size_t i = 0u;

    for (i = 0u; i < ARRAY_SIZE(devices); i++) {
        if (devices[i].used && bt_addr_le_eq(&devices[i].addr, addr)) {
            return &devices[i];
        }
        if (!devices[i].used && (free_slot == NULL)) {
            free_slot = &devices[i];
        }
    }

    if (free_slot != NULL) {
        (void)memset(free_slot, 0, sizeof(*free_slot));
        bt_addr_le_copy(&free_slot->addr, addr);
        free_slot->used = true;
        free_slot->battery = SWITCHBOT_BATTERY_UNKNOWN;
    }
    return free_slot;
}

/* 解析した結果を、機器ごとに記録して、送信する値を取り出す */
bool switchbot_accept(const bt_addr_le_t *addr, const struct switchbot_ad *ad, uint32_t now_ms,
                      struct switchbot_sample *out)
{
    struct sb_device *dev = find_device(addr);

    if (dev == NULL) {
        return false; /* 扱える機器の数を超えた */
    }

    if (ad->kind == SWITCHBOT_INFO) {
        dev->model = ad->model;
        dev->battery = (int8_t)ad->battery;
        return false;
    }

    /* 温度と湿度は、屋外用温湿度計と確認できた機器のものだけ使う (他の機種の製造者データと区別する)
     */
    if ((ad->kind != SWITCHBOT_ENV) || (dev->model != SWITCHBOT_MODEL_OUTDOOR)) {
        return false;
    }

    /* 前回の送信から、間隔がたっていなければ、送らない (最初の 1 回は、すぐ送る) */
    if (dev->reported && ((now_ms - dev->last_ms) < (uint32_t)INTERVAL_MS)) {
        return false;
    }

    dev->reported = true;
    dev->last_ms = now_ms;
    out->temp_x10 = ad->temp_x10;
    out->humidity = ad->humidity;
    out->battery = dev->battery;
    return true;
}

/* 機器ごとの記録を、全て消す */
void switchbot_reset(void)
{
    (void)memset(devices, 0, sizeof(devices));
}
