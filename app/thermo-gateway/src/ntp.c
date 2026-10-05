/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief SNTP による時刻の同期
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/clock.h>
#include <errno.h>  /* EHOSTUNREACH EAGAIN */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* memcpy */

#include "ntp.h"

LOG_MODULE_REGISTER(thermo_ntp);

/** NTP のポート */
#define NTP_PORT "123"

/** 同期したことがあるか */
static volatile bool synced;
/** 前回の同期の時刻 (k_uptime_get() の値 [ms]) */
static int64_t last_sync_ms;

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

/**
 * @brief NTP サーバに問い合わせて, システム時計を合わせる
 *
 * すでに同期していて, 前回の同期から CONFIG_THERMO_NTP_RESYNC_S 秒たっていなければ,
 * 問い合わせずに, すぐ戻る.
 *
 * @param[in] server  NTP サーバのホスト名
 * @param[in] timeout 応答を待つ時間
 * @retval EXIT_SUCCESS    同期した (または, 同期済み)
 * @retval -EHOSTUNREACH   サーバの名前を解決できなかった
 * @retval -ETIMEDOUT      時間内に, 応答がなかった
 * @retval negative        問い合わせ, または, 時計の設定の失敗 (負の errno)
 */
int ntp_sync(const char *server, k_timeout_t timeout)
{
    struct sockaddr_storage addr = {0}; /* アドレス */
    socklen_t addr_len = 0;             /* アドレスの長さ [バイト] */
    struct sntp_ctx ctx = {0};          /* SNTP のコンテキスト */
    struct sntp_time ts = {0};          /* SNTP の応答の時刻 */
    struct timespec now = {0};          /* 現在時刻 */
    int err = EXIT_SUCCESS;             /* エラーコード */

    /* 前回の同期から, 間もなければ, 問い合わせない */
    if (synced &&
        ((k_uptime_get() - last_sync_ms) < ((int64_t)CONFIG_THERMO_NTP_RESYNC_S * 1000))) {
        return EXIT_SUCCESS;
    }

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

    /* 秒だけを使う (小数部は, ミリ秒の精度が要らないので, 捨てる) */
    now.tv_sec = (time_t)ts.seconds;
    now.tv_nsec = 0;
    /* 戻り値は, 時計の種類が違うか, ナノ秒が範囲外のときだけ, 負. どちらでもないので, 見ない */
    (void)sys_clock_settime(SYS_CLOCK_REALTIME, &now);

    last_sync_ms = k_uptime_get();
    synced = true;
    LOG_INF("Time synchronized (UNIX time %lld)", (long long)ts.seconds);
    return EXIT_SUCCESS;
}

/**
 * @brief 時刻を同期したことがあるか調べる
 *
 * @return 1 回でも同期していれば true
 */
bool ntp_is_synced(void)
{
    return synced;
}

/**
 * @brief 現在の UNIX 時刻を返す
 *
 * @param[out] sec UNIX 時刻 [s] (同期していなければ, 変更しない)
 * @retval EXIT_SUCCESS 成功
 * @retval -EAGAIN      まだ, 同期していない
 */
int ntp_unix_time(int64_t *sec)
{
    struct timespec now = {0}; /* 現在時刻 */

    if (!synced) {
        return -EAGAIN;
    }
    /* 戻り値は, 時計の種類が違うときだけ, 負. 違わないので, 見ない */
    (void)sys_clock_gettime(SYS_CLOCK_REALTIME, &now);
    *sec = (int64_t)now.tv_sec;
    return EXIT_SUCCESS;
}
