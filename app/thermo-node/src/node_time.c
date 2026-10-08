/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の時刻の実装 (PCF8563 のレジスタを, I2C で直接読み書きする)
 *
 * Zephyr v4.3 の RTC ドライバ (rtc_pcf8563.c) は, 月 (0 から 11 の検証で, 12 月が通らないのに,
 * レジスタには, そのまま書く) と年 (1900 年からの年数を, そのまま BCD にする) の扱いが, チップ
 * (月は 1 から 12, 年は 00 から 99) と合っていないので, 使わない.
 */

#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/util.h>
#include <errno.h>  /* ENODEV ENODATA ERANGE */
#include <stdint.h> /* int64_t uint8_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "node_time.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(node_time_thermo_node, THERMO_LOG_LEVEL);

/** PCF8563 の, 制御レジスタ 1 のアドレス (bit 5 の STOP が 1 だと, 時計が止まる) */
#define PCF8563_REG_CONTROL1 0x00U
/** PCF8563 の, 秒のレジスタのアドレス (ここから, 秒, 分, 時, 日, 曜日, 月, 年の 7 バイト) */
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
/** RTC の年の基準 (レジスタの年 00 が, 2000 年) */
#define RTC_BASE_YEAR        2000
/** RTC が扱える最後の年 (レジスタの年 99) */
#define RTC_LAST_YEAR        2099
/** 1 分の秒数 */
#define SECONDS_PER_MINUTE   60LL
/** 1 時間の秒数 */
#define SECONDS_PER_HOUR     3600LL
/** 1 日の秒数 */
#define SECONDS_PER_DAY      86400LL
/** 1970-01-01 (UNIX 時刻の 0) の曜日 (木曜日 = 4. 日曜日が 0) */
#define EPOCH_WEEKDAY        4LL
/** 1 週間の日数 */
#define DAYS_PER_WEEK        7LL
/** 2000-01-01 00:00:00 UTC の UNIX 時刻 [s] (設定できる最小の時刻) */
#define UNIX_TIME_MIN        946684800LL
/** 2099-12-31 23:59:59 UTC の UNIX 時刻 [s] (設定できる最大の時刻) */
#define UNIX_TIME_MAX        4102444799LL
/** 0000-03-01 から 1970-01-01 までの日数 (日付の計算の基準を, 1970-01-01 に直すための値) */
#define DAYS_TO_EPOCH        719468LL
/** 400 年 (グレゴリオ暦の周期) の日数 */
#define DAYS_PER_ERA         146097LL
/** 1 年 (3 月始まり) の日数 */
#define DAYS_PER_YEAR        365LL
/** 4 年の日数の, うるう日を除いた, 4 年ごとの割り算の値 */
#define YEARS_PER_LEAP       4LL
/** 100 年ごとの, うるう日を除く割り算の値 */
#define YEARS_PER_CENTURY    100LL
/** グレゴリオ暦の周期 [年] */
#define YEARS_PER_ERA        400LL
/** 3 月始まりの, 5 か月 (3 月から 7 月) の日数 (月と日数の換算の係数) */
#define DAYS_PER_5_MONTHS    153LL

/** Devicetree (alias thermo-rtc) から取得した RTC の I2C の仕様 */
static const struct i2c_dt_spec rtc_i2c = I2C_DT_SPEC_GET(DT_ALIAS(thermo_rtc));

static int read_utc(int64_t *unix_s);

static int64_t days_from_civil(int64_t year, int64_t month, int64_t day);

static void civil_from_days(int64_t days, int64_t *year, int64_t *month, int64_t *day);

/**
 * @brief RTC を初期化する
 *
 * RTC に, I2C で, 1 回だけ, 時刻を読みにいって, つながっているかを確認する. 時刻が未設定
 * (電圧低下のビットが 1) のときも, RTC はあるので, 成功とする.
 *
 * @retval EXIT_SUCCESS 成功 (時刻が未設定でもよい)
 * @retval -ENODEV      I2C のバスが利用可能でない
 * @retval negative     RTC との通信の失敗 (負の errno)
 */
int node_time_init(void)
{
    int64_t unix_s = 0;     /* RTC の時刻 (UTC の UNIX 時刻 [s]) */
    int err = EXIT_SUCCESS; /* エラーコード */

    if (!i2c_is_ready_dt(&rtc_i2c)) {
        LOG_ERR("I2C bus of the RTC is not ready");
        return -ENODEV;
    }

    err = read_utc(&unix_s);
    if (err == -ENODATA) {
        LOG_WRN("The time of the RTC is not set");
        err = EXIT_SUCCESS;
    }
    if (err != EXIT_SUCCESS) {
        return err;
    }

    LOG_INF("RTC initialized");

    return EXIT_SUCCESS;
}

/**
 * @brief RTC の時刻を, ローカルタイムで読む
 *
 * RTC の UTC に, CONFIG_THERMO_UTC_OFFSET_MIN を足して, 年月日と時分秒に直す.
 *
 * @param[out] local 日時の格納先 (NULL 不可. 失敗したときは valid が false)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODATA     時刻が未設定, または, RTC の値が不正
 * @retval negative     RTC との通信の失敗 (負の errno)
 */
