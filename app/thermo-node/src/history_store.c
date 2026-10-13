/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief グラフの履歴の, 起動のときの復元の実装
 *
 * 履歴の区切りの番号は稼働時間 / 区切りの長さ. SD カードの記録の区切りの番号は測った
 * UNIX 時刻 / 区切りの長さ. 後者から base を引くと, 前者になる (base は起動のあとに RTC に
 * 時刻が入ったときに, 1 回だけ決める. 稼働時間と RTC の時刻の差を区切りの長さで割った値.
 * 稼働時間の区切りと, 時刻の区切りがずれずに 1 対 1 に対応する).
 */

#include <zephyr/logging/log.h>
#include <errno.h>   /* ENODATA */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t int32_t int64_t */
#include <stdlib.h>  /* EXIT_SUCCESS */

#include "backlog.h"
#include "history_store.h"
#include "node_time.h"
#include "thermo_ble_uuid.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(thermo_history_store, THERMO_LOG_LEVEL);

/** ミリ秒と秒の比 */
#define MS_PER_S 1000

/** 区切りごとの平均を求めている途中の状態 (backlog_foreach_since() の ctx) */
struct restore_state {
    struct history *temp;     /**< 温度の履歴 */
    struct history *humidity; /**< 湿度の履歴 */
    int64_t step_s;           /**< 区切りの長さ [s] */
    int64_t base;             /**< 時刻の区切りの番号から, 履歴の区切りの番号にするために引く値 */
    int64_t limit;            /**< 戻す区切りの上限 (履歴の区切りの番号. これより前だけ戻す) */
    int64_t slot;             /**< 今の区切りの番号 (時刻の区切り. 記録がまだなければ -1) */
    int32_t temp_sum;         /**< 今の区切りの温度の合計 */
    int32_t humidity_sum;     /**< 今の区切りの湿度の合計 */
    unsigned int temp_n;      /**< 今の区切りの温度の数 */
    unsigned int humidity_n;  /**< 今の区切りの湿度の数 */
    unsigned int points;      /**< 戻した点の数 */
};

/** 時刻が入って, 復元を済ませたか */
static bool active;

static void visit(const struct backlog_record *rec, void *ctx);
static void flush(struct restore_state *state);

/**
 * @brief 履歴の復元を進める (測定値を履歴に加えるたびに呼ぶ)
 *
 * RTC に時刻が入るまでは何もしない. 時刻が入ったら (1 回だけ), SD カードの記録から, 直近の点を
 * 履歴に戻す. 履歴に, すでに点があるときは (時刻が入るのが遅かったとき), 戻さない.
 *
 * @param[in,out] temp     温度の履歴 (NULL 不可)
 * @param[in,out] humidity 湿度の履歴 (NULL 不可)
 * @param[in]     now_ms   今の稼働時間 [ms]
 * @param[in]     step_ms  区切りの長さ [ms] (秒単位であること)
 */
void history_store_update(struct history *temp, struct history *humidity, int64_t now_ms,
                          int64_t step_ms)
{
    struct restore_state state = {
        .temp = temp,
        .humidity = humidity,
        .step_s = step_ms / MS_PER_S,
        .slot = -1,
    }; /* 平均を求めている途中の状態 */
    int64_t unix_s = 0;     /* RTC の UNIX 時刻 [s] */
    int err = EXIT_SUCCESS; /* エラーコード */

    if (active) {
        return;
    }
    err = node_time_unix(&unix_s);
    if (err != EXIT_SUCCESS) {
        return;
    }
    active = true;
    if (temp->count > 0U) {
        return;
    }

    state.base = (unix_s - (now_ms / MS_PER_S)) / state.step_s;
    /* 今の区切り (これから測る) より前の, 直近 HISTORY_SIZE 個の区切りを戻す */
    state.limit = now_ms / step_ms;
    err = backlog_foreach_since((state.limit + state.base - (int64_t)HISTORY_SIZE) * state.step_s,
                                visit, &state);
    flush(&state);
    if (err != EXIT_SUCCESS) {
        LOG_WRN("Could not read the SD card for the graph history (err %d)", err);
    }
    LOG_INF("Restored %u graph points from the SD card", state.points);
}

/**
 * @brief 復元の状態を起動のときに戻す (RTC に時刻が入るのをまた待つ)
 *
 * 単体テストで, 再起動を模擬するために使う.
 */
void history_store_reset(void)
{
    active = false;
}

/**
 * 記録 1 件を区切りごとの平均に加える (区切りが変わったら, 前の区切りを履歴に戻す)
 *
 * @param[in]     rec 記録 (古い順)
 * @param[in,out] ctx 平均を求めている途中の状態 (struct restore_state)
 */
static void visit(const struct backlog_record *rec, void *ctx)
{
    struct restore_state *state = (struct restore_state *)ctx; /* 平均を求めている途中の状態 */
    const int64_t slot = rec->unix_s / state->step_s; /* 記録の区切りの番号 (時刻の区切り) */

    if (slot != state->slot) {
        flush(state);
        state->slot = slot;
    }
    state->temp_sum += rec->temp_x10;
    state->temp_n++;
    if (rec->humidity_x10 != THERMO_HUMIDITY_NONE) {
        state->humidity_sum += (int32_t)rec->humidity_x10;
        state->humidity_n++;
    }
}

/**
 * 今の区切りの平均を履歴に戻して, 次の区切りのために消す (今の区切りの記録がなければ, 何もしない)
 *
 * 履歴の区切りの番号にして, 今の区切り (limit) 以降の記録は戻さない.
 *
 * @param[in,out] state 平均を求めている途中の状態
 */
static void flush(struct restore_state *state)
{
    const int64_t bucket = state->slot - state->base; /* 履歴の区切りの番号 */

    if ((state->temp_n > 0U) && (bucket < state->limit)) {
        history_restore(state->temp, bucket, (int16_t)(state->temp_sum / (int32_t)state->temp_n));
        if (state->humidity_n > 0U) {
            history_restore(state->humidity, bucket,
                            (int16_t)(state->humidity_sum / (int32_t)state->humidity_n));
        }
        state->points++;
    }
    state->temp_sum = 0;
    state->humidity_sum = 0;
    state->temp_n = 0U;
    state->humidity_n = 0U;
}
