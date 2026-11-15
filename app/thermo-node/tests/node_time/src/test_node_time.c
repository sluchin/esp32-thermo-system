/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief node_time.c の単体テスト
 *
 * RTC のデバイスを偽のもの (vnd,test-rtc) に置き換えて次を確認する.
 *  - RTC の時刻 (rtc_time. UTC) をローカルタイム (UTC+9) に直すこと
 *    (日付と年をまたぐ繰り上がり, うるう日)
 *  - 時刻が未設定 (-ENODATA) や RTC の失敗が呼び出し元に伝わること
 *  - UNIX 時刻を rtc_time (年月日, 時分秒, 曜日) に直して RTC に設定すること
 *  - 範囲外の時刻の設定が -ERANGE になること
 */

/** 偽の RTC のドライバが使う compatible (vnd,test-rtc) */
#define DT_DRV_COMPAT vnd_test_rtc

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <errno.h>  /* ENODEV ENODATA ERANGE EIO */
#include <stdint.h> /* int64_t */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* memset */

#include "node_time.h"
#include "thermo_rtc.h"

/** 2026-10-08 05:00:00 UTC の UNIX 時刻 [s] (木曜日. ローカルタイムは 14:00:00) */
#define UNIX_2026_10_08 1791435600LL
/** 2026-12-31 20:00:00 UTC の UNIX 時刻 [s] (ローカルタイムは翌年の 2027-01-01 05:00:00) */
#define UNIX_2026_12_31 1798747200LL
/** 2028-02-29 00:00:00 UTC の UNIX 時刻 [s] (うるう日. 火曜日) */
#define UNIX_2028_02_29 1835395200LL
/** 設定できる最小の時刻 2000-01-01 00:00:00 UTC [s] (土曜日) */
#define UNIX_MIN        946684800LL
/** 設定できる最大の時刻 2099-12-31 23:59:59 UTC [s] */
#define UNIX_MAX        4102444799LL
/** 木曜日の曜日の値 (日曜日が 0) */
#define WEEKDAY_THU     4
/** 火曜日の曜日の値 (日曜日が 0) */
#define WEEKDAY_TUE     2
/** 土曜日の曜日の値 (日曜日が 0) */
#define WEEKDAY_SAT     6

/** 偽の RTC が持つ時刻 (rtc_get_time が返し, rtc_set_time が書き換える) */
static struct rtc_time stored;
/** rtc_get_time が返す値 (負は時刻を返さない. THERMO_RTC_UNVERIFIED は時刻も返す) */
static int get_ret;
/** rtc_set_time が返す値 (0 以外のときは時刻を書き換えずに, この値を返す) */
static int set_ret;
/** rtc_get_time が呼ばれた回数 */
static unsigned int get_calls;
/** rtc_set_time が呼ばれた回数 */
static unsigned int set_calls;

/**
 * 偽の RTC の時刻の読み出し
 *
 * @param[in]  dev     RTC のデバイス (使用しない)
 * @param[out] timeptr 時刻の格納先
 * @retval 0       成功
 * @retval positive THERMO_RTC_UNVERIFIED (時刻も返す)
 * @retval negative get_ret
 */
static int fake_get_time(const struct device *dev, struct rtc_time *timeptr)
{
    ARG_UNUSED(dev);
    get_calls++;
    if (get_ret < 0) {
        return get_ret;
    }
    *timeptr = stored;

    return get_ret;
}

/**
 * 偽の RTC の時刻の書き込み
 *
 * @param[in] dev     RTC のデバイス (使用しない)
 * @param[in] timeptr 書き込む時刻
 * @retval 0       成功
 * @retval nonzero set_ret
 */
static int fake_set_time(const struct device *dev, const struct rtc_time *timeptr)
{
    ARG_UNUSED(dev);
    set_calls++;
    if (set_ret != 0) {
        return set_ret;
    }
    stored = *timeptr;

    return 0;
}

/** 偽の RTC のドライバ API */
static DEVICE_API(rtc, fake_api) = {
    .set_time = fake_set_time,
    .get_time = fake_get_time,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_RTC_INIT_PRIORITY, &fake_api)

/** 偽の RTC */
static const struct device *const fake_rtc = DEVICE_DT_GET(DT_NODELABEL(test_rtc));

