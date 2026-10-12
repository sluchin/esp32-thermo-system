/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief direct.c の単体テスト
 *
 * ble と cfg と cloud と backlog の関数を FFF のモックに置き換えて, 次を確認する.
 *  - 初期化: ノードのアドレスを取って, 止めた状態で送信のスレッドを作って,
 * あきらめる回数を決めること
 *  - BLE がつながっていない測定が 5 回続いたら, 直接の送信を始めること (つながったら, 数え直す)
 *  - 設定が揃っていなければ, 始めないこと
 *  - 測定値を, 測定のたびに SD カードに保存すること (ゲートウェイにつながっていれば送信済み,
 * つながって いなければ未送信. SD カードや時刻がなければ, 保存しない. 失敗しても続ける)
 *  - SD カードに未送信の記録があれば, WiFi と MQTT を動かして, 古い順に送り, 送れた分を,
 * 送り終えたことにすること (キューを満杯にしない. 失敗しても続ける)
 *  - SD カードに保存できないときは, 動かしていて MQTT につながっていれば, 測定値をそのまま送ること
 *  - ゲートウェイにつながって, 送るものがなくなったら, 止めること
 *  - あきらめたら, ゲートウェイにつながるまで, やり直さないこと
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <errno.h>   /* ENOENT EIO ENOMSG ENODATA ENOSPC */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* int16_t uint16_t */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* memset */

#include "backlog.h"
#include "ble.h"
#include "cfg.h"
#include "cloud.h"
#include "direct.h"
#include "node_time.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, backlog_init)
FAKE_VALUE_FUNC(int, backlog_append, const struct backlog_record *, bool)
FAKE_VALUE_FUNC(unsigned int, backlog_pending)
FAKE_VALUE_FUNC(int, backlog_read, struct backlog_record *, unsigned int)
FAKE_VALUE_FUNC(int, backlog_commit, unsigned int)
FAKE_VALUE_FUNC(int, node_time_unix, int64_t *)
FAKE_VALUE_FUNC(bool, ble_is_connected)
FAKE_VALUE_FUNC(int, ble_get_address, bt_addr_le_t *)
FAKE_VALUE_FUNC(bool, cfg_is_complete)
FAKE_VALUE_FUNC(int, cloud_init, bool)
FAKE_VOID_FUNC(cloud_set_enabled, bool)
FAKE_VOID_FUNC(cloud_set_attempt_limit, unsigned int)
FAKE_VALUE_FUNC(bool, cloud_has_given_up)
FAKE_VALUE_FUNC(int, cloud_publish_temperature, const bt_addr_le_t *, int16_t, uint16_t)
FAKE_VALUE_FUNC(int, cloud_publish_temperature_at, const bt_addr_le_t *, int16_t, uint16_t, int64_t)
FAKE_VALUE_FUNC(bool, cloud_is_connected)
FAKE_VALUE_FUNC(unsigned int, cloud_pending)
FAKE_VALUE_FUNC(uint32_t, cloud_sent_count)

/** 試験の温度 [℃ の 10 倍] */
#define TEMP_X10     235
/** 試験の湿度 [% の 10 倍] */
#define HUMIDITY_X10 450U
/** 試験のノードのアドレスを埋める値 */
#define ADDR_BYTE    0x5A

/** SD カードの記録の最大数 (試験用の, 記録の置き場) */
#define STORE_MAX 8U
/** 試験の, 時刻 (UNIX 時刻 [s]) */
#define NOW_SEC   1790000000LL

/** SD カードに保存された記録 (古い順) */
static struct backlog_record store[STORE_MAX];
/** store の記録が, 送信済みか */
static bool store_sent[STORE_MAX];
/** store の記録の数 */
static unsigned int store_count;
/** backlog_read() の戻り値 (0 以外なら, 読めない) */
static int read_error;
/** backlog_commit() の戻り値 */
static int commit_return;
/** cloud_pending() の戻り値 */
static unsigned int pending_value;
/** cloud_sent_count() の戻り値 */
static uint32_t sent_value;

/**
 * ble_get_address() のモック動作 (決まったアドレスを返す)
 *
 * @param[out] addr アドレス
 * @return 0
 */
static int fake_address(bt_addr_le_t *addr)
{
    (void)memset(addr, ADDR_BYTE, sizeof(*addr));

    return 0;
}

