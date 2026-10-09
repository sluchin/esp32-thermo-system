/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node のエントリポイント
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h> /* true */
#include <stdint.h>  /* int16_t uint16_t */
#include <stdlib.h>  /* EXIT_SUCCESS EXIT_FAILURE */

#include "ble.h"
#ifdef CONFIG_THERMO_RTC
#include "node_time.h"
#endif
#ifdef CONFIG_THERMO_DISPLAY
#include "button.h"
#include "history.h"
#include "oled.h"
#endif
#include "sensor.h"
#include "thermo_ble_uuid.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(thermo_node, THERMO_LOG_LEVEL);

/** 任意の機器 (RTC, OLED, ボタン) を使えるか. 初期化に失敗した機器は, 一定間隔で, 再試行する */
struct optional_devices {
    bool rtc_ready;     /**< RTC を使えるか */
    bool display_ready; /**< OLED を使えるか */
    bool button_ready;  /**< ボタンを使えるか */
};

static void log_sample(int16_t temp_x10, uint16_t humidity_x10);

static void init_optional(struct optional_devices *devs);

static int measure(int16_t *temp_x10, uint16_t *humidity_x10);

static int64_t next_grid_ms(int64_t start_ms, int64_t now_ms, int64_t interval_ms);

/** 温度を測定する間隔 [ms] */
#define SAMPLE_INTERVAL_MS 5000

/** OLED と時刻を更新する間隔 [ms] (時刻の秒を, 1 秒ごとに更新する) */
#define REDRAW_INTERVAL_MS 1000

/** 任意の機器の初期化に失敗したときに, やり直す間隔 [s] */
#define RETRY_INTERVAL_S 60

/** 任意の機器の初期化に失敗したときに, やり直す間隔 [ms] */
#define RETRY_INTERVAL_MS (RETRY_INTERVAL_S * 1000)

/** メインループの周期 [ms] (ボタンを調べる間隔. ボタンのチャタリングを無視する時間にもなる) */
#define TICK_MS 50

#ifdef CONFIG_THERMO_DISPLAY
/** グラフの 1 点の時間 [ms] (CONFIG_THERMO_GRAPH_STEP_S) */
#define GRAPH_STEP_MS ((int64_t)CONFIG_THERMO_GRAPH_STEP_S * 1000)
#endif

/**
 * @brief ノードのメイン関数
 *
 * センサと BLE を初期化してアドバタイズを開始し, その後は 50 ms ごとのループで, 一定間隔
 * (SAMPLE_INTERVAL_MS) で温度を読み取って, ログ出力して, 接続しているゲートウェイへ通知
 * (GATT の notify) する. ゲートウェイから時刻を受け取ったら (GATT の write), RTC に設定する
 * (CONFIG_THERMO_RTC). OLED があれば (CONFIG_THERMO_DISPLAY), 温度, 湿度, 時刻を, 1 秒ごとに
 * 表示して, 温度と湿度の履歴 (グラフ) を覚える. ボタンを押すと, ページを切り替えて, すぐに
 * 表示し直す. 時刻は, RTC (CONFIG_THERMO_RTC) から読む. OLED と RTC とボタンの初期化に失敗しても,
 * 表示と時刻を諦めるだけで, 測定と通知は続ける (起動時の一時的な失敗から復帰するため,
 * RETRY_INTERVAL_MS ごとに, 初期化をやり直す).
 *
 * @retval EXIT_FAILURE 初期化またはアドバタイズ開始に失敗した場合
 *                      (正常時はループから戻らない)
 */
int main(void)
{
    int ret = EXIT_SUCCESS;            /* 戻り値 */
    int64_t start_ms = k_uptime_get(); /* ループを始めた稼働時間 [ms] */
    int64_t next_sample_ms = 0;        /* 次に測る稼働時間 [ms] (初回は, すぐ測る) */
    int64_t next_redraw_ms = 0;        /* 次に表示を更新する稼働時間 [ms] */
    int64_t next_retry_ms = start_ms + RETRY_INTERVAL_MS; /* 次に初期化をやり直す稼働時間 [ms] */
    int64_t now_ms = 0;                                   /* 今の稼働時間 [ms] */
    int16_t temp_x10 = 0;                                 /* 温度 [℃ の 10 倍] */
    uint16_t humidity_x10 = 0;                            /* 湿度 [% の 10 倍] */
    struct optional_devices devs = {0};                   /* 任意の機器を使えるか */
#ifdef CONFIG_THERMO_RTC
    struct node_datetime datetime = {0}; /* RTC のローカルタイム */
#endif
#ifdef CONFIG_THERMO_DISPLAY
    struct history temp_history = {0};     /* 温度の履歴 */
    struct history humidity_history = {0}; /* 湿度の履歴 */
    struct oled_view view = {0};           /* OLED に表示する内容 */
#endif

/* ビルド構成に応じて起動ログを切り替える */
#ifdef CONFIG_SIMULATOR
    LOG_INF("Thermo Node started (SIMULATOR MODE)");
#else
    LOG_INF("Thermo Node started on ESP32C3");
#endif

    /* センサを初期化する */
    ret = sensor_init();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to initialize sensor");
        return EXIT_FAILURE;
    }

    /* BLE を初期化する */
    ret = ble_init();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to initialize BLE");
        return EXIT_FAILURE;
    }

