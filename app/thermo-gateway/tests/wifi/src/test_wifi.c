/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief wifi_link.c の単体テスト
 *
 * net_if と net_mgmt の関数を, FFF のモックに置き換える. 接続の要求のモック動作の中で,
 * net_mgmt のイベントのコールバック (wifi_link.c が登録したもの) を, 直接呼んで, WiFi の
 * イベントを再現する. 次を確認する.
 *  - 接続の要求のパラメータ (SSID, パスワード, セキュリティの種類)
 *  - 接続の成功, 失敗, タイムアウト
 *  - 切断のイベントと, 切断の要求
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <errno.h>  /* ENODEV EIO ECONNREFUSED ETIMEDOUT */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* memcpy memset strlen */

#include "wifi_link.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(struct net_if *, net_if_get_default)
FAKE_VOID_FUNC(net_mgmt_add_event_callback, struct net_mgmt_event_callback *)
FAKE_VALUE_FUNC(int, net_mgmt_NET_REQUEST_WIFI_CONNECT, uint64_t, struct net_if *, void *, size_t)
FAKE_VALUE_FUNC(int, net_mgmt_NET_REQUEST_WIFI_DISCONNECT, uint64_t, struct net_if *, void *,
                size_t)

/** 接続の要求の間に, 再現するイベント */
enum scenario {
    SCENARIO_NONE,             /**< 何も起きない (タイムアウト) */
    SCENARIO_IP,               /**< IPv4 アドレスを取得する */
    SCENARIO_CONNECT_FAILED,   /**< 接続に失敗する (status != 0) */
    SCENARIO_CONNECTED_THEN_IP /**< 接続に成功して (status == 0), IPv4 アドレスを取得する */
};

/** ネットワークインターフェースの代わり (ポインタの値だけを使う. 中身は参照しない) */
#define FAKE_IFACE ((struct net_if *)(uintptr_t)0x1000U)
/** 接続の要求の間に再現するイベント */
static enum scenario scenario;
/** 接続の要求に渡されたパラメータの写し */
static struct wifi_connect_req_params captured;
/** 接続の要求に渡された SSID の写し */
static char captured_ssid[33U];
/** 接続の要求に渡されたパスワードの写し */
static char captured_psk[65U];
/** wifi_link.c が登録した, イベントのコールバック */
static net_mgmt_event_handler_t handler;

/**
 * イベントのコールバックを呼ぶ
 *
 * @param[in] event イベント
 * @param[in] status イベントの情報 (struct wifi_status の status. NULL なら, 情報なし)
 */
static void send_event(uint64_t event, const int *status)
{
    struct wifi_status info = {0};           /* 機器の情報の広告データ */
    struct net_mgmt_event_callback cb = {0}; /* コールバックの登録情報 */

    if (status != NULL) {
        info.status = *status;
        cb.info = &info;
    }
    handler(&cb, event, FAKE_IFACE);
}

/**
 * net_mgmt(NET_REQUEST_WIFI_CONNECT) のモック動作 (パラメータを記録して, イベントを再現する)
 *
 * @param[in] request 使用しない
 * @param[in] iface 使用しない
 * @param[in] data パラメータ (struct wifi_connect_req_params)
 * @param[in] len 使用しない
 * @return 0
 */
static int fake_connect(uint64_t request, struct net_if *iface, void *data, size_t len)
{
    /* 接続のパラメータ */
    const struct wifi_connect_req_params *params = (const struct wifi_connect_req_params *)data;
    int failed = -1; /* 失敗の状態 */
    int ok = 0;      /* 成功の状態 */

    ARG_UNUSED(request);
    ARG_UNUSED(iface);
    ARG_UNUSED(len);

    captured = *params;
    (void)memcpy(captured_ssid, params->ssid, params->ssid_length);
    captured_ssid[params->ssid_length] = '\0';
    (void)memcpy(captured_psk, params->psk, params->psk_length);
    captured_psk[params->psk_length] = '\0';

    if (scenario == SCENARIO_IP) {
        send_event(NET_EVENT_IPV4_ADDR_ADD, NULL);
    } else if (scenario == SCENARIO_CONNECT_FAILED) {
        send_event(NET_EVENT_WIFI_CONNECT_RESULT, &failed);
    } else if (scenario == SCENARIO_CONNECTED_THEN_IP) {
        send_event(NET_EVENT_WIFI_CONNECT_RESULT, &ok);
        send_event(NET_EVENT_IPV4_ADDR_ADD, NULL);
    } else {
        /* 何も起きない */
    }

    return 0;
}

