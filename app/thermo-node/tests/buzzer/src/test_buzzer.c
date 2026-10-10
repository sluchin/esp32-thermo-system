/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief buzzer.c の単体テスト
 *
 * PWM のコントローラを偽のもの (vnd,test-pwm) に置き換えて, 次を確認する.
 *  - buzzer_init() が, PWM のデバイスの状態を返すこと
 *  - buzzer_play() が, メロディの音を, 順に, 周波数に合った周期 (デューティ比は 50%) で鳴らして,
 *    休符 (周波数 0) と最後に, 音を消すこと. 音の長さの分だけ, 次の音まで待つこと
 *  - 鳴らしている途中に, 新しいメロディを渡すと, 今のメロディを止めて, 最初から鳴らすこと
 *  - 音を鳴らせなかったときは, メロディを止めること
 *  - 音名の表 (notes.h) が, 平均律の値であること
 */

/** 偽の PWM コントローラのドライバが使う compatible (vnd,test-pwm) */
#define DT_DRV_COMPAT vnd_test_pwm

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <errno.h>  /* ENODEV EIO */
#include <stdint.h> /* uint32_t uint64_t */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* memset */

#include "buzzer.h"
#include "notes.h"

/** 記録する PWM の設定の最大の数 */
#define MAX_CALLS      16U
/** 偽の PWM の, 1 秒あたりのサイクル数 (1 GHz にして, サイクル数を, ナノ秒と同じにする) */
#define CYCLES_PER_SEC 1000000000ULL
/** メロディの 1 つ目の音の長さ [ms] */
#define NOTE1_MS       100U
/** メロディの 2 つ目 (休符) の長さ [ms] */
#define REST_MS        50U
/** メロディの 3 つ目の音の長さ [ms] */
#define NOTE3_MS       200U
/** 音を鳴らす処理が, 動くのを待つ時間 [ms] */
#define SETTLE_MS      5

/** 偽の PWM に設定された 1 回ぶん */
struct pwm_call {
    uint32_t period; /**< 周期 [サイクル = ns] */
    uint32_t pulse;  /**< パルスの幅 [サイクル = ns] */
};

/** 偽の PWM に設定された内容 (設定した順) */
static struct pwm_call calls[MAX_CALLS];
/** 偽の PWM に設定された回数 */
static unsigned int call_count;
/** 偽の PWM の set_cycles の戻り値 */
static int set_ret;

/**
 * 偽の PWM の set_cycles (周期とパルスを記録して, 設定した戻り値を返す)
 *
 * @param[in] dev    コントローラ (使用しない)
 * @param[in] ch     チャンネル (使用しない)
 * @param[in] period 周期 [サイクル]
 * @param[in] pulse  パルスの幅 [サイクル]
 * @param[in] flags  フラグ (使用しない)
 * @return set_ret
 */
static int fake_set_cycles(const struct device *dev, uint32_t ch, uint32_t period, uint32_t pulse,
                           pwm_flags_t flags)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(ch);
    ARG_UNUSED(flags);

    if (call_count < MAX_CALLS) {
        calls[call_count].period = period;
        calls[call_count].pulse = pulse;
        call_count++;
    }

    return set_ret;
}

/**
 * 偽の PWM の get_cycles_per_sec (1 GHz を返す)
 *
 * @param[in]  dev    コントローラ (使用しない)
 * @param[in]  ch     チャンネル (使用しない)
 * @param[out] cycles 1 秒あたりのサイクル数
 * @return 0
 */
static int fake_get_cycles_per_sec(const struct device *dev, uint32_t ch, uint64_t *cycles)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(ch);
    *cycles = CYCLES_PER_SEC;

    return 0;
}

/** 偽の PWM コントローラのドライバ API */
static DEVICE_API(pwm, fake_api) = {
    .set_cycles = fake_set_cycles,
    .get_cycles_per_sec = fake_get_cycles_per_sec,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_PWM_INIT_PRIORITY, &fake_api)

/** 偽の PWM コントローラ */
static const struct device *const fake_pwm = DEVICE_DT_GET(DT_NODELABEL(test_pwm));

/** テストのメロディ: 1000 Hz を 100 ms, 休符を 50 ms, 500 Hz を 200 ms */
static const struct buzzer_note melody[] = {
    {1000U, NOTE1_MS},
    {0U, REST_MS},
    {500U, NOTE3_MS},
};

/**
 * 各テストの前に, 偽の PWM を, 正常な状態に戻して, 鳴らしているメロディを止める
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    struct device_state *state = fake_pwm->state; /* コントローラの状態 */

    ARG_UNUSED(fixture);
    state->initialized = true;
    set_ret = 0;
    (void)buzzer_play(NULL, 0U);
    k_msleep(SETTLE_MS);
    (void)memset(calls, 0, sizeof(calls));
    call_count = 0U;
}

