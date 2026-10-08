/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief node_time.c の単体テスト
 *
 * I2C のコントローラを偽のもの (vnd,test-i2c) に置き換えて, PCF8563 のレジスタを模擬する.
 * 次を確認する.
 *  - レジスタ (BCD) から, UTC の時刻を読んで, ローカルタイム (UTC+9) に直すこと
 *    (日付と年をまたぐ繰り上がり, うるう日)
 *  - 時刻が未設定 (電圧低下のビット) や, 範囲外の値は, -ENODATA にすること
 *  - 時刻を設定すると, 正しい BCD がレジスタに書かれて, 電圧低下のビットが消えること
 *  - 範囲外の時刻の設定と, I2C の通信の失敗が, 呼び出し元に伝わること
 */

/** 偽の I2C コントローラのドライバが使う compatible (vnd,test-i2c) */
#define DT_DRV_COMPAT vnd_test_i2c

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <errno.h>   /* ENODEV ENODATA ERANGE EIO */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int64_t uint8_t */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* memset */

#include "node_time.h"

/** 偽の RTC の I2C アドレス (PCF8563) */
#define RTC_ADDR        0x51U
/** 偽の RTC のレジスタの数 [バイト] */
#define REG_COUNT       16U
/** 制御レジスタ 1 のアドレス */
#define REG_CONTROL1    0x00U
/** 秒のレジスタのアドレス */
#define REG_SECONDS     0x02U
/** 電圧低下 (VL) のビット */
#define VL_BIT          0x80U
/** 2026-10-08 05:00:00 UTC の UNIX 時刻 [s] (木曜日. ローカルタイムは 14:00:00) */
#define UNIX_2026_10_08 1791435600LL
/** 2026-12-31 20:00:00 UTC の UNIX 時刻 [s] (ローカルタイムは, 翌年の 2027-01-01 05:00:00) */
#define UNIX_2026_12_31 1798747200LL
/** 2028-02-29 00:00:00 UTC の UNIX 時刻 [s] (うるう日. 火曜日) */
#define UNIX_2028_02_29 1835395200LL
/** 設定できる最小の時刻 2000-01-01 00:00:00 UTC [s] (土曜日) */
#define UNIX_MIN        946684800LL
/** 設定できる最大の時刻 2099-12-31 23:59:59 UTC [s] */
#define UNIX_MAX        4102444799LL
/** 木曜日の曜日の値 (日曜日が 0) */
#define WEEKDAY_THU     4U
/** 火曜日の曜日の値 (日曜日が 0) */
#define WEEKDAY_TUE     2U
/** 土曜日の曜日の値 (日曜日が 0) */
#define WEEKDAY_SAT     6U

/** 偽の RTC のレジスタ */
static uint8_t regs[REG_COUNT];
/** 偽のコントローラが受けた転送の回数 */
static unsigned int transfers;
/** 失敗させる転送の番号 (1 始まり. 0 のときは, 失敗させない) */
static unsigned int fail_at;

/**
 * 偽の I2C コントローラの転送 (PCF8563 のレジスタの読み書きを模擬する)
 *
 * 最初の書き込みの 1 バイト目が, レジスタのアドレス. 続くバイトは, アドレスを増やしながら書く.
 * 読み出しは, 設定したアドレスから, 増やしながら読む.
 *
 * @param[in]     dev      コントローラ (使用しない)
 * @param[in,out] msgs     メッセージ
 * @param[in]     num_msgs メッセージの数
 * @param[in]     addr     デバイスのアドレス
 *
 * @retval 0    成功
 * @retval -EIO 失敗させる転送, または, アドレスが違う
 */