/**
 * 偽の RTC の時刻を設定する
 *
 * @param[in] year  年 (西暦)
 * @param[in] month 月 (1 から 12)
 * @param[in] day   日
 * @param[in] hour  時
 * @param[in] min   分
 * @param[in] sec   秒
 */
static void set_stored(int year, int month, int day, int hour, int min, int sec)
{
    stored.tm_year = year - 1900;
    stored.tm_mon = month - 1;
    stored.tm_mday = day;
    stored.tm_hour = hour;
    stored.tm_min = min;
    stored.tm_sec = sec;
}

/**
 * 各テストの前に偽の RTC を 2026-10-08 05:00:00 UTC の正常な状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    struct device_state *state = fake_rtc->state; /* RTC の状態 */

    ARG_UNUSED(fixture);
    state->initialized = true;
    (void)memset(&stored, 0, sizeof(stored));
    set_stored(2026, 10, 8, 5, 0, 0);
    get_ret = 0;
    set_ret = 0;
    get_calls = 0U;
    set_calls = 0U;
    node_time_set_floor(-1);
}

/** RTC に時刻が設定されていれば, node_time_init() は成功する */
ZTEST(node_time, test_init_success)
{
    zassert_equal(node_time_init(), EXIT_SUCCESS);
    /* 期待: 時刻を 1 回だけ読みにいく */
    zassert_equal(get_calls, 1U);
}

/** 時刻が未設定 (-ENODATA) でも RTC はあるので, node_time_init() は成功する */
ZTEST(node_time, test_init_time_unset)
{
    get_ret = -ENODATA;

    zassert_equal(node_time_init(), EXIT_SUCCESS);
}

/** 時刻が信用できなくても (電圧低下のビット), RTC はあるので, node_time_init() は成功する */
ZTEST(node_time, test_init_time_unverified)
{
    get_ret = THERMO_RTC_UNVERIFIED;

    zassert_equal(node_time_init(), EXIT_SUCCESS);
}

/** RTC のデバイスが準備できていなければ (ドライバの初期化に失敗), -ENODEV を返し, 通信しない */
ZTEST(node_time, test_init_not_ready)
{
    struct device_state *state = fake_rtc->state; /* RTC の状態 */

    state->initialized = false;
    zassert_equal(node_time_init(), -ENODEV);
    zassert_equal(get_calls, 0U);
}

/** RTC と通信できなければ (つながっていない), その値を返す */
ZTEST(node_time, test_init_rtc_failure)
{
    get_ret = -EIO;

    zassert_equal(node_time_init(), -EIO);
}

/** RTC の UTC の時刻 (05:00:00) をローカルタイム (UTC+9. 14:00:00) にして返す */
ZTEST(node_time, test_get_local_time)
{
    struct node_datetime local = {0}; /* 日時 */

    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_true(local.is_valid);
    zassert_equal(local.year, 2026U);
    zassert_equal(local.month, 10U);
    zassert_equal(local.day, 8U);
    zassert_equal(local.hour, 14U);
    zassert_equal(local.minute, 0U);
    zassert_equal(local.second, 0U);
}

/** 正常な時刻では, is_verified が true になる */
ZTEST(node_time, test_get_verified)
{
    struct node_datetime local = {0}; /* 日時 */

    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_true(local.is_verified);
}

/** 電圧低下のビットが立った妥当な時刻 (2026 年以降) は, is_verified を false にして読める */
ZTEST(node_time, test_get_unverified)
{
    struct node_datetime local = {0}; /* 日時 */

    get_ret = THERMO_RTC_UNVERIFIED;
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_true(local.is_valid);
    zassert_false(local.is_verified);
    zassert_equal(local.hour, 14U);
}

/** VL が立っていても, 時刻が最後の記録以上なら信用する (is_verified が true. 記録にも使える) */
ZTEST(node_time, test_unverified_accepted_after_last_record)
{
    struct node_datetime local = {0}; /* 日時 */
    int64_t unix_s = 0;               /* UTC の UNIX 時刻 [s] */

    get_ret = THERMO_RTC_UNVERIFIED;
    node_time_set_floor(UNIX_2026_10_08 - 60);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_true(local.is_verified);
    zassert_equal(node_time_unix(&unix_s), EXIT_SUCCESS);
    zassert_equal(unix_s, UNIX_2026_10_08);

    /* 期待: 記録の時刻とちょうど同じでも信用する. ログは 1 回だけ (基準を知らせ直すと, また出す) */
    node_time_set_floor(UNIX_2026_10_08);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_true(local.is_verified);
}

