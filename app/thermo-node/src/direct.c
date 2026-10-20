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

/** BLE がゲートウェイにつながっていない測定が続いている回数 (上限で止める) */
static unsigned int ble_failures;
/** 前の測定のとき, BLE がゲートウェイにつながっていたか */
static bool was_connected;
/** 直接の送信 (WiFi と MQTT) を動かしているか */
static bool active;
/** あきらめたか (ゲートウェイにつながるまで, やり直さない) */
static bool gave_up;
/** 設定が揃っていないことを知らせたか (測定のたびに, 知らせない) */
static bool warned_incomplete;
/** 時刻がなくて, SD カードに溜められないことを知らせたか */
static bool warned_no_time;
/** このノードの BLE のアドレス (ゲートウェイが見るアドレスと同じ. トピックと payload に使う) */
static bt_addr_le_t node_addr;
/** node_addr を取れたか */
static bool addr_ready;
/** direct_init() の結果 (成功は 0. まだ呼んでいなければ -EAGAIN) */
static int init_status = -EAGAIN;
/** SD カードを使えるか */
static bool backlog_ready;
/** SD カードの記録のうち, 送信のキューに入れて, まだ, 送り終えたことにしていない数 */
static unsigned int queued;
/** queued が 0 のときの, 送った数 (cloud_sent_count()). 送れた数を求めるために使う */
static uint32_t sent_mark;

static void stop_sending(void);
static void start_sending(void);
static bool save_value(int16_t temp_x10, uint16_t humidity_x10, uint16_t pressure_x10, bool sent);
static void publish_live(int16_t temp_x10, uint16_t humidity_x10, uint16_t pressure_x10);
static void flush_backlog(void);

/**
 * @brief 直接の送信の準備をする (止めた状態で, 送信のスレッドを作って, SD カードを使えるようにする)
 *
 * BLE を初期化したあとに呼ぶ. 失敗したときは直接の送信を使わない (ほかの機能には影響しない).
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
    init_status = -EAGAIN;
    ble_failures = 0U;
    was_connected = false;
    active = false;
    gave_up = false;
    warned_incomplete = false;
    warned_no_time = false;
    queued = 0U;
    sent_mark = 0U;

    err = ble_get_address(&node_addr);
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Could not get the BLE address (err %d), no direct publish", err);
        init_status = err;
        return err;
    }

    err = cloud_init(false);
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Could not start the cloud thread (err %d), no direct publish", err);
        init_status = err;
        return err;
    }
    cloud_set_attempt_limit(DIRECT_ATTEMPT_LIMIT);

    backlog_ready = (backlog_init() == EXIT_SUCCESS);
    if (!backlog_ready) {
        LOG_WRN("No SD card, the values are not saved while no connection is available");
    }
    addr_ready = true;
    init_status = EXIT_SUCCESS;

    return EXIT_SUCCESS;
}

/**
 * @brief 測定値を知らせる (測定のたびに呼ぶ)
 *
 * 測定値は SD カードに保存する (ゲートウェイにつながっているときは送信済みとして. つながって
 * いないときは未送信として). BLE がゲートウェイにつながっていない測定が続いたら, または
 * ゲートウェイにつながっていて, SD カードに未送信の記録があるときは WiFi と MQTT を動かして,
 * 未送信の記録を古い順に送る (測った時刻をそのまま payload に入れる). SD カードや時刻が
 * ないときは保存できないので, 動かしていて MQTT につながっていれば, 測定値をそのまま送る.
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍] (湿度がなければ THERMO_HUMIDITY_NONE)
 * @param[in] pressure_x10 気圧 [hPa の 10 倍] (気圧がなければ THERMO_PRESSURE_NONE)
 */
void direct_sample(int16_t temp_x10, uint16_t humidity_x10, uint16_t pressure_x10)
{
    bool connected = false;   /* ゲートウェイにつながっているか */
    bool saved = false;       /* SD カードに保存したか */
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
    } else if (ble_failures < DIRECT_BLE_FAIL_LIMIT) {
        ble_failures++;
    }
    was_connected = connected;

    /* 測定値を保存する (ゲートウェイにつながっていれば, ゲートウェイが送るので, 送信済み) */
    saved = save_value(temp_x10, humidity_x10, pressure_x10, connected);

    /* WiFi が要るのはつながらない測定が続くときと, つながっていて, 未送信の記録があるとき */
    wifi_wanted = ((!connected) && (ble_failures >= DIRECT_BLE_FAIL_LIMIT)) ||
                  (connected && (backlog_pending() > 0U));

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

    /* 保存できなかった測定値は送れるときだけ, そのまま送る */
    if ((!saved) && (!connected)) {
        publish_live(temp_x10, humidity_x10, pressure_x10);
    }

    if (active) {
        flush_backlog();
    }
}

