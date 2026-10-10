/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief main.c の単体テスト
 *
 * sensor と ble と node_time と oled の関数を, FFF のモックに置き換えて, 次を確認する.
 *  - 初期化に失敗したら, 後の処理に進まず, EXIT_FAILURE を返すこと
 *  - 初期化に成功したら, 一定間隔で温度を読み取り続けること (別スレッドで main を動かす)
 *  - OLED の表示と時刻を, 1 秒ごとに更新すること (グラフの履歴に, 測定値を加えること)
 *  - ボタンを押すと, ページを切り替えて (最後の次は, 最初に戻る), すぐに表示し直すこと
 *  - 起動のときに, 起動音 (メロディ) を始めて, 起動画面を, 全てのフレームで, 順に表示すること
 * (失敗しても続ける)
 *  - OLED や RTC の初期化に失敗しても, 表示と時刻を諦めるだけで, 測定と通知を続けること
 *  - 初期化に失敗した機器を, 一定間隔でやり直して, 復帰すること (成功した機器は, やり直さない)
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <errno.h>   /* ENODEV EIO ENOMEM ENODATA */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t */
#include <stdlib.h>  /* EXIT_FAILURE */
#include <string.h>  /* memset */

#include "ble.h"
#include "boot_assets.h"
#include "button.h"
#include "buzzer.h"
#include "history.h"
#include "node_time.h"
#include "oled.h"
#include "sensor.h"
#include "thermo_ble_uuid.h"

DEFINE_FFF_GLOBALS

/**
 * main.c の main() (CMakeLists.txt で名前を変えている)
 *
 * @return main.c の main() の戻り値
 */
int thermo_node_main(void);

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, sensor_init)
FAKE_VALUE_FUNC(int, sensor_read, int16_t *, uint16_t *)
FAKE_VALUE_FUNC(int, ble_init)
FAKE_VALUE_FUNC(int, ble_advertise)
FAKE_VALUE_FUNC(int, ble_notify_temperature, int16_t, uint16_t)
FAKE_VOID_FUNC(ble_set_time_callback, ble_time_cb_t)
FAKE_VALUE_FUNC(int, node_time_set, int64_t)
FAKE_VALUE_FUNC(int, button_init)
FAKE_VALUE_FUNC(bool, button_pressed)
FAKE_VOID_FUNC(history_add, struct history *, int16_t, int64_t, int64_t)
FAKE_VALUE_FUNC(int, oled_init)
FAKE_VALUE_FUNC(int, oled_show_boot, unsigned int)
FAKE_VALUE_FUNC(int, buzzer_init)
FAKE_VALUE_FUNC(int, buzzer_play, const struct buzzer_note *, size_t)
FAKE_VALUE_FUNC(int, oled_show, const struct oled_view *)
FAKE_VALUE_FUNC(int, node_time_init)
FAKE_VALUE_FUNC(int, node_time_get, struct node_datetime *)

/** oled_show() の呼び出しを記録する数 */
#define SHOWN_MAX 16U

/** モックの日時の年 */
#define FAKE_YEAR   2026U
/** モックの日時の月 */
#define FAKE_MONTH  10U
/** モックの日時の日 */
#define FAKE_DAY    8U
/** モックの日時の時 */
#define FAKE_HOUR   12U
/** モックの日時の分 */
#define FAKE_MINUTE 34U
/** モックの日時の秒 */
#define FAKE_SECOND 56U

/** メインループのスレッドのスタックサイズ */
#define STACK_SIZE        2048
/** メインループのスレッドの優先度 */
#define THREAD_PRIORITY   5
/** main.c の読み取り間隔 [秒] (SAMPLE_INTERVAL_S) */
#define SAMPLE_INTERVAL_S 5
/** 起動画面の長さ [ms] (フレームの数 x (1 フレームの時間 + ティックの丸め 10 ms)) */
#define BOOT_MS           (OLED_BOOT_FRAMES * (OLED_BOOT_FRAME_MS + 10U))
/** 起動画面のあと, 初回の読み取りを待つ時間 [ms] */
#define STARTUP_WAIT_MS   (BOOT_MS + 100)
/** モックの 1 回目の温度 [℃ の 10 倍] (-3.5 ℃. 負の温度の出力を通す) */
#define FAKE_TEMP_1_X10   (-35)
/** モックの 1 回目の湿度 [% の 10 倍] (45.0 %) */
#define FAKE_HUMIDITY_1   450U
/** モックの 2 回目以降の温度 [℃ の 10 倍] (23.5 ℃) */
#define FAKE_TEMP_2_X10   235

