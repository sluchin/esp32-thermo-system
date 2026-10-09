/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の時刻の実装 (Zephyr の RTC API で, 拡張ボードの PCF8563 を読み書きする)
 *
 * RTC のデバイスは, Devicetree の alias thermo-rtc で選ぶ. PCF8563 のドライバは, このアプリの
 * drivers/rtc_pcf8563.c (Zephyr v4.3 のドライバは, 月と年の扱いがチップと合わないので使わない).
 * このファイルは, RTC の時刻 (UTC) と UNIX 時刻の換算と, ローカルタイムへの変換を受け持つ.
 */

#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <errno.h>  /* ENODEV ENODATA ERANGE */
#include <stdint.h> /* int64_t uint8_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "node_time.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(node_time_thermo_node, THERMO_LOG_LEVEL);

/** rtc_time の tm_year の基準 (tm_year は, 1900 年からの年数) */
#define TM_YEAR_BASE       1900
/** 1 分の秒数 */
#define SECONDS_PER_MINUTE 60LL
/** 1 時間の秒数 */
#define SECONDS_PER_HOUR   3600LL
/** 1 日の秒数 */
#define SECONDS_PER_DAY    86400LL
/** 1970-01-01 (UNIX 時刻の 0) の曜日 (木曜日 = 4. 日曜日が 0) */
#define EPOCH_WEEKDAY      4LL
/** 1 週間の日数 */
#define DAYS_PER_WEEK      7LL
/** 2000-01-01 00:00:00 UTC の UNIX 時刻 [s] (設定できる最小の時刻) */
#define UNIX_TIME_MIN      946684800LL
/** 2099-12-31 23:59:59 UTC の UNIX 時刻 [s] (設定できる最大の時刻) */
#define UNIX_TIME_MAX      4102444799LL
/** 0000-03-01 から 1970-01-01 までの日数 (日付の計算の基準を, 1970-01-01 に直すための値) */
#define DAYS_TO_EPOCH      719468LL
/** 400 年 (グレゴリオ暦の周期) の日数 */
#define DAYS_PER_ERA       146097LL
/** 1 年 (3 月始まり) の日数 */
#define DAYS_PER_YEAR      365LL
/** 4 年の日数の, うるう日を除いた, 4 年ごとの割り算の値 */
#define YEARS_PER_LEAP     4LL
/** 100 年ごとの, うるう日を除く割り算の値 */
#define YEARS_PER_CENTURY  100LL
/** グレゴリオ暦の周期 [年] */
#define YEARS_PER_ERA      400LL
/** 3 月始まりの, 5 か月 (3 月から 7 月) の日数 (月と日数の換算の係数) */
#define DAYS_PER_5_MONTHS  153LL

/** Devicetree (alias thermo-rtc) から取得した RTC のデバイス */
static const struct device *const rtc_dev = DEVICE_DT_GET(DT_ALIAS(thermo_rtc));

static int read_utc(int64_t *unix_s);

static int64_t days_from_civil(int64_t year, int64_t month, int64_t day);

static void civil_from_days(int64_t days, int64_t *year, int64_t *month, int64_t *day);

/**
 * @brief RTC を初期化する
 *
 * RTC の時刻を, 1 回だけ読みにいって, つながっているかを確認する. 時刻が未設定
 * (電圧低下のビットが 1) のときも, RTC はあるので, 成功とする.
 *
 * @retval EXIT_SUCCESS 成功 (時刻が未設定でもよい)
 * @retval -ENODEV      RTC のデバイスが利用可能でない (ドライバの初期化に失敗した)
 * @retval negative     RTC との通信の失敗 (負の errno)
 */
int node_time_init(void)
{
    int64_t unix_s = 0;     /* RTC の時刻 (UTC の UNIX 時刻 [s]) */
    int err = EXIT_SUCCESS; /* エラーコード */

    if (!device_is_ready(rtc_dev)) {
        LOG_ERR("RTC is not ready");
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
 * UNIX 時刻を, 年月日と時分秒と曜日に直して, RTC のドライバ (rtc_set_time) に渡す.
 *
 * @param[in] unix_s UTC の UNIX 時刻 [s] (2000-01-01 から 2099-12-31 まで)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ERANGE      RTC が扱えない範囲の時刻
 * @retval negative     RTC のドライバの失敗 (負の errno)
 */
int node_time_set(int64_t unix_s)
{
    struct rtc_time tm = {0}; /* RTC に設定する時刻 */
    int64_t days = 0;         /* 1970-01-01 からの日数 */
    int64_t secs = 0;         /* その日の 0 時からの秒数 [s] */
    int64_t year = 0;         /* 年 */
    int64_t month = 0;        /* 月 */
    int64_t day = 0;          /* 日 */
    int err = EXIT_SUCCESS;   /* エラーコード */

    if ((unix_s < UNIX_TIME_MIN) || (unix_s > UNIX_TIME_MAX)) {
        LOG_ERR("The time is out of range of the RTC");
        return -ERANGE;
    }

    days = unix_s / SECONDS_PER_DAY;
    secs = unix_s % SECONDS_PER_DAY;
    civil_from_days(days, &year, &month, &day);

    tm.tm_sec = (int)(secs % SECONDS_PER_MINUTE);
    tm.tm_min = (int)((secs % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
    tm.tm_hour = (int)(secs / SECONDS_PER_HOUR);
    tm.tm_mday = (int)day;
    tm.tm_mon = (int)month - 1;
    tm.tm_year = (int)year - TM_YEAR_BASE;
    tm.tm_wday = (int)((days + EPOCH_WEEKDAY) % DAYS_PER_WEEK);
    tm.tm_yday = -1;
    tm.tm_isdst = -1;

    err = rtc_set_time(rtc_dev, &tm);
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
 * @retval -ENODATA     時刻が未設定 (ドライバが返す)
 * @retval negative     RTC のドライバの失敗 (負の errno)
 */
static int read_utc(int64_t *unix_s)
{
    struct rtc_time tm = {0}; /* RTC の時刻 */
    int err = EXIT_SUCCESS;   /* エラーコード */

    err = rtc_get_time(rtc_dev, &tm);
    if (err != 0) {
        if (err != -ENODATA) {
            LOG_ERR("Could not read the time of the RTC (%d)", err);
        }
        return err;
    }

    *unix_s = (days_from_civil((int64_t)tm.tm_year + TM_YEAR_BASE, (int64_t)tm.tm_mon + 1,
                               tm.tm_mday) *
               SECONDS_PER_DAY) +
              ((int64_t)tm.tm_hour * SECONDS_PER_HOUR) + ((int64_t)tm.tm_min * SECONDS_PER_MINUTE) +
              tm.tm_sec;

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