#ifdef CONFIG_THERMO_RTC
    /* ゲートウェイが SNTP で得た時刻を書き込んできたら, RTC に設定する */
    ble_set_time_callback(node_time_set);
#endif

    /* ゲートウェイから検出されるようアドバタイズを開始する */
    ret = ble_advertise();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to start BLE advertising");
        return EXIT_FAILURE;
    }

    /* RTC と OLED とボタンを初期化する (失敗しても, 測定と通知は続ける) */
    init_optional(&devs);
#ifdef CONFIG_THERMO_DISPLAY
    view.temp_history = &temp_history;
    view.humidity_history = &humidity_history;
#endif

    /* 50 ms ごとに, ボタンを調べて, 一定間隔で温度と湿度を測り, 1 秒ごとに時刻を表示する */
    while (true) {
        now_ms = k_uptime_get();

        if (now_ms >= next_sample_ms) {
            next_sample_ms = next_grid_ms(start_ms, now_ms, SAMPLE_INTERVAL_MS);
            ret = measure(&temp_x10, &humidity_x10);
#ifdef CONFIG_THERMO_DISPLAY
            /* 読み取りに失敗したときは, 前回の値を表示し続ける */
            if (ret == EXIT_SUCCESS) {
                view.has_sample = true;
                view.temp_x10 = temp_x10;
                view.humidity_x10 = humidity_x10;
                history_add(&temp_history, temp_x10, now_ms, GRAPH_STEP_MS);
                if (humidity_x10 != THERMO_HUMIDITY_NONE) {
                    history_add(&humidity_history, (int16_t)humidity_x10, now_ms, GRAPH_STEP_MS);
                }
            }
#endif
        }

#ifdef CONFIG_THERMO_DISPLAY
        /* ボタンが押されたら, 次のページにして, すぐに表示し直す */
        if (devs.button_ready && button_pressed()) {
            view.page = (view.page + 1U) % OLED_PAGE_COUNT;
            next_redraw_ms = now_ms;
        }
#endif

        /* 初期化に失敗した機器を, やり直す (起動のときの, 一時的な失敗から, 復帰する) */
        if (now_ms >= next_retry_ms) {
            next_retry_ms = next_grid_ms(start_ms, now_ms, RETRY_INTERVAL_MS);
            init_optional(&devs);
        }

        if (now_ms >= next_redraw_ms) {
            next_redraw_ms = next_grid_ms(start_ms, now_ms, REDRAW_INTERVAL_MS);
#ifdef CONFIG_THERMO_RTC
            if (devs.rtc_ready) {
                /* 未設定や読み取りの失敗のときは, datetime.valid が false (時刻なしと表示する) */
                (void)node_time_get(&datetime);
#ifdef CONFIG_THERMO_DISPLAY
                view.time = &datetime;
#endif
            }
#endif
#ifdef CONFIG_THERMO_DISPLAY
            /* OLED に表示する (失敗しても次回に再試行する) */
            if (devs.display_ready) {
                ret = oled_show(&view);
                if (ret != EXIT_SUCCESS) {
                    LOG_ERR("Failed to show on the OLED (err %d)", ret);
                }
            }
#endif
        }

        (void)k_msleep(TICK_MS);
    }
}

/**
 * 任意の機器 (RTC, OLED, ボタン) のうち, まだ使えない機器を初期化する
 *
 * 起動のときに呼んで, 失敗した機器は, RETRY_INTERVAL_MS ごとに, もう一度呼ぶ. 失敗しても,
 * その機器を 使えないだけで, 測定と通知は続ける. 使えるようになった機器は, 呼ばない.
 *
 * @param[in,out] devs 任意の機器を使えるか (初期化に成功した機器を, true にする)
 */
