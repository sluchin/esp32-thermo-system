/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief main.c の OLED の自動消灯の単体テスト
 *
 * sensor と ble と oled とボタンの関数を FFF のモックに置き換えて (別スレッドで main を動かして,
 * 仮想の時間を進めて), 次を確認する.
 *  - ボタンを押さずに 10 秒たつと, OLED を消して (oled_set_power(false)), 消えている間は,
 * 表示しないこと
 *  - 消えている間にボタンを押すと, 点灯するだけで, ページは切り替えないこと (点灯のあとは,
 * 表示を続ける)
 *  - 消えている間にボタンを押して点灯するときは, ボタンの音 (buzzer_play()) を鳴らさないこと
 *  - 点灯している間にボタンを押すと, ページを切り替えて, ボタンの音を鳴らして,
 * 消すまでの時間を数え直すこと
 *  - 消灯と点灯に失敗したときは, やり直すこと (消灯は 1 秒後, 点灯は次のボタン)
 *  - OLED の初期化に失敗した間は, 消灯も点灯もしないこと (ボタンでは, ページを切り替える).
 * やり直して, 初期化に成功したら, 点灯して, 消すまでの時間を数えること
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <errno.h>   /* ENODEV EIO */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* memset */

#include "ble.h"
#include "button.h"
#include "buzzer.h"
#include "history.h"
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
FAKE_VALUE_FUNC(int, buzzer_init)
FAKE_VALUE_FUNC(int, buzzer_play, const struct buzzer_note *, size_t)
FAKE_VALUE_FUNC(int, button_init)
FAKE_VALUE_FUNC(bool, button_pressed)
FAKE_VOID_FUNC(history_add, struct history *, int16_t, int64_t, int64_t)
FAKE_VALUE_FUNC(int, oled_init)
FAKE_VALUE_FUNC(int, oled_set_power, bool)
FAKE_VALUE_FUNC(int, oled_show, const struct oled_view *)

/** メインループのスレッドのスタックサイズ */
#define STACK_SIZE      2048
/** メインループのスレッドの優先度 */
#define THREAD_PRIORITY 5
/** main.c のループの 1 周の時間 [ms] (TICK_MS の 50 ms + ティックの丸め 10 ms. ボタンを調べる間隔)
 */
#define TICK_MS         60
/** OLED を消すまでの時間 [ms] (CONFIG_THERMO_DISPLAY_TIMEOUT_SEC = 10) */
#define TIMEOUT_MS      10000
/** 初期化をやり直す間隔 [ms] (main.c の RETRY_INTERVAL_MS) */
#define RETRY_MS        60000
/** 時間を進めるときの, 余裕 [ms] (ティックの丸めと, 表示の更新を待つ) */
#define MARGIN_MS       300
/** ボタンを押したことにする呼び出しの数の上限 */
#define PRESS_COUNT     3U
/** oled_show() に渡された表示内容を記録する数 */
#define SHOWN_MAX       128U

/** 押されたことにする button_pressed() の呼び出しの回数 (0 は, 使わない) */
static unsigned int press_at[PRESS_COUNT];
/** oled_show() に渡された表示内容 (呼ばれた順) */
static struct oled_view shown[SHOWN_MAX];
/** oled_set_power() の 1 回目の戻り値 (2 回目以降は, 0) */
static int power_first_ret;
/** oled_init() の 1 回目の戻り値 (2 回目以降は, 0) */
static int init_first_ret;
/** oled_init() が, 呼ばれるたびに失敗するか */
static bool init_always_fails;

/** main を動かすスレッド */
static struct k_thread main_thread;
/** main を動かすスレッドのスタック */
static K_THREAD_STACK_DEFINE(main_stack, STACK_SIZE)

/**
 * sensor_read() のモック動作 (固定の値を書き込む)
 *
 * @param[out] temp_x10     温度 [℃ の 10 倍]
 * @param[out] humidity_x10 湿度 [% の 10 倍]
 * @return 0
 */
static int fake_read(int16_t *temp_x10, uint16_t *humidity_x10)
{
    *temp_x10 = 235;
    *humidity_x10 = 450U;

    return 0;
}

/**
 * oled_show() のモック動作 (渡された表示内容を記録する)
 *
 * @param[in] view 表示内容
 * @return 0
 */
static int fake_show(const struct oled_view *view)
{
    unsigned int idx = oled_show_fake.call_count - 1U; /* 今回の呼び出しの番号 (0 始まり) */

    if (idx < SHOWN_MAX) {
        shown[idx] = *view;
    }

    return 0;
}

/**
 * oled_set_power() のモック動作 (1 回目だけ, 設定した値を返す)
 *
 * @param[in] on 点灯するなら true (使用しない)
 * @return 1 回目は power_first_ret, 2 回目以降は 0
 */
static int fake_power(bool on)
{
    ARG_UNUSED(on);

    return (oled_set_power_fake.call_count == 1U) ? power_first_ret : 0;
}