/**
 * backlog_append() のモック動作 (store に追加する. 満杯なら, -ENOSPC)
 *
 * @param[in] rec  記録
 * @param[in] sent 送信済みか
 * @return 0 (成功), -ENOSPC (満杯)
 */
static int fake_append(const struct backlog_record *rec, bool sent)
{
    if (store_count >= STORE_MAX) {
        return -ENOSPC;
    }
    store[store_count] = *rec;
    store_sent[store_count] = sent;
    store_count++;

    return 0;
}

/**
 * backlog_pending() のモック動作
 *
 * @return 送っていない記録の数
 */
static unsigned int fake_pending(void)
{
    unsigned int n = 0U; /* 送っていない記録の数 */
    unsigned int i = 0U; /* ループ用の添字 */

    for (i = 0U; i < store_count; i++) {
        if (!store_sent[i]) {
            n++;
        }
    }

    return n;
}

/**
 * backlog_read() のモック動作 (古いほうから index 番目)
 *
 * @param[out] rec   記録
 * @param[in]  index 古いほうからの順番
 * @return read_error (0 以外なら, 読めない), そうでなければ 0
 */
static int fake_read_record(struct backlog_record *rec, unsigned int index)
{
    unsigned int seen = 0U; /* 数えた, 送っていない記録の数 */
    unsigned int i = 0U;    /* ループ用の添字 */

    if (read_error != 0) {
        return read_error;
    }
    for (i = 0U; i < store_count; i++) {
        if (!store_sent[i]) {
            if (seen == index) {
                *rec = store[i];
                return 0;
            }
            seen++;
        }
    }

    return -ENOENT;
}

/**
 * backlog_commit() のモック動作 (古いほうから count 件を, 送信済みにする)
 *
 * @param[in] count 送り終えた数
 * @return commit_return
 */
static int fake_commit(unsigned int count)
{
    unsigned int i = 0U; /* ループ用の添字 */

    for (i = 0U; (i < store_count) && (count > 0U); i++) {
        if (!store_sent[i]) {
            store_sent[i] = true;
            count--;
        }
    }

    return commit_return;
}

/**
 * node_time_unix() のモック動作 (固定の時刻を返す)
 *
 * @param[out] unix_s UNIX 時刻 [s]
 * @return 0
 */
static int fake_unix(int64_t *unix_s)
{
    *unix_s = NOW_SEC;

    return 0;
}

/**
 * cloud_pending() のモック動作
 *
 * @return pending_value
 */
static unsigned int fake_cloud_pending(void)
{
    return pending_value;
}

/**
 * cloud_sent_count() のモック動作
 *
 * @return sent_value
 */
static uint32_t fake_cloud_sent(void)
{
    return sent_value;
}

/**
 * 各テストの前に, モックを初期状態 (ゲートウェイにつながっていない. 設定は揃っている) に戻して,
 * 初期化する
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(backlog_init);
    RESET_FAKE(backlog_append);
    RESET_FAKE(backlog_pending);
    RESET_FAKE(backlog_read);
    RESET_FAKE(backlog_commit);
    RESET_FAKE(node_time_unix);
    RESET_FAKE(cloud_publish_temperature_at);
    RESET_FAKE(cloud_is_connected);
    RESET_FAKE(cloud_pending);
    RESET_FAKE(cloud_sent_count);
    RESET_FAKE(ble_is_connected);
    RESET_FAKE(ble_get_address);
    RESET_FAKE(cfg_is_complete);
    RESET_FAKE(cloud_init);
    RESET_FAKE(cloud_set_enabled);
    RESET_FAKE(cloud_set_attempt_limit);
    RESET_FAKE(cloud_has_given_up);
    RESET_FAKE(cloud_publish_temperature);
    FFF_RESET_HISTORY();
    ble_get_address_fake.custom_fake = fake_address;
    cfg_is_complete_fake.return_val = true;
    backlog_init_fake.return_val = -ENODEV; /* 既定は, SD カードなし */
    backlog_append_fake.custom_fake = fake_append;
    backlog_pending_fake.custom_fake = fake_pending;
    backlog_read_fake.custom_fake = fake_read_record;
    backlog_commit_fake.custom_fake = fake_commit;
    node_time_unix_fake.custom_fake = fake_unix;
    cloud_is_connected_fake.return_val = true;
    (void)memset(store, 0, sizeof(store));
    (void)memset(store_sent, 0, sizeof(store_sent));
    store_count = 0U;
    read_error = 0;
    commit_return = 0;
    pending_value = 0U;
    sent_value = 0U;
    cloud_pending_fake.custom_fake = fake_cloud_pending;
    cloud_sent_count_fake.custom_fake = fake_cloud_sent;
    zassert_equal(direct_init(), EXIT_SUCCESS);
    RESET_FAKE(cloud_init);
    RESET_FAKE(cloud_set_attempt_limit);
}