static void init_optional(struct optional_devices *devs)
{
#ifdef CONFIG_THERMO_RTC
    int rtc_ret = EXIT_SUCCESS; /* RTC の初期化の戻り値 */
#endif
#ifdef CONFIG_THERMO_DISPLAY
    int display_ret = EXIT_SUCCESS; /* OLED の初期化の戻り値 */
    int button_ret = EXIT_SUCCESS;  /* ボタンの初期化の戻り値 */
#endif

    ARG_UNUSED(devs);

#ifdef CONFIG_THERMO_RTC
    if (!devs->rtc_ready) {
        rtc_ret = node_time_init();
        devs->rtc_ready = (rtc_ret == EXIT_SUCCESS);
        if (!devs->rtc_ready) {
            LOG_ERR("Failed to initialize the RTC (err %d), no time (retry in %d s)", rtc_ret,
                    RETRY_INTERVAL_S);
        }
    }
#endif

#ifdef CONFIG_THERMO_DISPLAY
    if (!devs->display_ready) {
        display_ret = oled_init();
        devs->display_ready = (display_ret == EXIT_SUCCESS);
        if (!devs->display_ready) {
            LOG_ERR("Failed to initialize the OLED (err %d), no display (retry in %d s)",
                    display_ret, RETRY_INTERVAL_S);
        }
    }

    if (!devs->button_ready) {
        button_ret = button_init();
        devs->button_ready = (button_ret == EXIT_SUCCESS);
        if (!devs->button_ready) {
            LOG_ERR("Failed to initialize the button (err %d), the page is not switched "
                    "(retry in %d s)",
                    button_ret, RETRY_INTERVAL_S);
        }
    }
#endif
}

/**
 * 温度と湿度を読み取って, ログに出力して, ゲートウェイへ通知する
 *
 * 読み取りに失敗したときは, 今回の値を捨てて (通知しない), 次回に再試行する. 通知に失敗しても,
 * 次回に再試行する.
 *
 * @param[out] temp_x10     温度 [℃ の 10 倍] の格納先 (NULL 不可)
 * @param[out] humidity_x10 湿度 [% の 10 倍] の格納先 (NULL 不可)
 *
 * @retval EXIT_SUCCESS 読み取りに成功 (通知の失敗は, 戻り値に含めない)
 * @retval negative     センサの読み取りの失敗 (負の errno)
 */
static int measure(int16_t *temp_x10, uint16_t *humidity_x10)
{
    int ret = EXIT_SUCCESS;        /* 戻り値 */
    int notify_ret = EXIT_SUCCESS; /* 通知の戻り値 */

    ret = sensor_read(temp_x10, humidity_x10);
    if (ret != EXIT_SUCCESS) {
        return ret;
    }

    log_sample(*temp_x10, *humidity_x10);

    /* 接続しているゲートウェイへ通知する (失敗しても次回に再試行する) */
    notify_ret = ble_notify_temperature(*temp_x10, *humidity_x10);
    if (notify_ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to notify the temperature (err %d)", notify_ret);
    }

    return EXIT_SUCCESS;
}

/**
 * 温度と湿度をログに出力する
 *
 * 10 分の 1 の単位の整数を, 小数点付きの形 (例: -3.5 ℃, 45.0 %) で出力する.
 * 湿度を測れないとき (THERMO_HUMIDITY_NONE) は, 温度だけを出力する.
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍]
 */
static void log_sample(int16_t temp_x10, uint16_t humidity_x10)
{
    int temp = temp_x10; /* 温度 [℃ の 10 倍] */
    /* 0 ℃ 未満は整数部が 0 でも (-0.5 など) 符号を出すため, 符号を別に出力する */
    const char *sign = ((temp < 0) ? "-" : "");                         /* 符号 */
    unsigned int magnitude = (unsigned int)((temp < 0) ? -temp : temp); /* 絶対値 */

    if (humidity_x10 == THERMO_HUMIDITY_NONE) {
        LOG_INF("Temperature: %s%u.%u C", sign, magnitude / 10U, magnitude % 10U);
    } else {
        LOG_INF("Temperature: %s%u.%u C, humidity: %u.%u %%", sign, magnitude / 10U,
                magnitude % 10U, humidity_x10 / 10U, humidity_x10 % 10U);
    }
}

/**
 * 一定の間隔の格子の, 次の時刻を求める
 *
 * 待ちは, カーネルのティックの丸めで, 少し長くなる. 「今 + 間隔」で次を決めると, 遅れが
 * 積もるので, ループを始めた時刻から, 間隔ごとの格子に合わせる.
 *
 * @param[in] start_ms    格子の始まりの稼働時間 [ms]
 * @param[in] now_ms      今の稼働時間 [ms] (start_ms 以上)
 * @param[in] interval_ms 間隔 [ms] (1 以上)
 *
 * @return 今より後の, 格子の最初の稼働時間 [ms]
 */
static int64_t next_grid_ms(int64_t start_ms, int64_t now_ms, int64_t interval_ms)
{
    return start_ms + ((((now_ms - start_ms) / interval_ms) + 1) * interval_ms);
}