/** VL が立っていて, 時刻が最後の記録より前なら, 信用しない (OLED だけに出す). 記録がなくても同じ */
ZTEST(node_time, test_unverified_rejected_before_last_record)
{
    struct node_datetime local = {0}; /* 日時 */

    get_ret = THERMO_RTC_UNVERIFIED;
    node_time_set_floor(UNIX_2026_10_08 + 1);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_false(local.is_verified);

    node_time_set_floor(-1);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_false(local.is_verified);
}

/** 電圧低下のビットが立っていて, 2026 年より前の時刻は, 読めない (-ENODATA) */
ZTEST(node_time, test_get_unverified_implausible)
{
    struct node_datetime local = {.is_valid = true}; /* 日時 */

    get_ret = THERMO_RTC_UNVERIFIED;
    set_stored(2000, 1, 1, 0, 0, 7);
    zassert_equal(node_time_get(&local), -ENODATA);
    zassert_false(local.is_valid);
    zassert_false(local.is_verified);
}

/** UTC の 20:00 はローカルタイムでは翌日になる (年末は翌年の 1 月 1 日になる) */
ZTEST(node_time, test_get_year_rollover)
{
    struct node_datetime local = {0}; /* 日時 */

    set_stored(2026, 12, 31, 20, 30, 59);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_equal(local.year, 2027U);
    zassert_equal(local.month, 1U);
    zassert_equal(local.day, 1U);
    zassert_equal(local.hour, 5U);
    zassert_equal(local.minute, 30U);
    zassert_equal(local.second, 59U);
}

/** うるう日 (2028-02-29) を正しく読む (1 月と 2 月の計算を通す) */
ZTEST(node_time, test_get_leap_day)
{
    struct node_datetime local = {0}; /* 日時 */

    set_stored(2028, 2, 29, 0, 0, 0);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_equal(local.year, 2028U);
    zassert_equal(local.month, 2U);
    zassert_equal(local.day, 29U);
    zassert_equal(local.hour, 9U);
}

/** 時刻が未設定 (-ENODATA) なら, その値を返し, is_valid を false にする */
ZTEST(node_time, test_get_time_unset)
{
    struct node_datetime local = {.is_valid = true}; /* 日時 (is_valid を関数が false にすること) */

    get_ret = -ENODATA;
    zassert_equal(node_time_get(&local), -ENODATA);
    zassert_false(local.is_valid);
}

/** RTC と通信できなければ, その値を返し, is_valid を false にする */
ZTEST(node_time, test_get_rtc_failure)
{
    struct node_datetime local = {.is_valid = true}; /* 日時 (is_valid を関数が false にすること) */

    get_ret = -EIO;
    zassert_equal(node_time_get(&local), -EIO);
    zassert_false(local.is_valid);
}

/** UTC の UNIX 時刻で読める (RTC の時刻そのまま. ローカルタイムの換算はしない) */
ZTEST(node_time, test_unix_time)
{
    int64_t unix_s = 0; /* UTC の UNIX 時刻 [s] */

    zassert_equal(node_time_unix(&unix_s), EXIT_SUCCESS);
    zassert_equal(unix_s, UNIX_2026_10_08);
}

/** 電圧低下のビットが立った時刻は, 記録に使わないので, -ENODATA を返す */
ZTEST(node_time, test_unix_time_unverified)
{
    int64_t unix_s = 0; /* UTC の UNIX 時刻 [s] */

    get_ret = THERMO_RTC_UNVERIFIED;
    zassert_equal(node_time_unix(&unix_s), -ENODATA);
}

/** 時刻が未設定 (-ENODATA) や, RTC の失敗はその値を返す */
ZTEST(node_time, test_unix_time_failures)
{
    int64_t unix_s = 0; /* UTC の UNIX 時刻 [s] */

    get_ret = -ENODATA;
    zassert_equal(node_time_unix(&unix_s), -ENODATA);
    get_ret = -EIO;
    zassert_equal(node_time_unix(&unix_s), -EIO);
}