/**
 * SD カードを使える状態で, 初期化し直す (記録は, 空)
 */
static void init_with_sd(void)
{
    backlog_init_fake.return_val = 0;
    zassert_equal(direct_init(), EXIT_SUCCESS);
}

/**
 * SD カードに, 送っていない記録を, 指定の数だけ置く (時刻は, 1000 から 1 秒ずつ. 温度は 100 から 1
 * ずつ)
 *
 * @param[in] count 数 (STORE_MAX 以下)
 */
static void put_records(unsigned int count)
{
    unsigned int i = 0U; /* ループ用の添字 */

    for (i = 0U; i < count; i++) {
        store[i].unix_s = 1000LL + (int64_t)i;
        store[i].temp_x10 = (int16_t)(100U + i);
        store[i].humidity_x10 = (uint16_t)(400U + i);
    }
    store_count = count;
    (void)memset(store_sent, 0, sizeof(store_sent));
}

/**
 * 測定を指定の回数だけ知らせる
 *
 * @param[in] count 回数
 */
static void samples(unsigned int count)
{
    unsigned int i = 0U; /* ループ用の添字 */

    for (i = 0U; i < count; i++) {
        direct_sample(TEMP_X10, HUMIDITY_X10);
    }
}

/** 初期化は, アドレスを取って, 止めた状態で送信のスレッドを作り, あきらめる回数を決める */
ZTEST(direct, test_init)
{
    zassert_equal(direct_init(), EXIT_SUCCESS);

    zassert_equal(ble_get_address_fake.call_count, 2U); /* before() の分と合わせて 2 回 */
    zassert_equal(cloud_init_fake.call_count, 1U);
    zassert_false(cloud_init_fake.arg0_val);
    zassert_equal(cloud_set_attempt_limit_fake.call_count, 1U);
    zassert_equal(cloud_set_attempt_limit_fake.arg0_val, DIRECT_ATTEMPT_LIMIT);
    zassert_false(direct_active());
}

/** アドレスを取れなければ, 失敗を返して, 何もしない (測定を知らせても, 動かさない) */
ZTEST(direct, test_init_address_failure)
{
    ble_get_address_fake.custom_fake = NULL;
    ble_get_address_fake.return_val = -ENOENT;

    zassert_equal(direct_init(), -ENOENT);
    zassert_equal(cloud_init_fake.call_count, 0U);

    samples(DIRECT_BLE_FAIL_LIMIT + 2U);
    zassert_equal(ble_is_connected_fake.call_count, 0U);
    zassert_equal(cloud_set_enabled_fake.call_count, 0U);
}

/** 送信のスレッドを作れなければ, 失敗を返して, 動かさない */
ZTEST(direct, test_init_cloud_failure)
{
    cloud_init_fake.return_val = -EIO;

    zassert_equal(direct_init(), -EIO);

    samples(DIRECT_BLE_FAIL_LIMIT + 2U);
    zassert_equal(cloud_set_enabled_fake.call_count, 0U);
}

/** ゲートウェイにつながっていない測定が 4 回では, 始めない. 5 回目で, 始める (送るのは, まだ) */
ZTEST(direct, test_starts_after_five_failures)
{
    samples(DIRECT_BLE_FAIL_LIMIT - 1U);
    zassert_equal(cloud_set_enabled_fake.call_count, 0U);
    zassert_false(direct_active());

    samples(1U);
    zassert_equal(cloud_set_enabled_fake.call_count, 1U);
    zassert_true(cloud_set_enabled_fake.arg0_val);
    zassert_true(direct_active());
    /* SD カードがないので, 始めた測定から, そのまま送る */
    zassert_equal(cloud_publish_temperature_fake.call_count, 1U);
}

