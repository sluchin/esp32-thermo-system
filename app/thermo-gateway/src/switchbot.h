/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_GATEWAY_SWITCHBOT_H
#define THERMO_GATEWAY_SWITCHBOT_H

/**
 * @file
 * @brief SwitchBot 屋外用温湿度計 (Outdoor Meter) のアドバタイズの解析と、送信の間引き
 *
 * SwitchBot の温湿度計は、接続せずに、アドバタイズだけで、温度と湿度を送る。
 * 1 つの機器の情報は、2 つのアドバタイズデータに分かれて届く。
 *  - サービスデータ (UUID 0xFD3D): 機種と、電池残量
 *  - 製造者データ (会社 ID 0x0969): MAC アドレスのあとに、温度と湿度
 *
 * 並びは、SwitchBot の公開仕様 (OpenWonderLabs/SwitchBotAPI-BLE) に基づく。
 * 実機での確認は、まだ。
 */

#include <zephyr/bluetooth/addr.h>
#include <stdbool.h> /* bool */
#include <stdint.h>  /* uint8_t int16_t int8_t uint32_t */

/** 屋外用温湿度計の機種コード (サービスデータの先頭。ASCII の 'w') */
#define SWITCHBOT_MODEL_OUTDOOR   0x77u
/** 電池残量が、わからないことを示す値 */
#define SWITCHBOT_BATTERY_UNKNOWN (-1)
/** 同時に扱える機器の数 */
#define SWITCHBOT_MAX_DEVICES     4u

/** アドバタイズデータ 1 要素の内容の種類 */
enum switchbot_kind {
    SWITCHBOT_NONE, /**< SwitchBot の温湿度計のデータではない */
    SWITCHBOT_INFO, /**< 機種と電池残量 (サービスデータ) */
    SWITCHBOT_ENV   /**< 温度と湿度 (製造者データ) */
};

/** アドバタイズデータ 1 要素を、解析した結果 */
struct switchbot_ad {
    enum switchbot_kind kind; /**< 内容の種類 */
    uint8_t model;            /**< 機種コード (SWITCHBOT_INFO のとき) */
    uint8_t battery;          /**< 電池残量 [%] (SWITCHBOT_INFO のとき) */
    int16_t temp_x10;         /**< 温度 [℃ の 10 倍] (SWITCHBOT_ENV のとき) */
    uint8_t humidity;         /**< 湿度 [%] (SWITCHBOT_ENV のとき) */
};

/** 送信する温湿度計の値 */
struct switchbot_sample {
    int16_t temp_x10; /**< 温度 [℃ の 10 倍] */
    uint8_t humidity; /**< 湿度 [%] */
    int8_t battery;   /**< 電池残量 [%]。わからなければ SWITCHBOT_BATTERY_UNKNOWN */
};

/**
 * @brief アドバタイズデータの 1 要素を解析する。
 *
 * @param[in]  type AD の種類 (BT_DATA_SVC_DATA16 か BT_DATA_MANUFACTURER_DATA)
 * @param[in]  data AD の中身 (UUID または会社 ID から)
 * @param[in]  len  data の長さ
 * @param[out] out  解析の結果
 * @return SwitchBot の温湿度計のデータなら true。そうでなければ false (out は、変えない)
 */
bool switchbot_parse(uint8_t type, const uint8_t *data, uint8_t len, struct switchbot_ad *out);

/**
 * @brief 解析した結果を、機器ごとに記録して、送信する値を取り出す。
 *
 * SWITCHBOT_INFO は、機種と電池残量を記録するだけ。SWITCHBOT_ENV は、機種が屋外用温湿度計と
 * わかっている機器で、前回の送信から CONFIG_THERMO_SWITCHBOT_INTERVAL_MS 以上たっていれば、
 * 送信する値を返す (最初の 1 回は、すぐ返す)。
 *
 * @param[in]  addr   機器のアドレス
 * @param[in]  ad     switchbot_parse() の結果
 * @param[in]  now_ms 現在の稼働時間 [ms]
 * @param[out] out    送信する値
 * @return 送信する値があれば true
 */
bool switchbot_accept(const bt_addr_le_t *addr, const struct switchbot_ad *ad, uint32_t now_ms,
                      struct switchbot_sample *out);

/**
 * @brief 機器ごとの記録を、全て消す。
 *
 * 通常は呼ばない。単体テストが、前のテストの記録を、残さないために使う。
 */
void switchbot_reset(void);

#endif /* THERMO_GATEWAY_SWITCHBOT_H */