static int fake_transfer(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
                         uint16_t addr)
{
    unsigned int ptr = 0U; /* レジスタのアドレス */
    bool have_ptr = false; /* アドレスを設定済みか */
    uint8_t i = 0U;        /* メッセージの番号 */
    uint32_t j = 0U;       /* メッセージの中のバイトの位置 */

    ARG_UNUSED(dev);
    transfers++;
    if ((fail_at == transfers) || (addr != RTC_ADDR)) {
        return -EIO;
    }

    for (i = 0U; i < num_msgs; i++) {
        for (j = 0U; j < msgs[i].len; j++) {
            if ((msgs[i].flags & I2C_MSG_READ) != 0U) {
                msgs[i].buf[j] = regs[ptr % REG_COUNT];
                ptr++;
            } else if (!have_ptr) {
                ptr = msgs[i].buf[j];
                have_ptr = true;
            } else {
                regs[ptr % REG_COUNT] = msgs[i].buf[j];
                ptr++;
            }
        }
    }

    return 0;
}

/** 偽の I2C コントローラのドライバ API */
static DEVICE_API(i2c, fake_api) = {
    .transfer = fake_transfer,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_I2C_INIT_PRIORITY, &fake_api)

/** 偽の I2C コントローラ */
static const struct device *const fake_bus = DEVICE_DT_GET(DT_NODELABEL(test_i2c));

/**
 * 偽の RTC の時刻のレジスタに, 時刻を書く
 *
 * @param[in] sec   秒のレジスタ (BCD. 電圧低下のビットを含む)
 * @param[in] min   分のレジスタ (BCD)
 * @param[in] hour  時のレジスタ (BCD)
 * @param[in] day   日のレジスタ (BCD)
 * @param[in] month 月のレジスタ (BCD)
 * @param[in] year  年のレジスタ (BCD)
 */
static void set_regs(uint8_t sec, uint8_t min, uint8_t hour, uint8_t day, uint8_t month,
                     uint8_t year)
{
    regs[REG_SECONDS] = sec;
    regs[REG_SECONDS + 1U] = min;
    regs[REG_SECONDS + 2U] = hour;
    regs[REG_SECONDS + 3U] = day;
    regs[REG_SECONDS + 4U] = 0U;
    regs[REG_SECONDS + 5U] = month;
    regs[REG_SECONDS + 6U] = year;
}

/**
 * 各テストの前に, 偽の RTC を, 2026-10-08 05:00:00 UTC の, 正常な状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    struct device_state *state = fake_bus->state; /* コントローラの状態 */

    ARG_UNUSED(fixture);
    state->initialized = true;
    (void)memset(regs, 0, sizeof(regs));
    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x10U, 0x26U);
    transfers = 0U;
    fail_at = 0U;
}

/** RTC に時刻が設定されていれば, node_time_init() は成功する */
ZTEST(node_time, test_init_success)
{
    zassert_equal(node_time_init(), EXIT_SUCCESS);
    /* 期待: 時刻を 1 回だけ読みにいく */
    zassert_equal(transfers, 1U);
}

/** 時刻が未設定 (電圧低下のビット) でも, RTC はあるので, node_time_init() は成功する */
ZTEST(node_time, test_init_time_unset)
{
    regs[REG_SECONDS] = VL_BIT;

    zassert_equal(node_time_init(), EXIT_SUCCESS);
}

/** I2C のバスが準備できていなければ (初期化されていない), -ENODEV を返し, 通信しない */
ZTEST(node_time, test_init_not_ready)
{
    struct device_state *state = fake_bus->state; /* コントローラの状態 */

    state->initialized = false;
    zassert_equal(node_time_init(), -ENODEV);
    zassert_equal(transfers, 0U);
}

/** RTC と通信できなければ (つながっていない), その値を返す */
ZTEST(node_time, test_init_i2c_failure)
{
    fail_at = 1U;

    zassert_equal(node_time_init(), -EIO);
}

