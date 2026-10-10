/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief WiFi と SNTP による時刻の取得
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <errno.h>  /* ENOENT EHOSTUNREACH */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* memcpy */

#include "thermo_log.h"
#include "wifi_cfg.h"
#include "wifi_link.h"
#include "wifi_time.h"

LOG_MODULE_REGISTER(thermo_wifi_time, THERMO_LOG_LEVEL);

/** NTP のポート */
#define NTP_PORT "123"

static int query_sntp(const char *server, k_timeout_t timeout, int64_t *unix_s);
static int resolve_server(const char *server, struct sockaddr_storage *addr, socklen_t *len);

/**
 * @brief 設定 (SSID とパスワード) を読み込んで, WiFi の準備をする
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int wifi_time_init(void)
{
    int err = wifi_cfg_init(); /* エラーコード */

    if (err != 0) {
        return err;
    }

    return wifi_link_init();
}

/**
 * @brief WiFi に接続して, SNTP で UTC の UNIX 時刻を取り, WiFi を切る
 *
 * 成功しても失敗しても, 最後に WiFi を切る.
 *
 * @param[out] unix_s UTC の UNIX 時刻 [s]
 * @retval EXIT_SUCCESS    成功
 * @retval -ENOENT         WiFi の SSID が設定されていない
 * @retval -ETIMEDOUT      WiFi の接続か, SNTP の応答が, 時間内に完了しなかった
 * @retval -ECONNREFUSED   WiFi の接続に失敗した (SSID やパスワードの誤りなど)
 * @retval -EHOSTUNREACH   NTP サーバの名前を解決できなかった
 * @retval negative        そのほかの失敗 (負の errno)
 */
int wifi_time_fetch(int64_t *unix_s)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    if (!wifi_cfg_is_complete()) {
        LOG_ERR("The WiFi SSID is not set (use 'thermo set ssid <SSID>' in the shell)");
        return -ENOENT;
    }

    err = wifi_link_connect(wifi_cfg_get(WIFI_CFG_SSID), wifi_cfg_get(WIFI_CFG_PSK),
                            K_SECONDS(WIFI_TIME_CONNECT_S));
    if (err == 0) {
        err = query_sntp(CONFIG_THERMO_NTP_SERVER, K_SECONDS(WIFI_TIME_SNTP_S), unix_s);
    } else {
        LOG_ERR("Failed to connect to WiFi (err %d)", err);
    }

    /* 接続の途中で失敗したときも, つなぎ続けないように切る */
    wifi_link_disconnect();

    return err;
}

/**
 * NTP サーバに問い合わせて, UNIX 時刻を得る (システム時計は, 合わせない)
 *
 * @param[in]  server  NTP サーバのホスト名
 * @param[in]  timeout 応答を待つ時間
 * @param[out] unix_s  UTC の UNIX 時刻 [s]
 * @retval EXIT_SUCCESS  成功
 * @retval -EHOSTUNREACH サーバの名前を解決できなかった
 * @retval negative      問い合わせの失敗 (負の errno)
 */
static int query_sntp(const char *server, k_timeout_t timeout, int64_t *unix_s)
{
    struct sockaddr_storage addr = {0}; /* アドレス */
    socklen_t addr_len = 0;             /* アドレスの長さ [バイト] */
    struct sntp_ctx ctx = {0};          /* SNTP のコンテキスト */
    struct sntp_time ts = {0};          /* SNTP の応答の時刻 */
    int err = EXIT_SUCCESS;             /* エラーコード */

    err = resolve_server(server, &addr, &addr_len);
    if (err != 0) {
        return err;
    }

    err = sntp_init(&ctx, (struct sockaddr *)&addr, addr_len);
    if (err != 0) {
        LOG_ERR("SNTP init failed (err %d)", err);
        return err;
    }

    err = sntp_query(&ctx, (uint32_t)k_ticks_to_ms_ceil32(timeout.ticks), &ts);
    sntp_close(&ctx);
    if (err != 0) {
        LOG_ERR("SNTP query failed (err %d)", err);
        return err;
    }

    *unix_s = (int64_t)ts.seconds;
    LOG_INF("Time from SNTP (UNIX time %lld)", (long long)*unix_s);

    return EXIT_SUCCESS;
}

/**
 * サーバの名前を解決する
 *
 * @param[in]  server ホスト名
 * @param[out] addr   アドレスの保存先
 * @param[out] len    アドレスの長さの保存先
 * @retval EXIT_SUCCESS  成功
 * @retval -EHOSTUNREACH 名前を解決できなかった
 */
static int resolve_server(const char *server, struct sockaddr_storage *addr, socklen_t *len)
{
    /* 名前解決の条件 (UDP) */
    struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
    struct zsock_addrinfo *res = NULL;                           /* 名前解決の結果 */
    int err = zsock_getaddrinfo(server, NTP_PORT, &hints, &res); /* エラーコード */

    if (err != 0) {
        LOG_ERR("Resolving '%s' failed (err %d)", server, err);
        return -EHOSTUNREACH;
    }

    (void)memcpy(addr, res->ai_addr, res->ai_addrlen);
    *len = res->ai_addrlen;
    zsock_freeaddrinfo(res);

    return EXIT_SUCCESS;
}