/** つながった測定があれば, 数え直す (4 回, つながって, 4 回では, 始めない) */
ZTEST(direct, test_connected_resets_the_count)
{
    samples(DIRECT_BLE_FAIL_LIMIT - 1U);
    ble_is_connected_fake.return_val = true;
    samples(1U);
    ble_is_connected_fake.return_val = false;
    samples(DIRECT_BLE_FAIL_LIMIT - 1U);

    zassert_equal(cloud_set_enabled_fake.call_count, 0U);
    zassert_false(direct_active());
}

/** SD カードがないときは, 動かしている間, 測定のたびに, ノードのアドレスと測定値で, そのまま送る */
ZTEST(direct, test_publishes_every_sample_without_sd)
{
    samples(DIRECT_BLE_FAIL_LIMIT - 1U);
    zassert_equal(cloud_publish_temperature_fake.call_count, 0U);

    /* 始めた測定から送る */
    samples(1U);
    zassert_equal(cloud_publish_temperature_fake.call_count, 1U);
    zassert_equal(cloud_publish_temperature_fake.arg1_val, TEMP_X10);
    zassert_equal(cloud_publish_temperature_fake.arg2_val, HUMIDITY_X10);
    zassert_equal(((const uint8_t *)cloud_publish_temperature_fake.arg0_val)[0], ADDR_BYTE);

    samples(1U);
    zassert_equal(cloud_publish_temperature_fake.call_count, 2U);
}

/** キューが満杯などで, 送れなくても, 動かし続ける */
ZTEST(direct, test_publish_failure_keeps_going)
{
    cloud_publish_temperature_fake.return_val = -ENOMSG;

    samples(DIRECT_BLE_FAIL_LIMIT + 1U);

    zassert_equal(cloud_publish_temperature_fake.call_count, 2U);
    zassert_true(direct_active());
}

/** SD カードがなくて, MQTT につながる前 (または, つながっていても記録が残っている) は, 送らない */
ZTEST(direct, test_no_live_publish_before_mqtt)
{
    cloud_is_connected_fake.return_val = false;

    samples(DIRECT_BLE_FAIL_LIMIT + 2U);

    zassert_true(direct_active());
    zassert_equal(cloud_publish_temperature_fake.call_count, 0U);
}

/** 時刻がなくて保存できなくても, 送っていない記録が残っていれば, そのまま送らない (順序を保つ) */
ZTEST(direct, test_no_live_publish_while_records_remain)
{
    init_with_sd();
    put_records(2U);
    node_time_unix_fake.custom_fake = NULL;
    node_time_unix_fake.return_val = -ENODATA;

    samples(DIRECT_BLE_FAIL_LIMIT + 1U);

    zassert_equal(cloud_publish_temperature_fake.call_count, 0U);
}

/** 設定が揃っていなければ, 始めない (測定のたびに調べる). 揃ったら, 次の測定で始める */
ZTEST(direct, test_incomplete_settings_do_not_start)
{
    cfg_is_complete_fake.return_val = false;

    samples(DIRECT_BLE_FAIL_LIMIT + 3U);
    zassert_equal(cloud_set_enabled_fake.call_count, 0U);
    zassert_false(direct_active());

    cfg_is_complete_fake.return_val = true;
    samples(1U);
    zassert_equal(cloud_set_enabled_fake.call_count, 1U);
    zassert_true(direct_active());
}

/** 動かしている間に, ゲートウェイにつながったら, 止める (動かしていなければ, 止めない) */
ZTEST(direct, test_gateway_connection_stops_it)
{
    ble_is_connected_fake.return_val = true;
    samples(1U);
    zassert_equal(cloud_set_enabled_fake.call_count, 0U);

    ble_is_connected_fake.return_val = false;
    samples(DIRECT_BLE_FAIL_LIMIT);
    zassert_true(direct_active());

    ble_is_connected_fake.return_val = true;
    samples(1U);
    zassert_false(direct_active());
    zassert_equal(cloud_set_enabled_fake.call_count, 2U);
    zassert_false(cloud_set_enabled_fake.arg0_val);
}