/** RTC の UTC の時刻 (05:00:00) を, ローカルタイム (UTC+9. 14:00:00) にして返す */
ZTEST(node_time, test_get_local_time)
{
    struct node_datetime local = {0}; /* 日時 */

    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_true(local.valid);
    zassert_equal(local.year, 2026U);
    zassert_equal(local.month, 10U);
    zassert_equal(local.day, 8U);
    zassert_equal(local.hour, 14U);
    zassert_equal(local.minute, 0U);
    zassert_equal(local.second, 0U);
}

/** UTC の 20:00 は, ローカルタイムでは, 翌日になる (年末は, 翌年の 1 月 1 日になる) */
ZTEST(node_time, test_get_year_rollover)
{
    struct node_datetime local = {0}; /* 日時 */

    set_regs(0x59U, 0x30U, 0x20U, 0x31U, 0x12U, 0x26U);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_equal(local.year, 2027U);
    zassert_equal(local.month, 1U);
    zassert_equal(local.day, 1U);
    zassert_equal(local.hour, 5U);
    zassert_equal(local.minute, 30U);
    zassert_equal(local.second, 59U);
}

/** うるう日 (2028-02-29) を, 正しく読む (1 月と 2 月の計算を通す) */
ZTEST(node_time, test_get_leap_day)
{
    struct node_datetime local = {0}; /* 日時 */

    set_regs(0x00U, 0x00U, 0x00U, 0x29U, 0x02U, 0x28U);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_equal(local.year, 2028U);
    zassert_equal(local.month, 2U);
    zassert_equal(local.day, 29U);
    zassert_equal(local.hour, 9U);
}

/** 電圧低下のビットが 1 なら (時刻が未設定), -ENODATA を返し, valid を false にする */
ZTEST(node_time, test_get_time_unset)
{
    struct node_datetime local = {.valid = true}; /* 日時 (valid を, 関数が false にすること) */

    regs[REG_SECONDS] = VL_BIT;
    zassert_equal(node_time_get(&local), -ENODATA);
    zassert_false(local.valid);
}

/** 範囲外のレジスタの値は, 信用せずに -ENODATA を返す (1 つずつ, 範囲外にする) */
ZTEST(node_time, test_get_out_of_range)
{
    struct node_datetime local = {0}; /* 日時 */

    set_regs(0x60U, 0x00U, 0x05U, 0x08U, 0x10U, 0x26U); /* 秒 60 */
    zassert_equal(node_time_get(&local), -ENODATA);
    set_regs(0x00U, 0x60U, 0x05U, 0x08U, 0x10U, 0x26U); /* 分 60 */
    zassert_equal(node_time_get(&local), -ENODATA);
    set_regs(0x00U, 0x00U, 0x24U, 0x08U, 0x10U, 0x26U); /* 時 24 */
    zassert_equal(node_time_get(&local), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x00U, 0x10U, 0x26U); /* 日 0 */
    zassert_equal(node_time_get(&local), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x32U, 0x10U, 0x26U); /* 日 32 */
    zassert_equal(node_time_get(&local), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x00U, 0x26U); /* 月 0 */
    zassert_equal(node_time_get(&local), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x13U, 0x26U); /* 月 13 */
    zassert_equal(node_time_get(&local), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x10U, 0xA0U); /* 年 100 (2100 年) */
    zassert_equal(node_time_get(&local), -ENODATA);
    zassert_false(local.valid);
}

/** 世紀のビット (月のレジスタの bit 7) は, 無視する */
ZTEST(node_time, test_get_ignores_century_bit)
{
    struct node_datetime local = {0}; /* 日時 */

    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x90U, 0x26U);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_equal(local.month, 10U);
}

/** RTC と通信できなければ, その値を返し, valid を false にする */
ZTEST(node_time, test_get_i2c_failure)
{
    struct node_datetime local = {.valid = true}; /* 日時 (valid を, 関数が false にすること) */

    fail_at = 1U;
    zassert_equal(node_time_get(&local), -EIO);
    zassert_false(local.valid);
}