int node_time_get(struct node_datetime *local)
{
    int64_t unix_s = 0;     /* RTC の時刻 (UTC の UNIX 時刻 [s]) */
    int64_t local_s = 0;    /* ローカルタイムの UNIX 時刻 [s] */
    int64_t secs = 0;       /* その日の 0 時からの秒数 [s] */
    int64_t year = 0;       /* 年 */
    int64_t month = 0;      /* 月 */
    int64_t day = 0;        /* 日 */
    int err = EXIT_SUCCESS; /* エラーコード */

    local->valid = false;

    err = read_utc(&unix_s);
    if (err != EXIT_SUCCESS) {
        return err;
    }

    local_s = unix_s + ((int64_t)CONFIG_THERMO_UTC_OFFSET_MIN * SECONDS_PER_MINUTE);
    secs = local_s % SECONDS_PER_DAY;
    civil_from_days(local_s / SECONDS_PER_DAY, &year, &month, &day);

    local->year = (uint16_t)year;
    local->month = (uint8_t)month;
    local->day = (uint8_t)day;
    local->hour = (uint8_t)(secs / SECONDS_PER_HOUR);
    local->minute = (uint8_t)((secs % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
    local->second = (uint8_t)(secs % SECONDS_PER_MINUTE);
    local->valid = true;

    return EXIT_SUCCESS;
}

/**
 * @brief RTC に時刻を設定する
 *
 * 時計が止まっていたら動かして (制御レジスタ 1 を 0 にする), 時刻のレジスタに書く. 秒のレジスタを
 * 書くと, 電圧低下のビット (VL) が, 0 になる.
 *
 * @param[in] unix_s UTC の UNIX 時刻 [s] (2000-01-01 から 2099-12-31 まで)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ERANGE      RTC が扱えない範囲の時刻
 * @retval negative     RTC との通信の失敗 (負の errno)
 */
int node_time_set(int64_t unix_s)
{
    uint8_t raw[PCF8563_TIME_BYTES] = {0}; /* 時刻のレジスタの値 (BCD) */
    int64_t days = 0;                      /* 1970-01-01 からの日数 */
    int64_t secs = 0;                      /* その日の 0 時からの秒数 [s] */
    int64_t year = 0;                      /* 年 */
    int64_t month = 0;                     /* 月 */
    int64_t day = 0;                       /* 日 */
    int err = EXIT_SUCCESS;                /* エラーコード */

    if ((unix_s < UNIX_TIME_MIN) || (unix_s > UNIX_TIME_MAX)) {
        LOG_ERR("The time is out of range of the RTC");
        return -ERANGE;
    }

    days = unix_s / SECONDS_PER_DAY;
    secs = unix_s % SECONDS_PER_DAY;
    civil_from_days(days, &year, &month, &day);

    raw[IDX_SECOND] = bin2bcd((uint8_t)(secs % SECONDS_PER_MINUTE));
    raw[IDX_MINUTE] = bin2bcd((uint8_t)((secs % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE));
    raw[IDX_HOUR] = bin2bcd((uint8_t)(secs / SECONDS_PER_HOUR));
    raw[IDX_DAY] = bin2bcd((uint8_t)day);
    raw[IDX_WEEKDAY] = (uint8_t)((days + EPOCH_WEEKDAY) % DAYS_PER_WEEK);
    raw[IDX_MONTH] = bin2bcd((uint8_t)month);
    raw[IDX_YEAR] = bin2bcd((uint8_t)(year - RTC_BASE_YEAR));

    err = i2c_reg_write_byte_dt(&rtc_i2c, PCF8563_REG_CONTROL1, 0U);
    if (err != 0) {
        LOG_ERR("Could not start the RTC (%d)", err);
        return err;
    }

    err = i2c_burst_write_dt(&rtc_i2c, PCF8563_REG_SECONDS, raw, sizeof(raw));
    if (err != 0) {
        LOG_ERR("Could not set the time of the RTC (%d)", err);
        return err;
    }

    LOG_INF("RTC set (UNIX time %lld)", (long long)unix_s);

    return EXIT_SUCCESS;
}

/**
 * RTC の時刻を読んで, UTC の UNIX 時刻に直す
 *
 * @param[out] unix_s UTC の UNIX 時刻 [s] の格納先 (NULL 不可)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODATA     時刻が未設定 (電圧低下のビットが 1), または, 値が範囲外
 * @retval negative     I2C の通信の失敗 (負の errno)
 */
static int read_utc(int64_t *unix_s)
{
    uint8_t raw[PCF8563_TIME_BYTES] = {0}; /* 時刻のレジスタの値 (BCD) */
    int sec = 0;                           /* 秒 */
    int min = 0;                           /* 分 */
    int hour = 0;                          /* 時 */
    int day = 0;                           /* 日 */
    int month = 0;                         /* 月 */
    int year = 0;                          /* 年 */
    int err = EXIT_SUCCESS;                /* エラーコード */

    err = i2c_burst_read_dt(&rtc_i2c, PCF8563_REG_SECONDS, raw, sizeof(raw));
    if (err != 0) {
        LOG_ERR("Could not read the time of the RTC (%d)", err);
        return err;
    }

    /* 電圧低下のビットが 1 のときは, 時刻が設定されていない (または, 電池が切れた) */
    if ((raw[IDX_SECOND] & PCF8563_VL_BIT) != 0U) {
        return -ENODATA;
    }

    sec = bcd2bin(raw[IDX_SECOND] & PCF8563_MASK_SECOND);
    min = bcd2bin(raw[IDX_MINUTE] & PCF8563_MASK_MINUTE);
    hour = bcd2bin(raw[IDX_HOUR] & PCF8563_MASK_HOUR);
    day = bcd2bin(raw[IDX_DAY] & PCF8563_MASK_DAY);
    month = bcd2bin(raw[IDX_MONTH] & PCF8563_MASK_MONTH);
    year = RTC_BASE_YEAR + bcd2bin(raw[IDX_YEAR]);

    /* 範囲外の値 (BCD として不正なビットなど) は, 信用しない */
    if ((sec > 59) || (min > 59) || (hour > 23) || (day < 1) || (day > 31) || (month < 1) ||
        (month > 12) || (year > RTC_LAST_YEAR)) {
        LOG_WRN("The time of the RTC is out of range");
        return -ENODATA;
    }

    *unix_s = (days_from_civil(year, month, day) * SECONDS_PER_DAY) +
              ((int64_t)hour * SECONDS_PER_HOUR) + ((int64_t)min * SECONDS_PER_MINUTE) + sec;

    return EXIT_SUCCESS;
}

/**
 * グレゴリオ暦の日付から, 1970-01-01 からの日数を求める
 *
 * 1 月と 2 月を, 前の年の 13 月と 14 月として数える方法 (3 月始まり) で, うるう日が, 年の最後に
 * 来るようにして, 400 年ごとの周期で計算する (H. Hinnant の civil 日付のアルゴリズム).
 * 1970 年以降の日付だけを扱う. 式の中の小さな数 (2, 3, 5, 9, 12 など) は, このアルゴリズムの
 * 定数 (月の並びの規則) なので, 名前を付けていない.
 *
 * @param[in] year  年 (1970 以降)
 * @param[in] month 月 (1 から 12)
 * @param[in] day   日 (1 から 31)
 *
 * @return 1970-01-01 からの日数
 */
static int64_t days_from_civil(int64_t year, int64_t month, int64_t day)
{
    int64_t y = year - ((month <= 2) ? 1 : 0);                    /* 3 月始まりの年 */
    int64_t era = y / YEARS_PER_ERA;                              /* 400 年の周期の番号 */
    int64_t yoe = y - (era * YEARS_PER_ERA);                      /* 周期の中の年 [0, 399] */
    int64_t mp = ((month > 2) ? (month - 3) : (month + 9));       /* 3 月始まりの月 [0, 11] */
    int64_t doy = (((DAYS_PER_5_MONTHS * mp) + 2) / 5) + day - 1; /* 年の中の日 [0, 365] */
    int64_t doe = (yoe * DAYS_PER_YEAR) + (yoe / YEARS_PER_LEAP) - (yoe / YEARS_PER_CENTURY) +
                  doy; /* 周期の中の日 [0, 146096] */

    return (era * DAYS_PER_ERA) + doe - DAYS_TO_EPOCH;
}

/**
 * 1970-01-01 からの日数から, グレゴリオ暦の日付を求める (days_from_civil() の逆)
 *
 * @param[in]  days  1970-01-01 からの日数 (0 以上)
 * @param[out] year  年の格納先 (NULL 不可)
 * @param[out] month 月 (1 から 12) の格納先 (NULL 不可)
 * @param[out] day   日 (1 から 31) の格納先 (NULL 不可)
 */
static void civil_from_days(int64_t days, int64_t *year, int64_t *month, int64_t *day)
{
    int64_t z = days + DAYS_TO_EPOCH;       /* 0000-03-01 からの日数 */
    int64_t era = z / DAYS_PER_ERA;         /* 400 年の周期の番号 */
    int64_t doe = z - (era * DAYS_PER_ERA); /* 周期の中の日 [0, 146096] */
    int64_t yoe = (doe - (doe / 1460LL) + (doe / 36524LL) - (doe / 146096LL)) /
                  DAYS_PER_YEAR; /* 周期の中の年 [0, 399] */
    int64_t doy = doe - ((DAYS_PER_YEAR * yoe) + (yoe / YEARS_PER_LEAP) -
                         (yoe / YEARS_PER_CENTURY));      /* 年の中の日 [0, 365] */
    int64_t mp = ((5LL * doy) + 2LL) / DAYS_PER_5_MONTHS; /* 3 月始まりの月 [0, 11] */

    *day = doy - (((DAYS_PER_5_MONTHS * mp) + 2LL) / 5LL) + 1LL;
    *month = ((mp < 10LL) ? (mp + 3LL) : (mp - 9LL));
    *year = yoe + (era * YEARS_PER_ERA) + ((*month <= 2LL) ? 1LL : 0LL);
}
