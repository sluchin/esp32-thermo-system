/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief ゲートウェイにつながらないときの, WiFi での AWS IoT Core への直接の送信 (MQTT) と,
 * SD カードへの保存, あとからの送信
 */

#include <zephyr/bluetooth/addr.h>
#include <zephyr/logging/log.h>
#include <errno.h>   /* ENOENT EBADMSG */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t int64_t uint32_t */
#include <stdlib.h>  /* EXIT_SUCCESS */

#include "backlog.h"
#include "ble.h"
#include "cfg.h"
#include "cloud.h"
#include "direct.h"
#include "node_time.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(thermo_direct, THERMO_LOG_LEVEL);

/** 送信のキューに, SD カードの記録を入れておく数の上限 (キューを満杯にしない) */
#define FLUSH_QUEUE_MAX 4U

/** BLE がゲートウェイにつながっていない測定が, 続いている回数 (上限で止める) */
static unsigned int ble_failures;
/** 前の測定のとき, BLE がゲートウェイにつながっていたか */
static bool was_connected;
/** 直接の送信 (WiFi と MQTT) を動かしているか */
static bool active;
/** あきらめたか (ゲートウェイにつながるまで, やり直さない) */
static bool gave_up;
/** 設定が揃っていないことを, 知らせたか (測定のたびに, 知らせない) */
static bool warned_incomplete;
/** 時刻がなくて, SD カードに溜められないことを, 知らせたか */
static bool warned_no_time;
/** ゲートウェイにつながっていない測定の数 (DIRECT_PUBLISH_EVERY ごとに, 1 回, 送るか, 溜める) */
static unsigned int tick;
/** このノードの BLE のアドレス (ゲートウェイが見るアドレスと同じ. トピックと payload に使う) */
static bt_addr_le_t node_addr;
/** node_addr を取れたか */
static bool addr_ready;
/** SD カードを使えるか */
static bool backlog_ready;
/** SD カードの記録のうち, 送信のキューに入れて, まだ, 送り終えたことにしていない数 */
static unsigned int queued;
/** queued が 0 のときの, 送った数 (cloud_sent_count()). 送れた数を求めるために使う */
static uint32_t sent_mark;

static void stop_sending(void);
static void start_sending(void);
static void handle_value(int16_t temp_x10, uint16_t humidity_x10);
static void save_value(int16_t temp_x10, uint16_t humidity_x10);
static void flush_backlog(void);

/**
 * @brief 直接の送信の準備をする (止めた状態で, 送信のスレッドを作って, SD カードを使えるようにする)
 *
 * BLE を初期化したあとに呼ぶ. 失敗したときは, 直接の送信を使わない (ほかの機能には, 影響しない).
 * SD カードがなくても, 成功する (溜めないだけ).
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int direct_init(void)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    /* 状態を最初に戻す (やり直しても, 前の状態を引き継がない) */
    addr_ready = false;
    ble_failures = 0U;
    was_connected = false;
    active = false;
    gave_up = false;
    warned_incomplete = false;
    warned_no_time = false;
    tick = 0U;
    queued = 0U;
    sent_mark = 0U;

    err = ble_get_address(&node_addr);
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Could not get the BLE address (err %d), no direct publish", err);
        return err;
    }

    err = cloud_init(false);
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Could not start the cloud thread (err %d), no direct publish", err);
        return err;
    }
    cloud_set_attempt_limit(DIRECT_ATTEMPT_LIMIT);

    backlog_ready = (backlog_init() == EXIT_SUCCESS);
    if (!backlog_ready) {
        LOG_WRN("No SD card, the values are not saved while no connection is available");
    }
    addr_ready = true;

    return EXIT_SUCCESS;
}

/**
 * @brief 測定値を知らせる (測定のたびに呼ぶ)
 *
 * BLE がゲートウェイにつながっていない測定が続いたら, または, SD カードに送っていない記録が
 * あるときは, WiFi と MQTT を動かす. ゲートウェイにつながっていない間は, 一定の間隔で, 測定値を,
 * MQTT で送る (まだ接続していない, または, SD カードに送っていない記録が残っているときは,
 * SD カードに溜める). 動かしている間は, SD カードの記録を, 古い順に送る.
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍] (湿度がなければ THERMO_HUMIDITY_NONE)
 */
void direct_sample(int16_t temp_x10, uint16_t humidity_x10)
{
    bool connected = false;   /* ゲートウェイにつながっているか */
    bool wifi_wanted = false; /* WiFi と MQTT が要るか */

    if (!addr_ready) {
        return;
    }
    connected = ble_is_connected();

    /* ゲートウェイにつながった: 数え直す (あきらめた状態も戻す) */
    if (connected) {
        if (!was_connected) {
            gave_up = false;
        }
        ble_failures = 0U;
        warned_incomplete = false;
        tick = 0U;
    } else if (ble_failures < DIRECT_BLE_FAIL_LIMIT) {
        ble_failures++;
    }
    was_connected = connected;

    /* WiFi が要るのは, つながらない測定が続くときと, 送っていない記録があるとき */
    wifi_wanted =
            ((!connected) && (ble_failures >= DIRECT_BLE_FAIL_LIMIT)) || (backlog_pending() > 0U);

    if (!wifi_wanted) {
        if (active) {
            stop_sending();
            LOG_INF("Nothing to send over WiFi, stop sending directly");
        }
    } else if (!active) {
        if (!gave_up) {
            start_sending();
        }
    } else if (cloud_has_given_up()) {
        active = false;
        gave_up = true;
        queued = 0U;
        LOG_ERR("Could not connect, give up sending directly until the gateway connects");
    }

    /* ゲートウェイにつながっていない間は, 一定の間隔で, 測定値を送るか, 溜める */
    if (!connected) {
        tick++;
        if (tick >= DIRECT_PUBLISH_EVERY) {
            tick = 0U;
            handle_value(temp_x10, humidity_x10);
        }
    }

    if (active) {
        flush_backlog();
    }
}