/** 時刻を設定すると, 時計を動かして (制御レジスタ 1 が 0), BCD の時刻が書かれる */
ZTEST(node_time, test_set_time)
{
    (void)memset(regs, 0xFF, sizeof(regs));

    zassert_equal(node_time_set(UNIX_2026_10_08), EXIT_SUCCESS);
    zassert_equal(regs[REG_CONTROL1], 0U);
    zassert_equal(regs[REG_SECONDS], 0x00U); /* 秒 (電圧低下のビットは 0) */
    zassert_equal(regs[REG_SECONDS + 1U], 0x00U);
    zassert_equal(regs[REG_SECONDS + 2U], 0x05U);
    zassert_equal(regs[REG_SECONDS + 3U], 0x08U);
    zassert_equal(regs[REG_SECONDS + 4U], WEEKDAY_THU);
    zassert_equal(regs[REG_SECONDS + 5U], 0x10U);
    zassert_equal(regs[REG_SECONDS + 6U], 0x26U);
}

/** 設定した時刻を, そのまま読み戻せる (年末の繰り上がりと, うるう日も) */
ZTEST(node_time, test_set_then_get)
{
    struct node_datetime local = {0}; /* 日時 */

    regs[REG_SECONDS] = VL_BIT;
    zassert_equal(node_time_set(UNIX_2026_12_31), EXIT_SUCCESS);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_equal(local.year, 2027U);
    zassert_equal(local.month, 1U);
    zassert_equal(local.day, 1U);
    zassert_equal(local.hour, 5U);

    zassert_equal(node_time_set(UNIX_2028_02_29), EXIT_SUCCESS);
    zassert_equal(regs[REG_SECONDS + 3U], 0x29U);
    zassert_equal(regs[REG_SECONDS + 4U], WEEKDAY_TUE);
    zassert_equal(regs[REG_SECONDS + 5U], 0x02U);
    zassert_equal(regs[REG_SECONDS + 6U], 0x28U);
}

/** 設定できる最小と最大の時刻は, 設定できる (最大は, ローカルタイムで 2100 年になる) */
ZTEST(node_time, test_set_limits)
{
    struct node_datetime local = {0}; /* 日時 */

    zassert_equal(node_time_set(UNIX_MIN), EXIT_SUCCESS);
    zassert_equal(regs[REG_SECONDS + 4U], WEEKDAY_SAT);
    zassert_equal(regs[REG_SECONDS + 6U], 0x00U);

    zassert_equal(node_time_set(UNIX_MAX), EXIT_SUCCESS);
    zassert_equal(regs[REG_SECONDS], 0x59U);
    zassert_equal(regs[REG_SECONDS + 5U], 0x12U);
    zassert_equal(regs[REG_SECONDS + 6U], 0x99U);
    zassert_equal(node_time_get(&local), EXIT_SUCCESS);
    zassert_equal(local.year, 2100U);
    zassert_equal(local.month, 1U);
    zassert_equal(local.day, 1U);
    zassert_equal(local.hour, 8U);
}

/** RTC が扱えない範囲の時刻は, -ERANGE を返して, 何も書かない */
ZTEST(node_time, test_set_out_of_range)
{
    zassert_equal(node_time_set(UNIX_MIN - 1LL), -ERANGE);
    zassert_equal(node_time_set(UNIX_MAX + 1LL), -ERANGE);
    zassert_equal(transfers, 0U);
}

/** 時計を動かす書き込みに失敗したら, その値を返し, 時刻は書かない */
ZTEST(node_time, test_set_start_failure)
{
    fail_at = 1U;

    zassert_equal(node_time_set(UNIX_2026_10_08), -EIO);
    zassert_equal(transfers, 1U);
}

/** 時刻の書き込みに失敗したら, その値を返す */
ZTEST(node_time, test_set_write_failure)
{
    fail_at = 2U;

    zassert_equal(node_time_set(UNIX_2026_10_08), -EIO);
}

ZTEST_SUITE(node_time, NULL, NULL, before, NULL, NULL);
