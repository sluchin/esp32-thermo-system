/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief WiFi (ステーションモード) の接続
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <errno.h>  /* ENODEV ETIMEDOUT ECONNREFUSED */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* strlen */

#include "wifi_link.h"

LOG_MODULE_REGISTER(thermo_wifi);

/** WiFi のイベント (接続の結果, 切断) のコールバック */
static struct net_mgmt_event_callback wifi_cb;
/** IPv4 のイベント (アドレスの取得) のコールバック */
static struct net_mgmt_event_callback ipv4_cb;

/** 接続の結果 (成功, または失敗) が確定したときに, 与えるセマフォ */
static K_SEM_DEFINE(result_sem, 0, 1)
        /** IPv4 アドレスを取得した状態か */
        static volatile bool ip_ready;
/** 接続の結果 (0 なら成功) */
static volatile int connect_status;

/**
 * net_mgmt のイベントのコールバック
 *
 * @param[in] cb    コールバック (info にイベントの情報がある)
 * @param[in] event イベント
 * @param[in] iface 使用しない
 */
static void event_handler(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
    const struct wifi_status *status = NULL; /* 接続の状態 */

    ARG_UNUSED(iface);

    if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
        status = (const struct wifi_status *)cb->info;
        if (status->status != 0) {
            LOG_ERR("WiFi connection failed (status %d)", status->status);
            connect_status = status->status;
            k_sem_give(&result_sem);
        }
    } else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        LOG_WRN("WiFi disconnected");
        ip_ready = false;
    } else if (event == NET_EVENT_IPV4_ADDR_ADD) {
        LOG_INF("WiFi connected (IPv4 address acquired)");
        connect_status = 0;
        ip_ready = true;
        k_sem_give(&result_sem);
    } else {
        LOG_DBG("Ignoring the net event 0x%llx", (unsigned long long)event);
    }
}

/**
 * @brief WiFi のイベント (接続の結果, 切断, IPv4 アドレスの取得) のコールバックを登録する
 *
 * @retval EXIT_SUCCESS 成功
 */
int wifi_link_init(void)
{
    /* WiFi のイベントと IPv4 のイベントは, 層が違うので別々のコールバックにして登録する */
    net_mgmt_init_event_callback(&wifi_cb, event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    net_mgmt_init_event_callback(&ipv4_cb, event_handler, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4_cb);

    return EXIT_SUCCESS;
}

/**
 * @brief アクセスポイントに接続して, IPv4 アドレスを取得するまで待つ
 *
 * すでに接続していれば, すぐ戻る.
 *
 * @param[in] ssid    SSID
 * @param[in] psk     パスワード (空の文字列なら, 暗号化なしで接続する)
 * @param[in] timeout 待つ時間
 * @retval EXIT_SUCCESS    接続して, IPv4 アドレスを取得した
 * @retval -ENODEV         ネットワークインターフェースがない
 * @retval -ETIMEDOUT      時間内に, 完了しなかった
 * @retval -ECONNREFUSED   接続に失敗した (SSID やパスワードの誤りなど)
 * @retval negative        接続の要求に失敗した (負の errno)
 */
int wifi_link_connect(const char *ssid, const char *psk, k_timeout_t timeout)
{
    struct net_if *iface = net_if_get_default(); /* ネットワークインターフェース */
    struct wifi_connect_req_params params = {0}; /* 接続のパラメータ */
    int err = EXIT_SUCCESS;                      /* エラーコード */

    if (iface == NULL) {
        return -ENODEV;
    }

    /* すでに接続していれば, つなぎ直さない */
    if (ip_ready) {
        return EXIT_SUCCESS;
    }

    /* パスワードがあれば WPA/WPA2 の PSK, なければ暗号化なしで接続する (チャンネルは自動) */
    params.ssid = (const uint8_t *)ssid;
    params.ssid_length = (uint8_t)strlen(ssid);
    params.psk = (const uint8_t *)psk;
    params.psk_length = (uint8_t)strlen(psk);
    params.security = ((params.psk_length > 0U) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE);
    params.band = WIFI_FREQ_BAND_UNKNOWN;
    params.channel = WIFI_CHANNEL_ANY;
    params.mfp = WIFI_MFP_OPTIONAL;
    params.timeout = SYS_FOREVER_MS;

    /* 前回の結果を捨ててから, 要求する (要求の直後にイベントが来ることもある) */
    connect_status = 0;
    k_sem_reset(&result_sem);

    err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
    if (err != 0) {
        LOG_ERR("WiFi connect request failed (err %d)", err);
        return err;
    }

    /* 接続の失敗 (イベント), または IPv4 アドレスの取得 (イベント) まで待つ */
    err = k_sem_take(&result_sem, timeout);
    if (err != 0) {
        LOG_ERR("WiFi connection timed out");
        return -ETIMEDOUT;
    }
    if (connect_status != 0) {
        return -ECONNREFUSED;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief IPv4 アドレスを取得した状態か調べる
 *
 * @return 接続していて, IPv4 アドレスがあれば true
 */
bool wifi_link_is_up(void)
{
    return ip_ready;
}

/**
 * @brief アクセスポイントから切断する (結果は待たない)
 */
void wifi_link_disconnect(void)
{
    struct net_if *iface = net_if_get_default(); /* ネットワークインターフェース */
    int err = EXIT_SUCCESS;                      /* エラーコード */

    /* 切断の完了を待たずに接続していない状態にする (再接続がすぐ始められるように) */
    ip_ready = false;
    if (iface == NULL) {
        return;
    }

    err = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
    if (err != 0) {
        LOG_WRN("WiFi disconnect request failed (err %d)", err);
    }
}