/**
 * oled_init() のモック動作
 *
 * @return init_always_fails なら -ENODEV, そうでなければ, 1 回目は init_first_ret, 2 回目以降は 0
 */
static int fake_init(void)
{
    if (init_always_fails) {
        return -ENODEV;
    }

    return (oled_init_fake.call_count == 1U) ? init_first_ret : 0;
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
    RESET_FAKE(buzzer_init);
    RESET_FAKE(buzzer_play);
    RESET_FAKE(button_init);
    RESET_FAKE(button_pressed);
    RESET_FAKE(history_add);
    RESET_FAKE(oled_init);
    RESET_FAKE(oled_set_power);
    RESET_FAKE(oled_show);
    FFF_RESET_HISTORY();
    sensor_read_fake.custom_fake = fake_read;
    oled_show_fake.custom_fake = fake_show;
    oled_set_power_fake.custom_fake = fake_power;
    oled_init_fake.custom_fake = fake_init;
    button_pressed_fake.custom_fake = fake_pressed;
    (void)memset(press_at, 0, sizeof(press_at));
    (void)memset(shown, 0, sizeof(shown));
    power_first_ret = 0;
    init_first_ret = 0;
    init_always_fails = false;
}

/** 別スレッドで main を動かす */
static void start_main(void)
{
    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack), main_entry, NULL,
                    NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
}

/**
 * ボタンを押すループの回数 (button_pressed() の呼び出しの回数) を, 経過時間から求める
 *
 * @param[in] ms 経過時間 [ms]
 * @return 呼び出しの回数 (1 始まり)
 */
static unsigned int tick_at(unsigned int ms)
{
    return (ms / TICK_MS) + 1U;
}

/** ボタンを押さずに 10 秒たつと, OLED を消して, 消えている間は, 表示しない */
ZTEST(main_timeout, test_display_turns_off_after_timeout)
{
    unsigned int shown_when_off = 0U; /* 消えた直後の, 表示した回数 */

    start_main();

    /* 10 秒の前は, 点灯のまま (表示は 1 秒ごと) */
    k_msleep(TIMEOUT_MS - MARGIN_MS);
    zassert_equal(oled_set_power_fake.call_count, 0U);
    zassert_true(oled_show_fake.call_count >= 9U);

    /* 10 秒たったら, 消す */
    k_msleep(2 * MARGIN_MS);
    zassert_equal(oled_set_power_fake.call_count, 1U);
    zassert_false(oled_set_power_fake.arg0_val);

    /* 消えている間は, 表示しない (測定は, 続く) */
    shown_when_off = oled_show_fake.call_count;
    k_msleep(3000);
    zassert_equal(oled_show_fake.call_count, shown_when_off);
    zassert_true(sensor_read_fake.call_count >= 2U);

    k_thread_abort(&main_thread);
}

/** 消えている間にボタンを押すと, 点灯するだけで, ページは切り替えない. 点灯したあとは, 表示を続ける
 */
ZTEST(main_timeout, test_button_wakes_display_without_switching_page)
{
    unsigned int shown_when_off = 0U; /* 消えている間の, 表示した回数 */

    press_at[0] = tick_at(13000U);

    start_main();

    k_msleep(12000);
    zassert_equal(oled_set_power_fake.call_count, 1U);
    shown_when_off = oled_show_fake.call_count;

    /* 13 秒に押す: 点灯して (ページは, 最初のまま), すぐに表示し直す */
    k_msleep(1500);
    zassert_equal(oled_set_power_fake.call_count, 2U);
    zassert_true(oled_set_power_fake.arg0_val);
    zassert_true(oled_show_fake.call_count > shown_when_off);
    zassert_equal(shown[oled_show_fake.call_count - 1U].page, OLED_PAGE_NOW);
    /* 点灯のための押下では, 音を鳴らさない (鳴らすのは, 起動のメロディの 1 回だけ) */
    zassert_equal(buzzer_play_fake.call_count, 1U);

    /* 押してから 10 秒たつと, また消える */
    k_msleep(TIMEOUT_MS);
    zassert_equal(oled_set_power_fake.call_count, 3U);
    zassert_false(oled_set_power_fake.arg0_val);

    k_thread_abort(&main_thread);
}

/** 点灯している間にボタンを押すと, ページを切り替えて, 消すまでの時間を数え直す */
ZTEST(main_timeout, test_button_extends_the_timeout)
{
    press_at[0] = tick_at(5000U);

    start_main();

    /* 5 秒に押す: ページを切り替える (OLED は, 点灯のまま) */
    k_msleep(5000 + MARGIN_MS);
    zassert_equal(shown[oled_show_fake.call_count - 1U].page, OLED_PAGE_TEMP_GRAPH);
    zassert_equal(oled_set_power_fake.call_count, 0U);
    /* ページを切り替えたときは, ボタンの音を鳴らす (起動のメロディと, 合わせて 2 回) */
    zassert_equal(buzzer_play_fake.call_count, 2U);

    /* 押した 5 秒後 (10 秒) には, まだ消えない */
    k_msleep(5000);
    zassert_equal(oled_set_power_fake.call_count, 0U);

    /* 押した 10 秒後 (15 秒) に消える */
    k_msleep(5000 + MARGIN_MS);
    zassert_equal(oled_set_power_fake.call_count, 1U);
    zassert_false(oled_set_power_fake.arg0_val);

    k_thread_abort(&main_thread);
}