/** ボタンを押したことにする呼び出しの数の上限 */
#define PRESS_COUNT 3U

/** 押されたことにする button_pressed() の呼び出しの回数 (0 は, 使わない) */
static unsigned int press_at[PRESS_COUNT];

static bool fake_pressed(void);

/** oled_show() に渡された表示内容 (呼ばれた順) */
static struct oled_view shown[SHOWN_MAX];
/** oled_show() に渡された日時 (呼ばれた順. 表示内容の time が NULL のときは, 使わない) */
static struct node_datetime shown_time[SHOWN_MAX];

/** main を動かすスレッド */
static struct k_thread main_thread;
/** main を動かすスレッドのスタック */
static K_THREAD_STACK_DEFINE(main_stack, STACK_SIZE)

/**
 * sensor_read() のモック動作 (値を書き込む)
 *
 * 1 回目は, 負の温度と湿度. 2 回目以降は, 0 ℃ 以上の温度と, 湿度なし (ログの出力の分岐を通す).
 *
 * @param[out] temp_x10     温度 [℃ の 10 倍]
 * @param[out] humidity_x10 湿度 [% の 10 倍]
 * @return 0
 */
static int fake_read(int16_t *temp_x10, uint16_t *humidity_x10)
{
    if (sensor_read_fake.call_count == 1U) {
        *temp_x10 = (int16_t)FAKE_TEMP_1_X10;
        *humidity_x10 = (uint16_t)FAKE_HUMIDITY_1;
    } else {
        *temp_x10 = (int16_t)FAKE_TEMP_2_X10;
        *humidity_x10 = THERMO_HUMIDITY_NONE;
    }

    return 0;
}

/**
 * oled_show() のモック動作 (渡された表示内容と日時を記録する)
 *
 * @param[in] view 表示内容
 * @return 0
 */
static int fake_show(const struct oled_view *view)
{
    unsigned int idx = oled_show_fake.call_count - 1U; /* 今回の呼び出しの番号 (0 始まり) */

    if (idx < SHOWN_MAX) {
        shown[idx] = *view;
        if (view->time != NULL) {
            shown_time[idx] = *view->time;
        }
    }

    return 0;
}

/**
 * node_time_get() のモック動作 (2026-10-08 12:34:56 を返す)
 *
 * @param[out] local 日時
 * @return 0
 */
static int fake_time_get(struct node_datetime *local)
{
    local->valid = true;
    local->year = FAKE_YEAR;
    local->month = FAKE_MONTH;
    local->day = FAKE_DAY;
    local->hour = FAKE_HOUR;
    local->minute = FAKE_MINUTE;
    local->second = FAKE_SECOND;

    return 0;
}

/**
 * main を動かすスレッドの入口
 *
 * @param[in] p1 使用しない
 * @param[in] p2 使用しない
 * @param[in] p3 使用しない
 */
static void main_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    (void)thermo_node_main();
}

/**
 * 各テストの前にモックを初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(sensor_init);
    RESET_FAKE(sensor_read);
    RESET_FAKE(ble_init);
    RESET_FAKE(ble_advertise);
    RESET_FAKE(ble_notify_temperature);
    RESET_FAKE(ble_set_time_callback);
    RESET_FAKE(node_time_set);
    RESET_FAKE(button_init);
    RESET_FAKE(button_pressed);
    RESET_FAKE(history_add);
    RESET_FAKE(oled_init);
    RESET_FAKE(oled_show_boot);
    RESET_FAKE(buzzer_init);
    RESET_FAKE(buzzer_play);
    RESET_FAKE(oled_show);
    RESET_FAKE(node_time_init);
    RESET_FAKE(node_time_get);
    FFF_RESET_HISTORY();
    oled_show_fake.custom_fake = fake_show;
    node_time_get_fake.custom_fake = fake_time_get;
    (void)memset(press_at, 0, sizeof(press_at));
    button_pressed_fake.custom_fake = fake_pressed;
    (void)memset(shown, 0, sizeof(shown));
    (void)memset(shown_time, 0, sizeof(shown_time));
}

/** センサの初期化に失敗したら, BLE には進まず, EXIT_FAILURE を返す */
ZTEST(main_node, test_sensor_init_failure)
{
    sensor_init_fake.return_val = -ENODEV;

    /* 期待: センサの初期化に失敗したら, BLE には進まない */
    zassert_equal(thermo_node_main(), EXIT_FAILURE);
    zassert_equal(sensor_init_fake.call_count, 1U);
    zassert_equal(ble_init_fake.call_count, 0U);
    zassert_equal(ble_advertise_fake.call_count, 0U);
}