/**
 * @brief 直接の送信を動かしているか (動かしている間は, WiFi を, 時刻の取得に使わない)
 *
 * @retval true  動かしている
 * @retval false 動かしていない
 */
bool direct_active(void)
{
    return active;
}

/** 直接の送信を止める (WiFi を切る) */
static void stop_sending(void)
{
    cloud_set_enabled(false);
    active = false;
    queued = 0U;
}

/** 直接の送信を始める (設定が揃っていなければ, 始めない) */
static void start_sending(void)
{
    if (!cfg_is_complete()) {
        if (!warned_incomplete) {
            LOG_WRN("WiFi is needed, but the settings are not complete, no direct publish");
            warned_incomplete = true;
        }
        return;
    }

    cloud_set_enabled(true);
    active = true;
    tick = 0U;
    queued = 0U;
    LOG_WRN("Start sending over WiFi (%u samples without the gateway, %u records on the SD card)",
            ble_failures, backlog_pending());
}

/**
 * 測定値を 1 件, 送る (MQTT につながっていて, SD カードの記録がなければ) か, SD カードに溜める
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍]
 */
static void handle_value(int16_t temp_x10, uint16_t humidity_x10)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    /* つながっていて, 先に送る記録がなければ, すぐに送る (順序を保つ) */
    if (active && cloud_is_connected() && (backlog_pending() == 0U)) {
        err = cloud_publish_temperature(&node_addr, temp_x10, humidity_x10);
        if (err != EXIT_SUCCESS) {
            LOG_WRN("Could not queue the temperature (err %d)", err);
        }
        return;
    }

    save_value(temp_x10, humidity_x10);
}

/**
 * 測定値を, SD カードに溜める (SD カードがない, または, 時刻がないときは, 捨てる)
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍]
 */
static void save_value(int16_t temp_x10, uint16_t humidity_x10)
{
    struct backlog_record rec = {0}; /* 溜める記録 */
    int err = EXIT_SUCCESS;          /* エラーコード */

    if (!backlog_ready) {
        return;
    }

    /* 測った UNIX 時刻が要る (あとから送るときに, そのまま payload に入れる) */
    err = node_time_unix(&rec.unix_s);
    if (err != EXIT_SUCCESS) {
        if (!warned_no_time) {
            LOG_WRN("No time in the RTC, the values cannot be saved on the SD card");
            warned_no_time = true;
        }
        return;
    }
    warned_no_time = false;

    rec.temp_x10 = temp_x10;
    rec.humidity_x10 = humidity_x10;
    err = backlog_append(&rec);
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Could not save the value on the SD card (err %d)", err);
    }
}

/**
 * SD カードの記録を, 古い順に, 送信のキューに入れる (MQTT につながっているとき). 送れた分は,
 * 送り終えたことにする
 */
static void flush_backlog(void)
{
    struct backlog_record rec = {0}; /* SD カードから読み出した記録 */
    uint32_t done = 0U;              /* 前回から, 送れた数 */
    int err = EXIT_SUCCESS;          /* エラーコード */

    if (!cloud_is_connected()) {
        return;
    }

    /* キューに入れた記録のうち, publish できた数だけ, 送り終えたことにする */
    done = cloud_sent_count() - sent_mark;
    if ((done > 0U) && (done <= queued)) {
        err = backlog_commit(done);
        if (err != EXIT_SUCCESS) {
            LOG_ERR("Could not update the position on the SD card (err %d)", err);
            return;
        }
        queued -= done;
        sent_mark += done;
    }

    /* 先の記録を, キューに入れる (キューを, 満杯にしない) */
    while ((queued < backlog_pending()) && (cloud_pending() < FLUSH_QUEUE_MAX)) {
        err = backlog_read(&rec, queued);
        if (err == -EBADMSG) {
            /* 壊れた記録: 先に入れた記録を送り終えたら, 捨てる (途中は, 数えられない) */
            if (queued == 0U) {
                LOG_ERR("The record on the SD card is broken, drop it");
                (void)backlog_commit(1U);
                continue;
            }
            break;
        }
        if (err != EXIT_SUCCESS) {
            break;
        }

        if (queued == 0U) {
            sent_mark = cloud_sent_count();
        }
        err = cloud_publish_temperature_at(&node_addr, rec.temp_x10, rec.humidity_x10, rec.unix_s);
        if (err != EXIT_SUCCESS) {
            break;
        }
        queued++;
    }
}
