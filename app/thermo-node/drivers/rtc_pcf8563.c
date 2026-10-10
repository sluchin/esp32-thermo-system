/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief PCF8563 の RTC ドライバ (Zephyr の RTC API の set_time と get_time だけを実装する)
 *
 * Zephyr v4.3 の RTC ドライバ (rtc_pcf8563.c) は, 月 (0 から 11 の検証で, 12 月が通らないのに,
 * レジスタには, そのまま書く) と年 (1900 年からの年数を, そのまま BCD にする) の扱いが, チップ
 * (月は 1 から 12, 年は 00 から 99) と合っていないので使わない. このドライバは, Zephyr の
 * rtc_time (tm_mon は 0 から 11, tm_year は 1900 年からの年数) を, レジスタの値に正しく直す.
 * 扱える年は, 2000 年から 2099 年まで (世紀のビットは, 書くときは 0 で, 読むときは無視する).
 * アラームと割り込みは, 実装しない.
 *
 * Devicetree の compatible は thermo,pcf8563 (nxp,pcf8563 は, Zephyr のドライバと重なる).
 */

/** このドライバが扱う Devicetree の compatible (thermo,pcf8563) */
#define DT_DRV_COMPAT thermo_pcf8563

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <errno.h>   /* ENODEV ENODATA EINVAL */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* uint8_t */
#include <stdlib.h>  /* EXIT_SUCCESS */

#include "thermo_log.h"

LOG_MODULE_REGISTER(rtc_pcf8563_thermo_node, THERMO_LOG_LEVEL);

/** 制御レジスタ 1 のアドレス (bit 5 の STOP が 1 だと, 時計が止まる) */
#define PCF8563_REG_CONTROL1 0x00U
/** 秒のレジスタのアドレス (ここから, 秒, 分, 時, 日, 曜日, 月, 年の 7 バイト) */
#define PCF8563_REG_SECONDS  0x02U
/** 時刻のレジスタの数 [バイト] */
#define PCF8563_TIME_BYTES   7U
/** 秒のレジスタの, 電圧低下 (VL) のビット (1 のとき, 時刻が信用できない) */
#define PCF8563_VL_BIT       0x80U
/** 秒のレジスタの, 秒の値のマスク */
#define PCF8563_MASK_SECOND  0x7FU
/** 分のレジスタの, 分の値のマスク */
#define PCF8563_MASK_MINUTE  0x7FU
/** 時のレジスタの, 時の値のマスク */
#define PCF8563_MASK_HOUR    0x3FU
/** 日のレジスタの, 日の値のマスク */
#define PCF8563_MASK_DAY     0x3FU
/** 曜日のレジスタの, 曜日の値のマスク */
#define PCF8563_MASK_WEEKDAY 0x07U
/** 月のレジスタの, 月の値のマスク (bit 7 の世紀のビットは, 使わない) */
#define PCF8563_MASK_MONTH   0x1FU
/** 時刻のレジスタの並びの, 秒の位置 */
#define IDX_SECOND           0U
/** 時刻のレジスタの並びの, 分の位置 */
#define IDX_MINUTE           1U
/** 時刻のレジスタの並びの, 時の位置 */
#define IDX_HOUR             2U
/** 時刻のレジスタの並びの, 日の位置 */
#define IDX_DAY              3U
/** 時刻のレジスタの並びの, 曜日の位置 */
#define IDX_WEEKDAY          4U
/** 時刻のレジスタの並びの, 月の位置 */
#define IDX_MONTH            5U
/** 時刻のレジスタの並びの, 年の位置 */
#define IDX_YEAR             6U
/** 年のレジスタ 00 の, rtc_time の tm_year (2000 年 - 1900 年) */
#define TM_YEAR_BASE         100
/** 年のレジスタ 99 の, rtc_time の tm_year (2099 年 - 1900 年) */
#define TM_YEAR_LAST         199
/** 秒と分の最大 */
#define MAX_SEC_MIN          59
/** 時の最大 */
#define MAX_HOUR             23
/** 日の最大 */
#define MAX_DAY              31
/** rtc_time の tm_mon の最大 (12 月) */
#define MAX_TM_MON           11
/** 曜日の最大 (土曜日. 日曜日が 0) */
#define MAX_WEEKDAY          6

/** ドライバの設定 (Devicetree から作る) */
struct pcf8563_config {
    struct i2c_dt_spec i2c; /**< RTC の I2C のバスとアドレス */
};