/** BLE の初期化に失敗したら, アドバタイズには進まず, EXIT_FAILURE を返す */
ZTEST(main_node, test_ble_init_failure)
{
    ble_init_fake.return_val = -EIO;

    /* 期待: BLE の初期化に失敗したら, アドバタイズにも読み取りにも進まない */
    zassert_equal(thermo_node_main(), EXIT_FAILURE);
    zassert_equal(sensor_init_fake.call_count, 1U);
    zassert_equal(ble_init_fake.call_count, 1U);
    zassert_equal(ble_advertise_fake.call_count, 0U);
    zassert_equal(sensor_read_fake.call_count, 0U);
}

/** アドバタイズの開始に失敗したら, 温度は読まず, EXIT_FAILURE を返す */
ZTEST(main_node, test_advertise_failure)
{
    ble_advertise_fake.return_val = -ENOMEM;

    /* 期待: アドバタイズの失敗で止まる (温度は読まない) */
    zassert_equal(thermo_node_main(), EXIT_FAILURE);
    zassert_equal(ble_advertise_fake.call_count, 1U);
    zassert_equal(sensor_read_fake.call_count, 0U);
    /* 時刻を受け取るコールバックは, アドバタイズの開始の前に, 登録する */
    zassert_equal(ble_set_time_callback_fake.call_count, 1U);
    /* RTC と OLED は, アドバタイズの開始のあとに, 初期化する */
    zassert_equal(node_time_init_fake.call_count, 0U);
    zassert_equal(oled_init_fake.call_count, 0U);
}