/** 設定のとき, RTC の時刻との差をログに出す (5 分を超えたら警告. 読めなければ出さない) */
ZTEST(node_time, test_set_logs_the_difference)
{
    /* RTC は 05:00:00 (UTC). 30 秒後と, 10 分後と, 10 分前を設定する */
    zassert_equal(node_time_set(UNIX_2026_10_08 + 30), EXIT_SUCCESS);
    set_stored(2026, 10, 8, 5, 0, 0);
    zassert_equal(node_time_set(UNIX_2026_10_08 + 600), EXIT_SUCCESS);
    set_stored(2026, 10, 8, 5, 0, 0);
    zassert_equal(node_time_set(UNIX_2026_10_08 - 600), EXIT_SUCCESS);

    /* RTC の時刻を読めなくても, 設定はできる */
    get_ret = -ENODATA;
    zassert_equal(node_time_set(UNIX_2026_10_08), EXIT_SUCCESS);
    zassert_equal(set_calls, 4U);
}

/** 時刻を設定すると, 月 (0 から 11), 年 (1900 年から), 曜日などが RTC に渡される */
ZTEST(node_time, test_set_time)
{
    zassert_equal(node_time_set(UNIX_2026_10_08 + 367LL), EXIT_SUCCESS);
    zassert_equal(set_calls, 1U);
    zassert_equal(stored.tm_year, 126);
    zassert_equal(stored.tm_mon, 9);
    zassert_equal(stored.tm_mday, 8);
    zassert_equal(stored.tm_hour, 5);
    zassert_equal(stored.tm_min, 6);
    zassert_equal(stored.tm_sec, 7);
    zassert_equal(stored.tm_wday, WEEKDAY_THU);
    zassert_equal(stored.tm_yday, -1);
    zassert_equal(stored.tm_isdst, -1);
}

/** 設定した時刻をそのまま読み戻せる (年末の繰り上がりと, うるう日も) */
ZTEST(node_time, test_set_then_get)
{
    struct node_datetime local = {0}; /* 日時 */

    zassert_equal(node_time_set(UNIX_2026_12_31), EXIT_SUCCESS);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_equal(local.year, 2027U);
    zassert_equal(local.month, 1U);
    zassert_equal(local.day, 1U);
    zassert_equal(local.hour, 5U);

    zassert_equal(node_time_set(UNIX_2028_02_29), EXIT_SUCCESS);
    zassert_equal(stored.tm_mday, 29);
    zassert_equal(stored.tm_wday, WEEKDAY_TUE);
    zassert_equal(stored.tm_mon, 1);
    zassert_equal(stored.tm_year, 128);
}

/** 設定できる最小と最大の時刻は設定できる (最大はローカルタイムで 2100 年になる) */
ZTEST(node_time, test_set_limits)
{
    struct node_datetime local = {0}; /* 日時 */

    zassert_equal(node_time_set(UNIX_MIN), EXIT_SUCCESS);
    zassert_equal(stored.tm_wday, WEEKDAY_SAT);
    zassert_equal(stored.tm_year, 100);
    zassert_equal(stored.tm_mon, 0);
    zassert_equal(stored.tm_mday, 1);

    zassert_equal(node_time_set(UNIX_MAX), EXIT_SUCCESS);
    zassert_equal(stored.tm_sec, 59);
    zassert_equal(stored.tm_mon, 11);
    zassert_equal(stored.tm_year, 199);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_equal(local.year, 2100U);
    zassert_equal(local.month, 1U);
    zassert_equal(local.day, 1U);
    zassert_equal(local.hour, 8U);
}

/** RTC が扱えない範囲の時刻は -ERANGE を返して, RTC に渡さない */
ZTEST(node_time, test_set_out_of_range)
{
    zassert_equal(node_time_set(UNIX_MIN - 1LL), -ERANGE);
    zassert_equal(node_time_set(UNIX_MAX + 1LL), -ERANGE);
    zassert_equal(set_calls, 0U);
}

/** RTC への書き込みに失敗したら, その値を返す */
ZTEST(node_time, test_set_rtc_failure)
{
    set_ret = -EIO;

    zassert_equal(node_time_set(UNIX_2026_10_08), -EIO);
    zassert_equal(set_calls, 1U);
}

ZTEST_SUITE(node_time, NULL, NULL, before, NULL, NULL);