static bool is_in_range(int value, int min, int max);

static int pcf8563_set_time(const struct device *dev, const struct rtc_time *timeptr);

static int pcf8563_get_time(const struct device *dev, struct rtc_time *timeptr);

static int pcf8563_init(const struct device *dev);

/** RTC のドライバ API (set_time と get_time だけ) */
static DEVICE_API(rtc, pcf8563_api) = {
    .set_time = pcf8563_set_time,
    .get_time = pcf8563_get_time,
};

/**
 * @brief RTC に時刻を設定する
 *
 * 時計が止まっていたら動かして (制御レジスタ 1 を 0 にする), 時刻のレジスタに書く. 秒のレジスタを
 * 書くと, 電圧低下のビット (VL) が, 0 になる.
 *
 * @param[in] dev     RTC のデバイス
 * @param[in] timeptr 設定する時刻 (tm_year は 100 から 199, tm_wday は 0 から 6 であること)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -EINVAL      範囲外の値 (チップが扱えない時刻)
 * @retval negative     I2C の通信の失敗 (負の errno)
 */
static int pcf8563_set_time(const struct device *dev, const struct rtc_time *timeptr)
{
    const struct pcf8563_config *config = NULL; /* ドライバの設定 */
    uint8_t raw[PCF8563_TIME_BYTES] = {0};      /* 時刻のレジスタの値 (BCD) */
    int err = EXIT_SUCCESS;                     /* エラーコード */

    config = (const struct pcf8563_config *)dev->config;

    if (!is_in_range(timeptr->tm_sec, 0, MAX_SEC_MIN) ||
        !is_in_range(timeptr->tm_min, 0, MAX_SEC_MIN) ||
        !is_in_range(timeptr->tm_hour, 0, MAX_HOUR) || !is_in_range(timeptr->tm_mday, 1, MAX_DAY) ||
        !is_in_range(timeptr->tm_mon, 0, MAX_TM_MON) ||
        !is_in_range(timeptr->tm_year, TM_YEAR_BASE, TM_YEAR_LAST) ||
        !is_in_range(timeptr->tm_wday, 0, MAX_WEEKDAY)) {
        LOG_ERR("The time is out of range of the RTC");
        return -EINVAL;
    }

    raw[IDX_SECOND] = bin2bcd((uint8_t)timeptr->tm_sec);
    raw[IDX_MINUTE] = bin2bcd((uint8_t)timeptr->tm_min);
    raw[IDX_HOUR] = bin2bcd((uint8_t)timeptr->tm_hour);
    raw[IDX_DAY] = bin2bcd((uint8_t)timeptr->tm_mday);
    raw[IDX_WEEKDAY] = (uint8_t)timeptr->tm_wday;
    raw[IDX_MONTH] = bin2bcd((uint8_t)(timeptr->tm_mon + 1));
    raw[IDX_YEAR] = bin2bcd((uint8_t)(timeptr->tm_year - TM_YEAR_BASE));

    err = i2c_reg_write_byte_dt(&config->i2c, PCF8563_REG_CONTROL1, 0U);
    if (err != 0) {
        LOG_ERR("Could not start the RTC (%d)", err);
        return err;
    }

    err = i2c_burst_write_dt(&config->i2c, PCF8563_REG_SECONDS, raw, sizeof(raw));
    if (err != 0) {
        LOG_ERR("Could not set the time of the RTC (%d)", err);
        return err;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief RTC の時刻を読む
 *
 * @param[in]  dev     RTC のデバイス
 * @param[out] timeptr 時刻の格納先 (tm_yday と tm_isdst は -1, tm_nsec は 0. 曜日が不正なら -1)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODATA     時刻が未設定 (電圧低下のビットが 1), または, 値が範囲外
 * @retval negative     I2C の通信の失敗 (負の errno)
 */
static int pcf8563_get_time(const struct device *dev, struct rtc_time *timeptr)
{
    const struct pcf8563_config *config = NULL; /* ドライバの設定 */
    uint8_t raw[PCF8563_TIME_BYTES] = {0};      /* 時刻のレジスタの値 (BCD) */
    int weekday = 0;                            /* 曜日 (日曜日が 0) */
    int err = EXIT_SUCCESS;                     /* エラーコード */

    config = (const struct pcf8563_config *)dev->config;

    err = i2c_burst_read_dt(&config->i2c, PCF8563_REG_SECONDS, raw, sizeof(raw));
    if (err != 0) {
        LOG_ERR("Could not read the time of the RTC (%d)", err);
        return err;
    }

    /* 電圧低下のビットが 1 のときは, 時刻が設定されていない (または, 電池が切れた) */
    if ((raw[IDX_SECOND] & PCF8563_VL_BIT) != 0U) {
        return -ENODATA;
    }

    timeptr->tm_sec = bcd2bin(raw[IDX_SECOND] & PCF8563_MASK_SECOND);
    timeptr->tm_min = bcd2bin(raw[IDX_MINUTE] & PCF8563_MASK_MINUTE);
    timeptr->tm_hour = bcd2bin(raw[IDX_HOUR] & PCF8563_MASK_HOUR);
    timeptr->tm_mday = bcd2bin(raw[IDX_DAY] & PCF8563_MASK_DAY);
    timeptr->tm_mon = bcd2bin(raw[IDX_MONTH] & PCF8563_MASK_MONTH) - 1;
    timeptr->tm_year = TM_YEAR_BASE + bcd2bin(raw[IDX_YEAR]);

    /* 範囲外の値 (BCD として不正なビットなど) は, 信用しない */
    if (!is_in_range(timeptr->tm_sec, 0, MAX_SEC_MIN) ||
        !is_in_range(timeptr->tm_min, 0, MAX_SEC_MIN) ||
        !is_in_range(timeptr->tm_hour, 0, MAX_HOUR) || !is_in_range(timeptr->tm_mday, 1, MAX_DAY) ||
        !is_in_range(timeptr->tm_mon, 0, MAX_TM_MON) ||
        !is_in_range(timeptr->tm_year, TM_YEAR_BASE, TM_YEAR_LAST)) {
        LOG_WRN("The time of the RTC is out of range");
        return -ENODATA;
    }

    weekday = (int)(raw[IDX_WEEKDAY] & PCF8563_MASK_WEEKDAY);
    timeptr->tm_wday = (is_in_range(weekday, 0, MAX_WEEKDAY) ? weekday : -1);
    timeptr->tm_yday = -1;
    timeptr->tm_isdst = -1;
    timeptr->tm_nsec = 0;

    return EXIT_SUCCESS;
}

/**
 * @brief RTC のドライバを初期化する (I2C のバスが使えることを確認する)
 *
 * RTC とは通信しない (つながっているかの確認は, 時刻を読むときに分かる).
 *
 * @param[in] dev RTC のデバイス
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      I2C のバスが利用可能でない
 */
static int pcf8563_init(const struct device *dev)
{
    const struct pcf8563_config *config = NULL; /* ドライバの設定 */

    config = (const struct pcf8563_config *)dev->config;

    if (!i2c_is_ready_dt(&config->i2c)) {
        LOG_ERR("I2C bus of the RTC is not ready");
        return -ENODEV;
    }

    return EXIT_SUCCESS;
}

/**
 * 値が範囲に入っているかを調べる (数直線の順に, min <= value <= max と書く)
 *
 * @param[in] value 調べる値
 * @param[in] min   範囲の最小 (この値を含む)
 * @param[in] max   範囲の最大 (この値を含む)
 *
 * @retval true  min 以上 max 以下
 * @retval false 範囲外
 */
static bool is_in_range(int value, int min, int max)
{
    return (min <= value) && (value <= max);
}

/**
 * @brief RTC のデバイスを定義する
 *
 * @param inst Devicetree のインスタンスの番号
 */
#define PCF8563_INIT(inst)                                                                         \
    static const struct pcf8563_config pcf8563_config_##inst = {                                   \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                                         \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(inst, pcf8563_init, NULL, NULL, &pcf8563_config_##inst, POST_KERNEL,     \
                          CONFIG_RTC_INIT_PRIORITY, &pcf8563_api)

/*
 * MISRA-C / -Wpedantic に従えない箇所: DEVICE_DT_INST_DEFINE() (Zephyr のマクロ) が,
 * 柔軟配列メンバー (nodelabels) を初期化する. このアプリのコードではなく, マクロの展開なので,
 * ここだけ警告を止める.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
DT_INST_FOREACH_STATUS_OKAY(PCF8563_INIT)
#pragma GCC diagnostic pop