/** 初期化に成功したら, 初期化を 1 回ずつ行い, 一定間隔で温度を読み続ける */
ZTEST(main_node, test_main_loop)
{
    sensor_read_fake.custom_fake = fake_read;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    /* 起動直後: 初期化を 1 回ずつ行い, 1 回目の温度を読む */
    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(sensor_init_fake.call_count, 1U);
    zassert_equal(ble_init_fake.call_count, 1U);
    zassert_equal(ble_advertise_fake.call_count, 1U);
    zassert_equal(sensor_read_fake.call_count, 1U);
    zassert_equal(node_time_init_fake.call_count, 1U);
    zassert_equal(oled_init_fake.call_count, 1U);

    /* 読み取った温度と湿度と, RTC の日時を, OLED に表示する */
    zassert_equal(oled_show_fake.call_count, 1U);
    zassert_true(shown[0].has_sample);
    zassert_equal(shown[0].temp_x10, FAKE_TEMP_1_X10);
    zassert_equal(shown[0].humidity_x10, FAKE_HUMIDITY_1);
    zassert_not_null(shown[0].time);
    zassert_true(shown_time[0].valid);
    zassert_equal(shown_time[0].hour, FAKE_HOUR);

    /* 測定値を, グラフの履歴に加える (温度と湿度. 区切りの長さは, 30 秒) */
    zassert_equal(button_init_fake.call_count, 1U);
    zassert_equal(history_add_fake.call_count, 2U);
    zassert_equal(history_add_fake.arg1_history[0], FAKE_TEMP_1_X10);
    /* 稼働時間は, テストの開始から進んでいるので, 1 回目の測定の時間との差で調べる */
    zassert_equal(history_add_fake.arg2_history[1], history_add_fake.arg2_history[0]);
    zassert_equal(history_add_fake.arg3_history[0], 30000);
    zassert_equal(history_add_fake.arg1_history[1], (int16_t)FAKE_HUMIDITY_1);
    zassert_not_null(shown[0].temp_history);
    zassert_not_null(shown[0].humidity_history);
    zassert_not_equal(shown[0].temp_history, shown[0].humidity_history);

    /* 読み取った温度を通知する */
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    zassert_equal(ble_notify_temperature_fake.arg0_history[0], FAKE_TEMP_1_X10);
    zassert_equal(ble_notify_temperature_fake.arg1_history[0], FAKE_HUMIDITY_1);

    /* 読み取り間隔だけ待つと, 2 回目の温度を読んで通知する (初期化は繰り返さない) */
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(sensor_read_fake.call_count, 2U);
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);
    zassert_equal(ble_notify_temperature_fake.arg0_history[1], FAKE_TEMP_2_X10);
    zassert_equal(ble_notify_temperature_fake.arg1_history[1], THERMO_HUMIDITY_NONE);
    /* OLED は, 1 秒ごとに更新する (100 ms + 5 s の間に, 0 s から 5 s の 6 回) */
    zassert_equal(oled_show_fake.call_count, 6U);
    zassert_equal(node_time_get_fake.call_count, 6U);
    zassert_equal(shown[5].temp_x10, FAKE_TEMP_2_X10);
    zassert_equal(shown[5].humidity_x10, THERMO_HUMIDITY_NONE);
    /* 2 回目は, 湿度がないので, 温度だけを履歴に加える */
    zassert_equal(history_add_fake.call_count, 3U);
    zassert_equal(history_add_fake.arg1_history[2], FAKE_TEMP_2_X10);
    /* 測定の間隔は, 5 秒の格子に合わせる (待ちの丸めで, 格子の直後 (100 ms 以内) になる) */
    zassert_within(history_add_fake.arg2_history[2] - history_add_fake.arg2_history[0],
                   SAMPLE_INTERVAL_S * 1000, 100);
    zassert_equal(sensor_init_fake.call_count, 1U);
    zassert_equal(node_time_init_fake.call_count, 1U);
    zassert_equal(oled_init_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

/** 通知に失敗しても次の周期で, 再び温度を読んで通知する */
ZTEST(main_node, test_notify_failure_retries)
{
    sensor_read_fake.custom_fake = fake_read;
    ble_notify_temperature_fake.return_val = -EIO;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    /* 1 回目: 通知に失敗する */
    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    /* 失敗しても止まらず, 次の周期で再び読み取って, 通知する */
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(sensor_read_fake.call_count, 2U);
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);

    k_thread_abort(&main_thread);
}

/** 温度の読み取りに失敗しても次の周期で, 再び読み取る */
ZTEST(main_node, test_read_failure_retries)
{
    sensor_read_fake.return_val = -EIO;

    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(sensor_read_fake.call_count, 1U);
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(sensor_read_fake.call_count, 2U);

    /* 読み取りに失敗したときは, 通知しない */
    zassert_equal(ble_notify_temperature_fake.call_count, 0U);

    k_thread_abort(&main_thread);
}

/** ゲートウェイから受け取った時刻を RTC に設定する関数 (node_time_set) を, BLE に登録する */
ZTEST(main_node, test_time_callback_registered)
{
    /* アドバタイズの開始で止めて (戻ってこない無限ループに入らず), 登録だけを確かめる */
    ble_advertise_fake.return_val = -ENOMEM;

    zassert_equal(thermo_node_main(), EXIT_FAILURE);
    zassert_equal(ble_set_time_callback_fake.call_count, 1U);
    zassert_equal(ble_set_time_callback_fake.arg0_val, node_time_set);
}

/**
 * button_pressed() のモック動作 (設定した回数目の呼び出しで, 押されたことにする)
 *
 * @return 押された呼び出しなら true
 */
static bool fake_pressed(void)
{
    unsigned int call = button_pressed_fake.call_count; /* 今回の呼び出しの回数 (1 始まり) */
    unsigned int i = 0U;                                /* ループ用の添字 */

    for (i = 0U; i < PRESS_COUNT; i++) {
        if (call == press_at[i]) {
            return true;
        }
    }

    return false;
}

/**
 * 別スレッドで main を動かす
 */
static void start_main(void)
{
    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
}

