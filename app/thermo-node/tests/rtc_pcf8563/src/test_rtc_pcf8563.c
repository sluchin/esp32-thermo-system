/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief drivers/rtc_pcf8563.c の単体テスト
 *
 * I2C のコントローラを偽のもの (vnd,test-i2c) に置き換えて, PCF8563 のレジスタを模擬する.
 * 次を確認する.
 *  - レジスタ (BCD) を, Zephyr の rtc_time (月は 0 から 11, 年は 1900 年からの年数) に直すこと
 *    (12 月と 2099 年を含む)
 *  - 時刻が未設定 (電圧低下のビット) や, 範囲外の値は, -ENODATA にすること. 世紀のビットは無視
 *  - rtc_time を設定すると, 正しい BCD がレジスタに書かれて, 電圧低下のビットが消えること
 *  - 範囲外の時刻の設定は, 何も書かずに -EINVAL にすること. I2C の通信の失敗が伝わること
 */

/** 偽の I2C コントローラのドライバが使う compatible (vnd,test-i2c) */
#define DT_DRV_COMPAT vnd_test_i2c

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/rtc.h>
#include <errno.h>   /* ENODEV ENODATA EINVAL EIO */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* uint8_t */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* memset */

/** 偽の RTC の I2C アドレス (PCF8563) */
#define RTC_ADDR     0x51U
/** 偽の RTC のレジスタの数 [バイト] */
#define REG_COUNT    16U
/** 制御レジスタ 1 のアドレス */
#define REG_CONTROL1 0x00U
/** 秒のレジスタのアドレス */
#define REG_SECONDS  0x02U
/** 電圧低下 (VL) のビット */
#define VL_BIT       0x80U
/** 世紀のビット (月のレジスタの bit 7) */
#define CENTURY_BIT  0x80U

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

/** テスト対象の RTC のデバイス (thermo,pcf8563) */
static const struct device *const rtc_dev = DEVICE_DT_GET(DT_NODELABEL(test_rtc));

/**
 * 偽の RTC の時刻のレジスタに, 時刻を書く
 *
 * @param[in] sec   秒のレジスタ (BCD. 電圧低下のビットを含む)
 * @param[in] min   分のレジスタ (BCD)
 * @param[in] hour  時のレジスタ (BCD)
 * @param[in] day   日のレジスタ (BCD)
 * @param[in] wday  曜日のレジスタ
 * @param[in] month 月のレジスタ (BCD)
 * @param[in] year  年のレジスタ (BCD)
 */
static void set_regs(uint8_t sec, uint8_t min, uint8_t hour, uint8_t day, uint8_t wday,
                     uint8_t month, uint8_t year)
{
    regs[REG_SECONDS] = sec;
    regs[REG_SECONDS + 1U] = min;
    regs[REG_SECONDS + 2U] = hour;
    regs[REG_SECONDS + 3U] = day;
    regs[REG_SECONDS + 4U] = wday;
    regs[REG_SECONDS + 5U] = month;
    regs[REG_SECONDS + 6U] = year;
}

/**
 * 正常な設定用の時刻 (2026-10-08 05:06:07. 木曜日) を作る
 *
 * @return 時刻
 */
static struct rtc_time valid_time(void)
{
    struct rtc_time tm = {
        .tm_sec = 7,
        .tm_min = 6,
        .tm_hour = 5,
        .tm_mday = 8,
        .tm_mon = 9,
        .tm_year = 126,
        .tm_wday = 4,
        .tm_yday = -1,
        .tm_isdst = -1,
    }; /* 時刻 */

    return tm;
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
    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x04U, 0x10U, 0x26U);
    transfers = 0U;
    fail_at = 0U;
}

/** I2C のバスが使えれば, ドライバは準備できていて (RTC と通信しない), 初期化の関数も成功する */
ZTEST(rtc_pcf8563, test_init_success)
{
    zassert_true(device_is_ready(rtc_dev));
    zassert_equal(rtc_dev->ops.init(rtc_dev), EXIT_SUCCESS);
    zassert_equal(transfers, 0U);
}

/** I2C のバスが準備できていなければ (初期化されていない), -ENODEV を返す */
ZTEST(rtc_pcf8563, test_init_bus_not_ready)
{
    struct device_state *state = fake_bus->state; /* コントローラの状態 */

    state->initialized = false;
    zassert_equal(rtc_dev->ops.init(rtc_dev), -ENODEV);
}