/**
 * 各テストの前に, モックを初期状態に戻して, コールバックを登録して, 切断した状態にする
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(net_if_get_default);
    RESET_FAKE(net_mgmt_add_event_callback);
    RESET_FAKE(net_mgmt_NET_REQUEST_WIFI_CONNECT);
    RESET_FAKE(net_mgmt_NET_REQUEST_WIFI_DISCONNECT);
    FFF_RESET_HISTORY();
    net_if_get_default_fake.return_val = FAKE_IFACE;
    net_mgmt_NET_REQUEST_WIFI_CONNECT_fake.custom_fake = fake_connect;
    scenario = SCENARIO_IP;
    (void)memset(&captured, 0, sizeof(captured));

    zassert_equal(wifi_link_init(), EXIT_SUCCESS);
    handler = net_mgmt_add_event_callback_fake.arg0_history[0]->handler;

    /* 前のテストの状態 (接続済み) を, 戻す */
    send_event(NET_EVENT_WIFI_DISCONNECT_RESULT, NULL);
    zassert_false(wifi_link_is_up());
    RESET_FAKE(net_mgmt_NET_REQUEST_WIFI_CONNECT);
    net_mgmt_NET_REQUEST_WIFI_CONNECT_fake.custom_fake = fake_connect;
}

/** 初期化は, WiFi のイベントと, IPv4 のイベントの, 2 つのコールバックを登録する */
ZTEST(wifi, test_init_registers_callbacks)
{
    const struct net_mgmt_event_callback *wifi_cb =
            net_mgmt_add_event_callback_fake.arg0_history[0];
    const struct net_mgmt_event_callback *ipv4_cb =
            net_mgmt_add_event_callback_fake.arg0_history[1];

    /* 期待: WiFi (接続の結果, 切断) と IPv4 (アドレスの取得) の, 2 つを, 同じ処理で登録する */
    zassert_equal(net_mgmt_add_event_callback_fake.call_count, 2U);
    zassert_equal(wifi_cb->event_mask,
                  NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    zassert_equal(ipv4_cb->event_mask, NET_EVENT_IPV4_ADDR_ADD);
    zassert_equal(wifi_cb->handler, ipv4_cb->handler);
}

/** 接続して, IPv4 アドレスを取得したら, 成功して, 接続した状態になる */
ZTEST(wifi, test_connect_success)
{
    /* 期待: 要求を 1 回出して, IPv4 アドレスの取得で成功する (接続した状態になる) */
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), EXIT_SUCCESS);

    zassert_true(wifi_link_is_up());
    zassert_equal(net_mgmt_NET_REQUEST_WIFI_CONNECT_fake.call_count, 1U);
    zassert_equal(net_mgmt_NET_REQUEST_WIFI_CONNECT_fake.arg1_val, FAKE_IFACE);
}

/** 接続の結果 (成功) のイベントだけでは, 完了にならず, IPv4 アドレスの取得まで待つ */
ZTEST(wifi, test_connect_waits_for_ip)
{
    scenario = SCENARIO_CONNECTED_THEN_IP;

    /* 期待: 接続の成功のイベントだけでは完了せず, アドレスの取得まで待つ */
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), EXIT_SUCCESS);
    zassert_true(wifi_link_is_up());
}

/** パスワードがあれば PSK で, 空なら暗号化なしで, 接続を要求する */
ZTEST(wifi, test_connect_parameters)
{
    /* パスワードあり: PSK で, SSID とパスワードを, そのまま渡す */
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), EXIT_SUCCESS);
    zassert_str_equal(captured_ssid, "home-ap");
    zassert_equal(captured.ssid_length, strlen("home-ap"));
    zassert_str_equal(captured_psk, "secret-pass");
    zassert_equal(captured.psk_length, strlen("secret-pass"));
    zassert_equal(captured.security, WIFI_SECURITY_TYPE_PSK);
    zassert_equal(captured.channel, WIFI_CHANNEL_ANY);
    zassert_equal(captured.band, WIFI_FREQ_BAND_UNKNOWN);

    /* パスワードなし: 暗号化なしで要求する (先に, 切断した状態にする) */
    send_event(NET_EVENT_WIFI_DISCONNECT_RESULT, NULL);
    zassert_equal(wifi_link_connect("open-ap", "", K_MSEC(100)), EXIT_SUCCESS);
    zassert_str_equal(captured_ssid, "open-ap");
    zassert_equal(captured.psk_length, 0U);
    zassert_equal(captured.security, WIFI_SECURITY_TYPE_NONE);
}