/** 消灯に失敗したら, 点灯のまま, 1 秒後にやり直す */
ZTEST(main_timeout, test_power_off_failure_retries)
{
    power_first_ret = -EIO;

    start_main();

    k_msleep(TIMEOUT_MS + MARGIN_MS);
    zassert_equal(oled_set_power_fake.call_count, 1U);
    /* 失敗したので, まだ点灯していることにして, 表示を続ける */
    k_msleep(500);
    zassert_equal(oled_set_power_fake.call_count, 1U);
    k_msleep(1000);
    zassert_equal(oled_set_power_fake.call_count, 2U);
    zassert_false(oled_set_power_fake.arg0_val);

    k_thread_abort(&main_thread);
}

/** 点灯に失敗したら, 消えたまま (ページも切り替えない). 次にボタンを押したときに, 点灯する */
ZTEST(main_timeout, test_power_on_failure_waits_for_the_next_press)
{
    unsigned int shown_when_off = 0U; /* 消えている間の, 表示した回数 */

    /* 消灯 (1 回目) は成功, 点灯 (2 回目) は失敗, 点灯 (3 回目) は成功にする */
    oled_set_power_fake.custom_fake = NULL;
    oled_set_power_fake.return_val = 0;
    int returns[] = {0, -EIO, 0};

    SET_RETURN_SEQ(oled_set_power, returns, ARRAY_SIZE(returns));
    press_at[0] = tick_at(13000U);
    press_at[1] = tick_at(15000U);

    start_main();

    k_msleep(12000);
    shown_when_off = oled_show_fake.call_count;

    /* 13 秒に押す: 点灯に失敗して, 消えたまま (表示しない) */
    k_msleep(2000 - 2 * MARGIN_MS);
    zassert_equal(oled_set_power_fake.call_count, 2U);
    zassert_equal(oled_show_fake.call_count, shown_when_off);

    /* 15 秒に押す: 点灯して, 表示し直す */
    k_msleep(2 * MARGIN_MS + 1000);
    zassert_equal(oled_set_power_fake.call_count, 3U);
    zassert_true(oled_show_fake.call_count > shown_when_off);

    k_thread_abort(&main_thread);
}

/** OLED の初期化に失敗している間は, 消灯も点灯もしない (ボタンを押しても, 失敗しない) */
ZTEST(main_timeout, test_no_power_control_without_display)
{
    init_always_fails = true;
    press_at[0] = tick_at(2000U);

    start_main();

    /* 初期化のやり直しの後 (60 秒後) まで進める: 失敗し続ける */
    k_msleep(RETRY_MS + 1000);
    zassert_true(oled_init_fake.call_count >= 2U);
    zassert_equal(oled_set_power_fake.call_count, 0U);
    zassert_equal(oled_show_fake.call_count, 0U);

    k_thread_abort(&main_thread);
}

/** 初期化に失敗した OLED が, やり直しで使えるようになったら, 点灯して, 消すまでの時間を数える */
ZTEST(main_timeout, test_display_recovers_and_times_out)
{
    init_first_ret = -ENODEV;

    start_main();

    /* 最初の 60 秒は, 使えない (表示しない) */
    k_msleep(RETRY_MS - MARGIN_MS);
    zassert_equal(oled_show_fake.call_count, 0U);
    zassert_equal(oled_set_power_fake.call_count, 0U);

    /* やり直しで使えるようになったら, 表示する (消えていない) */
    k_msleep(2 * MARGIN_MS + 1000);
    zassert_true(oled_show_fake.call_count >= 1U);
    zassert_equal(oled_set_power_fake.call_count, 0U);

    /* 使えるようになってから 10 秒後に消える */
    k_msleep(TIMEOUT_MS);
    zassert_equal(oled_set_power_fake.call_count, 1U);
    zassert_false(oled_set_power_fake.arg0_val);

    k_thread_abort(&main_thread);
}

/** 使えている OLED は, やり直しの時刻 (60 秒後) に, 点灯と消灯の状態を変えない */
ZTEST(main_timeout, test_retry_keeps_the_state_of_a_working_display)
{
    start_main();

    /* 10 秒で消えて, 60 秒のやり直しのあとも, 消えたまま (1 回だけ消す) */
    k_msleep(RETRY_MS + 1000);
    zassert_equal(oled_set_power_fake.call_count, 1U);
    zassert_equal(oled_init_fake.call_count, 1U);

    k_thread_abort(&main_thread);
}

ZTEST_SUITE(main_timeout, NULL, NULL, before, NULL, NULL);