/** あきらめたら, 止めて, ゲートウェイにつながるまで, やり直さない. つながったら, また始められる */
ZTEST(direct, test_gives_up_until_the_gateway_connects)
{
    samples(DIRECT_BLE_FAIL_LIMIT);
    zassert_true(direct_active());

    cloud_has_given_up_fake.return_val = true;
    samples(1U);
    zassert_false(direct_active());

    /* あきらめたあとは, ゲートウェイにつながらない測定が続いても, 始めない */
    cloud_has_given_up_fake.return_val = false;
    samples(DIRECT_BLE_FAIL_LIMIT + 2U);
    zassert_false(direct_active());
    zassert_equal(cloud_set_enabled_fake.call_count, 1U);

    /* つながったら, あきらめた状態が戻って, また 5 回続いたら, 始める */
    ble_is_connected_fake.return_val = true;
    samples(1U);
    ble_is_connected_fake.return_val = false;
    samples(DIRECT_BLE_FAIL_LIMIT);
    zassert_true(direct_active());
    zassert_equal(cloud_set_enabled_fake.call_count, 2U);
}

/** 初期化は, SD カードも使えるようにする. SD カードがなくても, 成功する */
ZTEST(direct, test_init_with_and_without_sd)
{
    zassert_equal(direct_init(), EXIT_SUCCESS);
    zassert_equal(backlog_init_fake.call_count, 2U); /* before() の分と合わせて 2 回 */

    init_with_sd();
    zassert_equal(backlog_init_fake.call_count, 3U);
}

/** SD カードがなければ, 保存しない */
ZTEST(direct, test_no_sd_does_not_save)
{
    samples(2U);

    zassert_equal(backlog_append_fake.call_count, 0U);
}

/** ゲートウェイにつながっていない間の測定値は, 未送信として, 測定のたびに SD カードに保存する */
ZTEST(direct, test_offline_values_are_saved_as_not_sent)
{
    init_with_sd();

    samples(DIRECT_BLE_FAIL_LIMIT - 1U);

    zassert_equal(backlog_append_fake.call_count, DIRECT_BLE_FAIL_LIMIT - 1U);
    zassert_equal(store[0].unix_s, NOW_SEC);
    zassert_equal(store[0].temp_x10, TEMP_X10);
    zassert_equal(store[0].humidity_x10, HUMIDITY_X10);
    zassert_false(store_sent[0]);
    zassert_false(backlog_append_fake.arg1_val);
    zassert_equal(cloud_publish_temperature_fake.call_count, 0U);
}

/** ゲートウェイにつながっている間の測定値は, 送信済みとして保存する (WiFi は, 動かさない) */
ZTEST(direct, test_connected_values_are_saved_as_sent)
{
    init_with_sd();
    ble_is_connected_fake.return_val = true;

    samples(3U);

    zassert_equal(backlog_append_fake.call_count, 3U);
    zassert_true(store_sent[0]);
    zassert_true(backlog_append_fake.arg1_val);
    zassert_equal(backlog_pending(), 0U);
    zassert_false(direct_active());
    zassert_equal(cloud_set_enabled_fake.call_count, 0U);
}

/** RTC に時刻がなければ, 保存しない (知らせるのは, 1 回だけ). 時刻が入ったら, 保存する */
ZTEST(direct, test_no_time_does_not_save)
{
    init_with_sd();
    node_time_unix_fake.custom_fake = NULL;
    node_time_unix_fake.return_val = -ENODATA;

    samples(3U);
    zassert_equal(backlog_append_fake.call_count, 0U);

    node_time_unix_fake.custom_fake = fake_unix;
    samples(1U);
    zassert_equal(backlog_append_fake.call_count, 1U);
}

/** SD カードへの保存に失敗しても (満杯など), 続ける */
ZTEST(direct, test_save_failure_keeps_going)
{
    init_with_sd();
    store_count = STORE_MAX; /* 満杯 (全て, 送り終えたことにして, 送らない) */
    (void)memset(store_sent, true, sizeof(store_sent));

    samples(2U);

    zassert_equal(backlog_append_fake.call_count, 2U);
    zassert_equal(store_count, STORE_MAX);
}

/** 動かしていても, MQTT につながる前は, 送らずに, SD カードに保存だけする */
ZTEST(direct, test_values_are_saved_until_mqtt_is_connected)
{
    init_with_sd();
    cloud_is_connected_fake.return_val = false;

    samples(DIRECT_BLE_FAIL_LIMIT + 2U);
    zassert_true(direct_active());
    zassert_equal(cloud_publish_temperature_fake.call_count, 0U);
    zassert_equal(cloud_publish_temperature_at_fake.call_count, 0U);
    zassert_equal(backlog_append_fake.call_count, DIRECT_BLE_FAIL_LIMIT + 2U);
}