/** PWM のデバイスが準備できていれば, buzzer_init() は成功する */
ZTEST(buzzer, test_init_success)
{
    zassert_equal(buzzer_init(), EXIT_SUCCESS);
}

/** PWM のデバイスが準備できていなければ (初期化されていない), -ENODEV を返す */
ZTEST(buzzer, test_init_not_ready)
{
    struct device_state *state = fake_pwm->state; /* コントローラの状態 */

    state->initialized = false;
    zassert_equal(buzzer_init(), -ENODEV);
}

/** メロディを, 順に鳴らして, 周期は 1 秒 / 周波数, 休符で音を消し, 最後にも消す */
ZTEST(buzzer, test_play_plays_notes_in_order)
{
    zassert_equal(buzzer_play(melody, ARRAY_SIZE(melody)), EXIT_SUCCESS);

    /* 1 つ目の音: すぐに鳴る */
    k_msleep(SETTLE_MS);
    zassert_equal(call_count, 1U);
    zassert_equal(calls[0].period, 1000000U); /* 1000 Hz = 1 ms = 1000000 ns */
    zassert_equal(calls[0].pulse, 500000U);

    /* 1 つ目の音が終わったら, 休符 (音を消す) */
    k_msleep(NOTE1_MS);
    zassert_equal(call_count, 2U);
    zassert_equal(calls[1].pulse, 0U);

    /* 休符が終わったら, 3 つ目の音 (500 Hz = 2 ms) */
    k_msleep(REST_MS);
    zassert_equal(call_count, 3U);
    zassert_equal(calls[2].period, 2000000U);
    zassert_equal(calls[2].pulse, 1000000U);

    /* 3 つ目の音が終わったら, 音を消して, 止まる (それ以上は, 設定しない) */
    k_msleep(NOTE3_MS);
    zassert_equal(call_count, 4U);
    zassert_equal(calls[3].pulse, 0U);
    k_msleep(NOTE3_MS);
    zassert_equal(call_count, 4U);
}

/** 音の数が 0 のときは, 音を鳴らさずに, 音を消す */
ZTEST(buzzer, test_play_empty_melody_silences)
{
    zassert_equal(buzzer_play(melody, 0U), EXIT_SUCCESS);

    k_msleep(SETTLE_MS);
    zassert_equal(call_count, 1U);
    zassert_equal(calls[0].pulse, 0U);
}

/** 鳴らしている途中に, 新しいメロディを渡すと, 最初から鳴らす */
ZTEST(buzzer, test_play_restarts_melody)
{
    static const struct buzzer_note other[] = {{2000U, NOTE1_MS}};

    zassert_equal(buzzer_play(melody, ARRAY_SIZE(melody)), EXIT_SUCCESS);
    k_msleep(SETTLE_MS);
    zassert_equal(call_count, 1U);

    zassert_equal(buzzer_play(other, ARRAY_SIZE(other)), EXIT_SUCCESS);
    k_msleep(SETTLE_MS);
    /* 古いメロディの 2 つ目 (休符) は, 鳴らさず, 新しいメロディの 1 つ目 (2000 Hz = 0.5 ms) になる
     */
    zassert_equal(call_count, 2U);
    zassert_equal(calls[1].period, 500000U);

    k_msleep(NOTE1_MS + SETTLE_MS);
    zassert_equal(call_count, 3U);
    zassert_equal(calls[2].pulse, 0U);
}

/** 音を鳴らせなかったとき (PWM の失敗) は, メロディを止める (次の音には, 進まない) */
ZTEST(buzzer, test_play_stops_on_pwm_failure)
{
    set_ret = -EIO;

    zassert_equal(buzzer_play(melody, ARRAY_SIZE(melody)), EXIT_SUCCESS);
    k_msleep(SETTLE_MS);
    zassert_equal(call_count, 1U);

    k_msleep(NOTE1_MS + REST_MS + NOTE3_MS);
    zassert_equal(call_count, 1U);
}

/** 音名の表が, 平均律 (A4 = 440 Hz) の値で, オクターブごとに, 2 倍になること */
ZTEST(buzzer, test_notes_table)
{
    zassert_equal(NOTE_REST, 0U);
    zassert_equal(NOTE_A4, 440U);
    zassert_equal(NOTE_C4, 262U);
    zassert_equal(NOTE_CS4, 277U);
    zassert_equal(NOTE_B3, 247U);
    zassert_equal(NOTE_C5, 523U);
    zassert_equal(NOTE_C8, 4186U);
    zassert_equal(NOTE_A5, NOTE_A4 * 2U);
    zassert_equal(NOTE_A6, NOTE_A5 * 2U);
    zassert_true(NOTE_B7 < UINT16_MAX);
}

ZTEST_SUITE(buzzer, NULL, NULL, before, NULL, NULL);