/**
 * @brief 直接の送信を動かしているか (動かしている間は WiFi を時刻の取得に使わない)
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
    queued = 0U;
    LOG_WRN("Start sending over WiFi (%u samples without the gateway, %u records on the SD card)",
            ble_failures, backlog_pending());
}

/**
 * 測定値を 1 件, そのまま送る (動かしていて, MQTT につながっていて, 先に送る記録がなければ)
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍]
 * @param[in] pressure_x10 気圧 [hPa の 10 倍]
 */
static void publish_live(int16_t temp_x10, uint16_t humidity_x10, uint16_t pressure_x10)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    /* つながっていて, 先に送る記録がなければ, すぐに送る (順序を保つ) */
    if (!(active && cloud_is_connected() && (backlog_pending() == 0U))) {
        return;
    }
    err = cloud_publish_temperature(&node_addr, temp_x10, humidity_x10, pressure_x10);
    if (err != EXIT_SUCCESS) {
        LOG_WRN("Could not queue the temperature (err %d)", err);
    }
}

/**
 * 測定値を SD カードに保存する (SD カードがない, または時刻がないときは保存しない)
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍]
 * @param[in] pressure_x10 気圧 [hPa の 10 倍]
 * @param[in] sent         送信済みとして保存するか
 * @retval true  保存した
 * @retval false 保存しなかった (SD カードがない, 時刻がない, 失敗)
 */
static bool save_value(int16_t temp_x10, uint16_t humidity_x10, uint16_t pressure_x10, bool sent)
{
    struct backlog_record rec = {0}; /* 保存する記録 */
    int err = EXIT_SUCCESS;          /* エラーコード */

    if (!backlog_ready) {
        return false;
    }

    /* 測った UNIX 時刻が要る (あとから送るときに, そのまま payload に入れる) */
    err = node_time_unix(&rec.unix_s);
    if (err != EXIT_SUCCESS) {
        if (!warned_no_time) {
            LOG_WRN("No time in the RTC, the values cannot be saved on the SD card");
            warned_no_time = true;
        }
        return false;
    }
    warned_no_time = false;

    rec.temp_x10 = temp_x10;
    rec.humidity_x10 = humidity_x10;
    rec.pressure_x10 = pressure_x10;
    err = backlog_append(&rec, sent);
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Could not save the value on the SD card (err %d)", err);
        return false;
    }

    return true;
}

/**
 * SD カードの記録を古い順に, 送信のキューに入れる (MQTT につながっているとき). 送れた分は
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

    /* 先の記録をキューに入れる (キューを満杯にしない) */
    while ((queued < backlog_pending()) && (cloud_pending() < FLUSH_QUEUE_MAX)) {
        err = backlog_read(&rec, queued);
        if (err != EXIT_SUCCESS) {
            break;
        }

        if (queued == 0U) {
            sent_mark = cloud_sent_count();
        }
        err = cloud_publish_temperature_at(&node_addr, rec.temp_x10, rec.humidity_x10,
                                           rec.pressure_x10, rec.unix_s);
        if (err != EXIT_SUCCESS) {
            break;
        }
        queued++;
    }
}

/**
 * @brief SD カードが使えるかを確かめる (使えなければ, マウントをやり直す. シェルの `thermo sd` 用)
 *
 * 起動のときに SD カードをマウントできなかったときも, カードを挿し直したあとに, 再起動せずに
 * 確かめられる. 失敗の理由は backlog_init() のログに出る. すでに使えているときは
 * マウントし直さない (測定値の保存の途中に, 外さないため).
 *
 * @param[out] pending 未送信の記録の数 (成功のときだけ)
 * @retval EXIT_SUCCESS SD カードが使える
 * @retval -ENODEV      SD カードがない (またはマウントできない)
 */
int direct_sd_check(unsigned int *pending)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    if (!backlog_ready) {
        err = backlog_init();
        if (err != EXIT_SUCCESS) {
            return err;
        }
        backlog_ready = true;
    }
    *pending = backlog_pending();

    return EXIT_SUCCESS;
}

/**
 * @brief direct_init() の結果を返す (シェルの `thermo sd` 用)
 *
 * 失敗していると, 測定値を保存しない (SD カードがマウントできていても). 理由は起動のログの
 * direct_init() の失敗にも出る.
 *
 * @retval EXIT_SUCCESS direct_init() が成功した
 * @retval negative     direct_init() が失敗した (負の errno). まだ呼んでいなければ -EAGAIN
 */
int direct_init_status(void)
{
    return init_status;
}