/** すでに接続していれば, 新しい要求はせずに, すぐ成功する */
ZTEST(wifi, test_connect_already_up)
{
    /* 期待: 接続済みなら, 新しい要求は出さない */
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), EXIT_SUCCESS);
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), EXIT_SUCCESS);

    zassert_equal(net_mgmt_NET_REQUEST_WIFI_CONNECT_fake.call_count, 1U);
}

/** ネットワークインターフェースがなければ, -ENODEV */
ZTEST(wifi, test_connect_no_interface)
{
    net_if_get_default_fake.return_val = NULL;

    /* 期待: インターフェースがなければ -ENODEV で, 要求しない */
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), -ENODEV);
    zassert_equal(net_mgmt_NET_REQUEST_WIFI_CONNECT_fake.call_count, 0U);
}

/** 接続の要求に失敗したら, そのエラーを返す */
ZTEST(wifi, test_connect_request_failure)
{
    net_mgmt_NET_REQUEST_WIFI_CONNECT_fake.custom_fake = NULL;
    net_mgmt_NET_REQUEST_WIFI_CONNECT_fake.return_val = -EIO;

    /* 期待: 要求の失敗を返して, 接続した状態にならない */
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), -EIO);
    zassert_false(wifi_link_is_up());
}

/** 接続の結果が失敗 (SSID やパスワードの誤りなど) なら, -ECONNREFUSED */
ZTEST(wifi, test_connect_refused)
{
    scenario = SCENARIO_CONNECT_FAILED;

    /* 期待: 接続の失敗のイベントは -ECONNREFUSED */
    zassert_equal(wifi_link_connect("home-ap", "wrong", K_MSEC(100)), -ECONNREFUSED);
    zassert_false(wifi_link_is_up());
}

/** 時間内にイベントが来なければ, -ETIMEDOUT */
ZTEST(wifi, test_connect_timeout)
{
    scenario = SCENARIO_NONE;

    /* 期待: イベントが来なければ, 時間切れで -ETIMEDOUT */
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(50)), -ETIMEDOUT);
    zassert_false(wifi_link_is_up());
}

/** 失敗したあとの, 再接続は, 前の失敗の結果を引きずらない */
ZTEST(wifi, test_connect_after_failure)
{
    scenario = SCENARIO_CONNECT_FAILED;
    /* 期待: 失敗のあとの再接続は, 前の結果の影響を受けずに成功する */
    zassert_equal(wifi_link_connect("home-ap", "wrong", K_MSEC(100)), -ECONNREFUSED);

    scenario = SCENARIO_IP;
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), EXIT_SUCCESS);
}

/** 切断のイベントで, 接続していない状態になる. 知らないイベントは, 無視する */
ZTEST(wifi, test_disconnect_event_and_unknown_event)
{
    /* 期待: 知らないイベントは無視して, 切断のイベントで, 接続していない状態になる */
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), EXIT_SUCCESS);

    send_event(NET_EVENT_IPV4_ADDR_DEL, NULL);
    zassert_true(wifi_link_is_up());

    send_event(NET_EVENT_WIFI_DISCONNECT_RESULT, NULL);
    zassert_false(wifi_link_is_up());
}

/** 切断を要求すると, 接続していない状態にして, 切断の要求を出す */
ZTEST(wifi, test_disconnect)
{
    /* 期待: 切断を要求して, 接続していない状態になる */
    zassert_equal(wifi_link_connect("home-ap", "secret-pass", K_MSEC(100)), EXIT_SUCCESS);

    wifi_link_disconnect();

    zassert_false(wifi_link_is_up());
    zassert_equal(net_mgmt_NET_REQUEST_WIFI_DISCONNECT_fake.call_count, 1U);
}

/** 切断の要求に失敗しても, 接続していない状態にする */
ZTEST(wifi, test_disconnect_request_failure)
{
    net_mgmt_NET_REQUEST_WIFI_DISCONNECT_fake.return_val = -EIO;

    wifi_link_disconnect();

    /* 期待: 要求が失敗しても, 接続していない状態にする */
    zassert_false(wifi_link_is_up());
    zassert_equal(net_mgmt_NET_REQUEST_WIFI_DISCONNECT_fake.call_count, 1U);
}

/** ネットワークインターフェースがなければ, 切断の要求は出さない */
ZTEST(wifi, test_disconnect_no_interface)
{
    net_if_get_default_fake.return_val = NULL;

    wifi_link_disconnect();

    /* 期待: インターフェースがなければ, 要求を出さない */
    zassert_equal(net_mgmt_NET_REQUEST_WIFI_DISCONNECT_fake.call_count, 0U);
}

ZTEST_SUITE(wifi, NULL, NULL, before, NULL, NULL);