/** レジスタの BCD を, rtc_time (月は 0 から 11, 年は 1900 年からの年数) に直して読む */
ZTEST(rtc_pcf8563, test_get_time)
{
    struct rtc_time tm = {0}; /* 時刻 */

    set_regs(0x07U, 0x06U, 0x05U, 0x08U, 0x04U, 0x10U, 0x26U);

    zassert_equal(rtc_get_time(rtc_dev, &tm), EXIT_SUCCESS);
    zassert_equal(tm.tm_sec, 7);
    zassert_equal(tm.tm_min, 6);
    zassert_equal(tm.tm_hour, 5);
    zassert_equal(tm.tm_mday, 8);
    zassert_equal(tm.tm_mon, 9); /* 10 月 */
    zassert_equal(tm.tm_year, 126);
    zassert_equal(tm.tm_wday, 4);
    zassert_equal(tm.tm_yday, -1);
    zassert_equal(tm.tm_isdst, -1);
    zassert_equal(tm.tm_nsec, 0);
}

/** 12 月 (月のレジスタ 0x12) と 2099 年 (年のレジスタ 0x99) を, 読める */
ZTEST(rtc_pcf8563, test_get_december_and_last_year)
{
    struct rtc_time tm = {0}; /* 時刻 */

    set_regs(0x59U, 0x59U, 0x23U, 0x31U, 0x04U, 0x12U, 0x99U);

    zassert_equal(rtc_get_time(rtc_dev, &tm), EXIT_SUCCESS);
    zassert_equal(tm.tm_mon, 11);
    zassert_equal(tm.tm_year, 199);
}

/** 世紀のビット (月のレジスタの bit 7) は, 無視する */
ZTEST(rtc_pcf8563, test_get_ignores_century_bit)
{
    struct rtc_time tm = {0}; /* 時刻 */

    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x04U, 0x10U | CENTURY_BIT, 0x26U);

    zassert_equal(rtc_get_time(rtc_dev, &tm), EXIT_SUCCESS);
    zassert_equal(tm.tm_mon, 9);
}

/** 曜日のレジスタが 7 (範囲外) のときは, 曜日を -1 (不明) にする */
ZTEST(rtc_pcf8563, test_get_weekday_unknown)
{
    struct rtc_time tm = {0}; /* 時刻 */

    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x07U, 0x10U, 0x26U);

    zassert_equal(rtc_get_time(rtc_dev, &tm), EXIT_SUCCESS);
    zassert_equal(tm.tm_wday, -1);
}

/** 電圧低下のビットが 1 なら (時刻が未設定), -ENODATA を返す */
ZTEST(rtc_pcf8563, test_get_time_unset)
{
    struct rtc_time tm = {0}; /* 時刻 */

    regs[REG_SECONDS] = VL_BIT;

    zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA);
}

/** 範囲外のレジスタの値は, 信用せずに -ENODATA を返す (1 つずつ, 範囲外にする) */
ZTEST(rtc_pcf8563, test_get_out_of_range)
{
    struct rtc_time tm = {0}; /* 時刻 */

    set_regs(0x60U, 0x00U, 0x05U, 0x08U, 0x04U, 0x10U, 0x26U); /* 秒 60 */
    zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA);
    set_regs(0x00U, 0x60U, 0x05U, 0x08U, 0x04U, 0x10U, 0x26U); /* 分 60 */
    zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA);
    set_regs(0x00U, 0x00U, 0x24U, 0x08U, 0x04U, 0x10U, 0x26U); /* 時 24 */
    zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x00U, 0x04U, 0x10U, 0x26U); /* 日 0 */
    zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x32U, 0x04U, 0x10U, 0x26U); /* 日 32 */
    zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x04U, 0x00U, 0x26U); /* 月 0 */
    zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x04U, 0x13U, 0x26U); /* 月 13 */
    zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA);
    set_regs(0x00U, 0x00U, 0x05U, 0x08U, 0x04U, 0x10U, 0xA0U); /* 年 100 (2100 年) */
    zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA);
}

/** RTC と通信できなければ, その値を返す */
ZTEST(rtc_pcf8563, test_get_i2c_failure)
{
    struct rtc_time tm = {0}; /* 時刻 */

    fail_at = 1U;

    zassert_equal(rtc_get_time(rtc_dev, &tm), -EIO);
}