/** MQTT につながったら, 保存した未送信の記録を, 古い順に送る (今の測定値も, 保存したあとに送る) */
ZTEST(direct, test_new_values_are_sent_through_the_store)
{
    init_with_sd();

    samples(DIRECT_BLE_FAIL_LIMIT);

    /* 5 回目で始めて, 5 件 (全て未送信) を, 古い順に送る */
    zassert_true(direct_active());
    zassert_equal(cloud_publish_temperature_fake.call_count, 0U);
    zassert_equal(cloud_publish_temperature_at_fake.call_count, DIRECT_BLE_FAIL_LIMIT);
    zassert_equal(cloud_publish_temperature_at_fake.arg3_val, NOW_SEC);
}

/** SD カードに送っていない記録があれば, ゲートウェイにつながっていても, WiFi で送る */
ZTEST(direct, test_backlog_starts_wifi_with_the_gateway_connected)
{
    init_with_sd();
    put_records(3U);
    ble_is_connected_fake.return_val = true;

    samples(1U);

    zassert_true(direct_active());
    zassert_true(cloud_set_enabled_fake.arg0_val);
    /* 古い順に, 測った時刻つきで, ノードのアドレスで, 送信のキューに入れる */
    zassert_equal(cloud_publish_temperature_at_fake.call_count, 3U);
    zassert_equal(cloud_publish_temperature_at_fake.arg1_history[0], 100);
    zassert_equal(cloud_publish_temperature_at_fake.arg2_history[0], 400U);
    zassert_equal(cloud_publish_temperature_at_fake.arg3_history[0], 1000LL);
    zassert_equal(cloud_publish_temperature_at_fake.arg3_history[2], 1002LL);
    zassert_equal(((const uint8_t *)cloud_publish_temperature_at_fake.arg0_history[0])[0],
                  ADDR_BYTE);
    zassert_equal(backlog_commit_fake.call_count, 0U);
}

/** 送れた分 (cloud_sent_count() の増えた分) を, 送り終えたことにする. 全て送り終えたら, 止める */
ZTEST(direct, test_commits_the_sent_records_and_stops)
{
    init_with_sd();
    put_records(3U);
    ble_is_connected_fake.return_val = true;
    samples(1U);

    sent_value = 2U;
    samples(1U);
    zassert_equal(backlog_commit_fake.call_count, 1U);
    zassert_equal(backlog_commit_fake.arg0_val, 2U);
    zassert_true(direct_active());

    sent_value = 3U;
    samples(1U);
    zassert_equal(backlog_commit_fake.call_count, 2U);
    zassert_equal(backlog_commit_fake.arg0_val, 1U);

    /* 送るものがなくなって, ゲートウェイにつながっているので, 止める */
    samples(1U);
    zassert_false(direct_active());
    zassert_false(cloud_set_enabled_fake.arg0_val);
}

/** 送れた数が, キューに入れた数より多い (ほかの送信も数えている) ときは, 送り終えたことにしない */
ZTEST(direct, test_sent_count_over_the_queue_is_ignored)
{
    init_with_sd();
    put_records(2U);
    ble_is_connected_fake.return_val = true;
    samples(1U);

    sent_value = 9U;
    samples(1U);
    zassert_equal(backlog_commit_fake.call_count, 0U);
}

/** 送信のキューが, いっぱいに近いときは, 記録を入れない */
ZTEST(direct, test_flush_respects_the_queue_limit)
{
    init_with_sd();
    put_records(3U);
    ble_is_connected_fake.return_val = true;
    pending_value = 4U;

    samples(2U);

    zassert_equal(cloud_publish_temperature_at_fake.call_count, 0U);

    pending_value = 0U;
    samples(1U);
    zassert_equal(cloud_publish_temperature_at_fake.call_count, 3U);
}

/** MQTT につながっていなければ, 記録を入れない */
ZTEST(direct, test_flush_waits_for_mqtt)
{
    init_with_sd();
    put_records(2U);
    ble_is_connected_fake.return_val = true;
    cloud_is_connected_fake.return_val = false;

    samples(2U);

    zassert_true(direct_active());
    zassert_equal(cloud_publish_temperature_at_fake.call_count, 0U);
}