/** 測定の間隔 (5 秒) の間も, OLED の表示と時刻を, 1 秒ごとに更新する (測定は 1 回だけ) */
ZTEST(main_node, test_display_updates_every_second)
{
    sensor_read_fake.custom_fake = fake_read;

    start_main();

    /* 0 s, 1 s, 2 s, 3 s の 4 回表示する */
    k_msleep(STARTUP_WAIT_MS);
    k_sleep(K_SECONDS(3));
    zassert_equal(oled_show_fake.call_count, 4U);
    zassert_equal(sensor_read_fake.call_count, 1U);
    /* 測定していない間は, 前回の値を表示し続ける */
    zassert_true(shown[3].has_sample);
    zassert_equal(shown[3].temp_x10, FAKE_TEMP_1_X10);

    k_thread_abort(&main_thread);
}

/** 温度の読み取りに失敗している間は, 測定値なしで表示する */
ZTEST(main_node, test_display_without_sample)
{
    sensor_read_fake.return_val = -EIO;

    start_main();

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(oled_show_fake.call_count, 1U);
    zassert_false(shown[0].has_sample);

    k_thread_abort(&main_thread);
}

/** OLED の初期化に失敗しても, 表示だけを諦めて, 測定と通知を続ける */
ZTEST(main_node, test_oled_init_failure_continues)
{
    sensor_read_fake.custom_fake = fake_read;
    oled_init_fake.return_val = -ENODEV;

    start_main();

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(oled_init_fake.call_count, 1U);
    zassert_equal(sensor_read_fake.call_count, 1U);
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);

    /* 初期化に失敗した OLED には, 表示しない */
    zassert_equal(oled_show_fake.call_count, 0U);

    k_thread_abort(&main_thread);
}

/** OLED の表示に失敗しても, 通知を続けて, 次の周期で再び表示する */
ZTEST(main_node, test_oled_show_failure_retries)
{
    sensor_read_fake.custom_fake = fake_read;
    oled_show_fake.custom_fake = NULL; /* 戻り値 (return_val) を使うため, 記録の動作を外す */
    oled_show_fake.return_val = -EIO;

    start_main();

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(oled_show_fake.call_count, 1U);
    /* 表示に失敗しても, 通知する */
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);
    k_sleep(K_SECONDS(SAMPLE_INTERVAL_S));
    zassert_equal(oled_show_fake.call_count, 6U);
    zassert_equal(ble_notify_temperature_fake.call_count, 2U);

    k_thread_abort(&main_thread);
}