/** 時刻を設定すると, 時計を動かして (制御レジスタ 1 が 0), 月は 1 から 12 の BCD で書かれる */
ZTEST(rtc_pcf8563, test_set_time)
{
    struct rtc_time tm = valid_time(); /* 設定する時刻 */

    (void)memset(regs, 0xFF, sizeof(regs));

    zassert_equal(rtc_set_time(rtc_dev, &tm), EXIT_SUCCESS);
    zassert_equal(regs[REG_CONTROL1], 0U);
    zassert_equal(regs[REG_SECONDS], 0x07U); /* 秒 (電圧低下のビットは 0) */
    zassert_equal(regs[REG_SECONDS + 1U], 0x06U);
    zassert_equal(regs[REG_SECONDS + 2U], 0x05U);
    zassert_equal(regs[REG_SECONDS + 3U], 0x08U);
    zassert_equal(regs[REG_SECONDS + 4U], 4U);
    zassert_equal(regs[REG_SECONDS + 5U], 0x10U); /* 10 月 (tm_mon は 9) */
    zassert_equal(regs[REG_SECONDS + 6U], 0x26U); /* 2026 年 (tm_year は 126) */
}

/** 設定できる最小 (2000-01-01) と最大 (2099-12-31) の時刻を, 設定して, 読み戻せる */
ZTEST(rtc_pcf8563, test_set_limits)
{
    struct rtc_time tm = valid_time(); /* 設定する時刻 */
    struct rtc_time back = {0};        /* 読み戻した時刻 */

    tm.tm_sec = 0;
    tm.tm_min = 0;
    tm.tm_hour = 0;
    tm.tm_mday = 1;
    tm.tm_mon = 0;
    tm.tm_year = 100;
    tm.tm_wday = 6;
    zassert_equal(rtc_set_time(rtc_dev, &tm), EXIT_SUCCESS);
    zassert_equal(regs[REG_SECONDS + 5U], 0x01U);
    zassert_equal(regs[REG_SECONDS + 6U], 0x00U);
    zassert_equal(rtc_get_time(rtc_dev, &back), EXIT_SUCCESS);
    zassert_equal(back.tm_year, 100);
    zassert_equal(back.tm_mon, 0);
    zassert_equal(back.tm_mday, 1);

    tm.tm_sec = 59;
    tm.tm_min = 59;
    tm.tm_hour = 23;
    tm.tm_mday = 31;
    tm.tm_mon = 11;
    tm.tm_year = 199;
    tm.tm_wday = 4;
    zassert_equal(rtc_set_time(rtc_dev, &tm), EXIT_SUCCESS);
    zassert_equal(regs[REG_SECONDS + 5U], 0x12U);
    zassert_equal(regs[REG_SECONDS + 6U], 0x99U);
    zassert_equal(rtc_get_time(rtc_dev, &back), EXIT_SUCCESS);
    zassert_equal(back.tm_year, 199);
    zassert_equal(back.tm_mon, 11);
    zassert_equal(back.tm_mday, 31);
    zassert_equal(back.tm_sec, 59);
}

/** 範囲外の値は, 何も書かずに -EINVAL を返す (1 つずつ, 下限の外と上限の外にする) */
ZTEST(rtc_pcf8563, test_set_out_of_range)
{
    struct rtc_time tm = valid_time(); /* 設定する時刻 */

    tm.tm_sec = -1;
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_sec = 60;
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_min = 60;
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_hour = 24;
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_mday = 0;
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_mday = 32;
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_mon = 12;
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_year = 99; /* 1999 年 */
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_year = 200; /* 2100 年 */
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_wday = -1; /* 曜日が不明 */
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);
    tm = valid_time();
    tm.tm_wday = 7;
    zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL);

    zassert_equal(transfers, 0U);
}

/** 時計を動かす書き込みに失敗したら, その値を返し, 時刻は書かない */
ZTEST(rtc_pcf8563, test_set_start_failure)
{
    struct rtc_time tm = valid_time(); /* 設定する時刻 */

    fail_at = 1U;

    zassert_equal(rtc_set_time(rtc_dev, &tm), -EIO);
    zassert_equal(transfers, 1U);
}

/** 時刻の書き込みに失敗したら, その値を返す */
ZTEST(rtc_pcf8563, test_set_write_failure)
{
    struct rtc_time tm = valid_time(); /* 設定する時刻 */

    fail_at = 2U;

    zassert_equal(rtc_set_time(rtc_dev, &tm), -EIO);
}

ZTEST_SUITE(rtc_pcf8563, NULL, NULL, before, NULL, NULL);
