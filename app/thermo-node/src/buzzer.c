/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node のブザーの実装 (PWM. システムのワークキューで, メロディを順に鳴らす)
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <errno.h>  /* ENODEV */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "buzzer.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(buzzer_thermo_node, THERMO_LOG_LEVEL);

/** Devicetree (alias thermo-buzzer の pwms) から取得した PWM */
static const struct pwm_dt_spec buzzer = PWM_DT_SPEC_GET(DT_ALIAS(thermo_buzzer));

/** 鳴らしているメロディ (鳴らしていなければ NULL) */
static const struct buzzer_note *playing;
/** メロディの音の数 */
static size_t play_count;
/** 次に鳴らす音の位置 */
static size_t play_pos;

static void step_handler(struct k_work *work);

static int set_tone(uint16_t freq_hz);

/** メロディを 1 音ずつ進める処理 (システムのワークキューで実行する) */
static K_WORK_DELAYABLE_DEFINE(step_work, step_handler);

/**
 * @brief ブザーを初期化する (PWM のデバイスが使えるかを確かめる)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      PWM のデバイスが利用可能でない
 */
int buzzer_init(void)
{
    if (!pwm_is_ready_dt(&buzzer)) {
        LOG_ERR("PWM of the buzzer is not ready");
        return -ENODEV;
    }

    LOG_INF("Buzzer initialized");

    return EXIT_SUCCESS;
}

/**
 * @brief メロディを鳴らし始める (すぐに戻る. 鳴らすのは, システムのワークキュー)
 *
 * 鳴らしている途中で呼ぶと, 今のメロディを止めて, 新しいメロディを, 最初から鳴らす.
 * notes は, 鳴らし終わるまで, 書き換えない (static const の表を渡すこと). count が 0 のときは,
 * 鳴らしているメロディを止めて, 音を消す.
 *
 * @param[in] notes メロディの表 (周波数と長さ)
 * @param[in] count 音の数
 *
 * @retval EXIT_SUCCESS 成功
 */
int buzzer_play(const struct buzzer_note *notes, size_t count)
{
    (void)k_work_cancel_delayable(&step_work);
    playing = notes;
    play_count = count;
    play_pos = 0U;
    /* 戻り値は 0 以上 (ワークキューが止まっているときだけ, 負). 止まっていても, 鳴らせないだけ */
    (void)k_work_schedule(&step_work, K_NO_WAIT);

    return EXIT_SUCCESS;
}

/**
 * メロディを 1 音ずつ進める (次の音を鳴らして, その長さのあとに, 自分を, もう一度動かす)
 *
 * 最後の音の次は, 音を消して, 止まる. 音を鳴らせなかったときは (PWM の失敗), ログを出して,
 * メロディを止める.
 *
 * @param[in] work 使用しない
 */
static void step_handler(struct k_work *work)
{
    const struct buzzer_note *note = NULL; /* 今回の音 */
    int err = EXIT_SUCCESS;                /* エラーコード */

    ARG_UNUSED(work);

    if ((playing == NULL) || (play_pos >= play_count)) {
        (void)set_tone(0U);
        playing = NULL;
        return;
    }

    note = &playing[play_pos];
    play_pos++;
    err = set_tone(note->freq_hz);
    if (err != 0) {
        LOG_ERR("Could not play the note (%d)", err);
        playing = NULL;
        return;
    }

    (void)k_work_schedule(&step_work, K_MSEC(note->duration_ms));
}

/**
 * 音を出す (周波数 0 は, 音を消す)
 *
 * パッシブブザーは, PWM の周波数が, 音の高さになる. デューティ比は 50%.
 *
 * @param[in] freq_hz 周波数 [Hz]
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     PWM の失敗 (負の errno)
 */
static int set_tone(uint16_t freq_hz)
{
    uint32_t period_ns = 0U; /* PWM の周期 [ns] */

    if (freq_hz == 0U) {
        return pwm_set_pulse_dt(&buzzer, 0U);
    }

    period_ns = (uint32_t)(NSEC_PER_SEC / freq_hz);

    return pwm_set_dt(&buzzer, period_ns, period_ns / 2U);
}