/** RTC の初期化に失敗したら, 時刻を読まず, 日付と時刻なしで表示して, 測定と通知を続ける */
ZTEST(main_node, test_rtc_init_failure_continues)
{
    sensor_read_fake.custom_fake = fake_read;
    node_time_init_fake.return_val = -EIO;

    start_main();

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(node_time_init_fake.call_count, 1U);
    zassert_equal(node_time_get_fake.call_count, 0U);
    zassert_equal(oled_show_fake.call_count, 1U);
    zassert_is_null(shown[0].time);
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

/** 時刻が未設定 (-ENODATA) のときは, 日時を, 設定なし (valid が false) として表示する */
ZTEST(main_node, test_time_unset)
{
    node_time_get_fake.custom_fake = NULL;
    node_time_get_fake.return_val = -ENODATA;

    start_main();

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(node_time_get_fake.call_count, 1U);
    zassert_equal(oled_show_fake.call_count, 1U);
    zassert_not_null(shown[0].time);
    zassert_false(shown_time[0].valid);

    k_thread_abort(&main_thread);
}

/** ボタンを押すと, 次のページにして, 1 秒を待たずに表示し直す */
ZTEST(main_node, test_button_switches_page)
{
    sensor_read_fake.custom_fake = fake_read;
    press_at[0] = 3U; /* 3 回目の呼び出し (100 ms) で押される */

    start_main();

    k_msleep(STARTUP_WAIT_MS + 60);
    zassert_equal(oled_show_fake.call_count, 2U);
    zassert_equal(shown[0].page, OLED_PAGE_NOW);
    zassert_equal(shown[1].page, OLED_PAGE_TEMP_GRAPH);

    k_thread_abort(&main_thread);
}

/** ボタンを続けて押すと, ページが順に進み, 最後の次は, 最初に戻る */
ZTEST(main_node, test_button_page_wraps_around)
{
    sensor_read_fake.custom_fake = fake_read;
    press_at[0] = 2U;
    press_at[1] = 4U;
    press_at[2] = 6U;

    start_main();

    k_msleep(STARTUP_WAIT_MS + 300);
    zassert_equal(shown[1].page, OLED_PAGE_TEMP_GRAPH);
    zassert_equal(shown[2].page, OLED_PAGE_HUMIDITY_GRAPH);
    zassert_equal(shown[3].page, OLED_PAGE_NOW);

    k_thread_abort(&main_thread);
}

/** ボタンの初期化に失敗したら, ボタンは調べず (ページは切り替えず), 測定と表示は続ける */
ZTEST(main_node, test_button_init_failure_continues)
{
    sensor_read_fake.custom_fake = fake_read;
    button_init_fake.return_val = -ENODEV;

    start_main();

    k_msleep(STARTUP_WAIT_MS + 300);
    zassert_equal(button_init_fake.call_count, 1U);
    zassert_equal(button_pressed_fake.call_count, 0U);
    zassert_equal(oled_show_fake.call_count, 1U);
    zassert_equal(shown[0].page, OLED_PAGE_NOW);
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

/** main.c の RETRY_INTERVAL_S (初期化をやり直す間隔 [秒]) */
#define RETRY_INTERVAL_S 60

/** 起動のときに初期化に失敗した RTC と OLED とボタンは, 60 秒後にやり直して, 使えるようになる */
ZTEST(main_node, test_optional_devices_recover_by_retry)
{
    static int fail_then_ok[] = {-EIO, 0};

    sensor_read_fake.custom_fake = fake_read;
    SET_RETURN_SEQ(node_time_init, fail_then_ok, ARRAY_SIZE(fail_then_ok));
    SET_RETURN_SEQ(oled_init, fail_then_ok, ARRAY_SIZE(fail_then_ok));
    SET_RETURN_SEQ(button_init, fail_then_ok, ARRAY_SIZE(fail_then_ok));
    SET_RETURN_SEQ(buzzer_init, fail_then_ok, ARRAY_SIZE(fail_then_ok));

    start_main();

    /* 起動のとき: 3 つとも失敗して, 時刻も, 表示も, ボタンもない. 測定と通知は, 続く */
    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(node_time_init_fake.call_count, 1U);
    zassert_equal(oled_init_fake.call_count, 1U);
    zassert_equal(button_init_fake.call_count, 1U);
    zassert_equal(buzzer_init_fake.call_count, 1U);
    /* 起動のときに使えなかった機器は, あとで使えるようになっても, 起動画面と音を出さない */
    zassert_equal(oled_show_boot_fake.call_count, 0U);
    zassert_equal(buzzer_play_fake.call_count, 0U);
    zassert_equal(node_time_get_fake.call_count, 0U);
    zassert_equal(oled_show_fake.call_count, 0U);
    zassert_equal(button_pressed_fake.call_count, 0U);
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);

    /* 60 秒後: やり直して, 3 つとも成功する. 時刻と表示とボタンが, 使えるようになる */
    k_sleep(K_SECONDS(RETRY_INTERVAL_S + 2));
    zassert_equal(node_time_init_fake.call_count, 2U);
    zassert_equal(oled_init_fake.call_count, 2U);
    zassert_equal(button_init_fake.call_count, 2U);
    zassert_equal(buzzer_init_fake.call_count, 2U);
    zassert_equal(oled_show_boot_fake.call_count, 0U);
    zassert_equal(buzzer_play_fake.call_count, 0U);
    zassert_true(node_time_get_fake.call_count > 0U);
    zassert_true(oled_show_fake.call_count > 0U);
    zassert_true(button_pressed_fake.call_count > 0U);
    zassert_not_null(shown[0].time);

    /* 使えるようになったあとは, やり直さない */
    k_sleep(K_SECONDS(RETRY_INTERVAL_S * 2));
    zassert_equal(node_time_init_fake.call_count, 2U);
    zassert_equal(oled_init_fake.call_count, 2U);
    zassert_equal(button_init_fake.call_count, 2U);

    k_thread_abort(&main_thread);
}

/** 初期化に失敗し続ける機器は, 60 秒ごとに, やり直し続ける */
ZTEST(main_node, test_optional_devices_keep_retrying)
{
    sensor_read_fake.custom_fake = fake_read;
    node_time_init_fake.return_val = -EIO;
    oled_init_fake.return_val = -ENODEV;
    button_init_fake.return_val = -ENODEV;
    buzzer_init_fake.return_val = -ENODEV;

    start_main();

    /* 起動のときと, 60 秒後と, 120 秒後の, 3 回 */
    k_msleep(STARTUP_WAIT_MS);
    k_sleep(K_SECONDS(RETRY_INTERVAL_S * 2));
    zassert_equal(node_time_init_fake.call_count, 3U);
    zassert_equal(oled_init_fake.call_count, 3U);
    zassert_equal(button_init_fake.call_count, 3U);
    zassert_equal(buzzer_init_fake.call_count, 3U);
    zassert_equal(oled_show_fake.call_count, 0U);

    k_thread_abort(&main_thread);
}

/** 起動のときに, 起動音を始めて, 起動画面を全てのフレームで出してから, 測定を始める */
ZTEST(main_node, test_boot_sequence)
{
    unsigned int frame = 0U; /* フレームの番号 */

    sensor_read_fake.custom_fake = fake_read;

    start_main();

    k_msleep(STARTUP_WAIT_MS);

    /* メロディは, 素材 (boot_melody) を, 1 回だけ, 始める */
    zassert_equal(buzzer_play_fake.call_count, 1U);
    zassert_equal(buzzer_play_fake.arg0_val, boot_melody);
    zassert_equal(buzzer_play_fake.arg1_val, boot_melody_count);

    /* 起動画面は, フレーム 0 から OLED_BOOT_FRAMES - 1 まで, 順に 1 回ずつ */
    zassert_equal(oled_show_boot_fake.call_count, OLED_BOOT_FRAMES);
    for (frame = 0U; frame < OLED_BOOT_FRAMES; frame++) {
        zassert_equal(oled_show_boot_fake.arg0_history[frame], frame);
    }

    /* そのあとで, 測定が始まる (起動画面の間は, 測定も, 通常の表示も, しない) */
    zassert_equal(sensor_read_fake.call_count, 1U);
    zassert_equal(oled_show_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

/** 起動画面の表示に失敗したら, その時点で, 起動画面をやめて, 測定を始める (ブザーの音は, そのまま)
 */
ZTEST(main_node, test_boot_screen_failure_continues)
{
    sensor_read_fake.custom_fake = fake_read;
    oled_show_boot_fake.return_val = -EIO;

    start_main();

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(oled_show_boot_fake.call_count, 1U);
    zassert_equal(buzzer_play_fake.call_count, 1U);
    zassert_equal(sensor_read_fake.call_count, 1U);
    zassert_equal(ble_notify_temperature_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

/** メロディを始められなくても (失敗), 起動画面を出して, 測定を続ける */
ZTEST(main_node, test_boot_sound_failure_continues)
{
    sensor_read_fake.custom_fake = fake_read;
    buzzer_play_fake.return_val = -EIO;

    start_main();

    k_msleep(STARTUP_WAIT_MS);
    zassert_equal(buzzer_play_fake.call_count, 1U);
    zassert_equal(oled_show_boot_fake.call_count, OLED_BOOT_FRAMES);
    zassert_equal(sensor_read_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

/** OLED もブザーも使えないときは, 起動画面も, 起動音も, 出さずに, すぐに, 測定を始める */
ZTEST(main_node, test_boot_skipped_without_devices)
{
    sensor_read_fake.custom_fake = fake_read;
    oled_init_fake.return_val = -ENODEV;
    buzzer_init_fake.return_val = -ENODEV;

    start_main();

    /* 起動画面がないので, 通常の待ち時間 (100 ms) で, 測定が始まっている */
    k_msleep(100);
    zassert_equal(oled_show_boot_fake.call_count, 0U);
    zassert_equal(buzzer_play_fake.call_count, 0U);
    zassert_equal(sensor_read_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

ZTEST_SUITE(main_node, NULL, NULL, before, NULL, NULL);