/** 記録を読めなければ, 入れずに, 続ける */
ZTEST(direct, test_read_failure_keeps_going)
{
    init_with_sd();
    put_records(2U);
    ble_is_connected_fake.return_val = true;
    read_error = -EIO;

    samples(2U);

    zassert_true(direct_active());
    zassert_equal(cloud_publish_temperature_at_fake.call_count, 0U);
}

/** 送信のキューに入れられなければ, そこで止める (次の測定で, やり直す) */
ZTEST(direct, test_queue_failure_stops_the_flush)
{
    init_with_sd();
    put_records(3U);
    ble_is_connected_fake.return_val = true;
    cloud_publish_temperature_at_fake.return_val = -ENOMSG;

    samples(1U);

    zassert_equal(cloud_publish_temperature_at_fake.call_count, 1U);
}

/** 送り終えた数を SD カードに保存できなくても, 続ける (次の測定で, やり直す) */
ZTEST(direct, test_commit_failure_keeps_going)
{
    init_with_sd();
    put_records(2U);
    ble_is_connected_fake.return_val = true;
    samples(1U);

    commit_return = -EIO;
    sent_value = 1U;
    samples(1U);

    zassert_equal(backlog_commit_fake.call_count, 1U);
    zassert_true(direct_active());
}

/** SD カードの記録を送りたいが, 設定が揃っていなければ, 始めない */
ZTEST(direct, test_backlog_needs_complete_settings)
{
    init_with_sd();
    put_records(2U);
    ble_is_connected_fake.return_val = true;
    cfg_is_complete_fake.return_val = false;

    samples(3U);

    zassert_false(direct_active());
    zassert_equal(cloud_set_enabled_fake.call_count, 0U);
}

/** ゲートウェイにつながっていて, 記録を送るために, あきらめたら, つなぎ直すまで, やり直さない */
ZTEST(direct, test_backlog_gives_up_until_reconnected)
{
    init_with_sd();
    put_records(2U);
    ble_is_connected_fake.return_val = true;
    samples(1U);
    zassert_true(direct_active());

    cloud_has_given_up_fake.return_val = true;
    samples(1U);
    zassert_false(direct_active());

    cloud_has_given_up_fake.return_val = false;
    samples(3U);
    zassert_false(direct_active());

    /* 切れて, また, つながったら, もう一度, 試す */
    ble_is_connected_fake.return_val = false;
    samples(1U);
    ble_is_connected_fake.return_val = true;
    samples(1U);
    zassert_true(direct_active());
}

/** SD カードが起動のときに使えなかったら, direct_sd_check() が, マウントをやり直す */
ZTEST(direct, test_sd_check_mounts_again)
{
    unsigned int pending = 99U; /* 未送信の記録の数 */

    /* 起動のときは使えなかった (before() の初期化は, SD カードなし) */
    backlog_init_fake.return_val = -ENODEV;
    zassert_equal(direct_sd_check(&pending), -ENODEV);
    zassert_equal(pending, 99U);

    /* カードを挿し直したら, 使える */
    backlog_init_fake.return_val = 0;
    put_records(2U);
    zassert_equal(direct_sd_check(&pending), EXIT_SUCCESS);
    zassert_equal(pending, 2U);
}

/** SD カードがすでに使えているときは, マウントし直さない */
ZTEST(direct, test_sd_check_keeps_the_mount)
{
    unsigned int pending = 0U;      /* 未送信の記録の数 */
    unsigned int before_calls = 0U; /* 呼ぶ前の, backlog_init() の回数 */

    init_with_sd();
    before_calls = backlog_init_fake.call_count;
    zassert_equal(direct_sd_check(&pending), EXIT_SUCCESS);
    zassert_equal(backlog_init_fake.call_count, before_calls);
}

/** direct_init() の結果は, 成功なら 0, 失敗ならそのエラー (BLE のアドレス, クラウド) */
ZTEST(direct, test_init_status)
{
    zassert_equal(direct_init_status(), EXIT_SUCCESS); /* before() で, 成功している */

    ble_get_address_fake.custom_fake = NULL;
    ble_get_address_fake.return_val = -ENOENT;
    zassert_equal(direct_init(), -ENOENT);
    zassert_equal(direct_init_status(), -ENOENT);

    ble_get_address_fake.custom_fake = fake_address;
    cloud_init_fake.return_val = -EIO;
    zassert_equal(direct_init(), -EIO);
    zassert_equal(direct_init_status(), -EIO);
}

ZTEST_SUITE(direct, NULL, NULL, before, NULL, NULL);
