/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief 時刻の取得に使う WiFi を設定するための, シェルのコマンド (`thermo`)
 *
 * 使い方:
 * @code
 *   thermo set <ssid|psk> <値>
 *   thermo show
 *   thermo sync
 *   thermo reset
 * @endcode
 */

#include <zephyr/shell/shell.h>
#include <errno.h>  /* EINVAL */
#include <stdint.h> /* int64_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "node_time.h"
#include "wifi_cfg.h"
#include "wifi_time.h"

/**
 * `thermo set <項目> <値>`: 設定の値を, フラッシュに保存する
 *
 * @param[in] sh   シェル
 * @param[in] argc 引数の数 (3)
 * @param[in] argv 引数 (argv[1] が項目の名前, argv[2] が値)
 * @return 0 (成功), 負の errno (失敗)
 */
static int cmd_set(const struct shell *sh, size_t argc, char **argv)
{
    int key = wifi_cfg_key_from_name(argv[1]); /* 設定項目の番号 */
    int err = EXIT_SUCCESS;                    /* エラーコード */

    ARG_UNUSED(argc);

    if (key < 0) {
        shell_error(sh, "Unknown item '%s' (ssid, psk)", argv[1]);
        return -EINVAL;
    }

    err = wifi_cfg_set((enum wifi_cfg_key)key, argv[2]);
    if (err != 0) {
        shell_error(sh, "Failed to set '%s' (err %d)", argv[1], err);
        return err;
    }
    shell_print(sh, "%s saved", argv[1]);

    return EXIT_SUCCESS;
}

/**
 * `thermo show`: 設定の状態を表示する (パスワードは表示しない)
 *
 * @param[in] sh   シェル
 * @param[in] argc 使用しない
 * @param[in] argv 使用しない
 * @return 0
 */
static int cmd_show(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "%s: %s", wifi_cfg_key_name(WIFI_CFG_SSID), wifi_cfg_get(WIFI_CFG_SSID));
    shell_print(sh, "%s: %s", wifi_cfg_key_name(WIFI_CFG_PSK),
                ((wifi_cfg_get(WIFI_CFG_PSK)[0] == '\0') ? "(empty)" : "********"));
    shell_print(sh, "ready to get the time: %s", (wifi_cfg_is_complete() ? "yes" : "no"));

    return EXIT_SUCCESS;
}

/**
 * `thermo sync`: WiFi と SNTP で時刻を取って, RTC に設定する (すぐに実行する. 最大 25 秒かかる)
 *
 * @param[in] sh   シェル
 * @param[in] argc 使用しない
 * @param[in] argv 使用しない
 * @return 0 (成功), 負の errno (失敗)
 */
static int cmd_sync(const struct shell *sh, size_t argc, char **argv)
{
    int64_t unix_s = 0;     /* UTC の UNIX 時刻 [s] */
    int err = EXIT_SUCCESS; /* エラーコード */

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    err = wifi_time_fetch(&unix_s);
    if (err != 0) {
        shell_error(sh, "Failed to get the time over WiFi (err %d)", err);
        return err;
    }

    err = node_time_set(unix_s);
    if (err != 0) {
        shell_error(sh, "Failed to set the RTC (err %d)", err);
        return err;
    }
    shell_print(sh, "time set (UNIX time %lld)", (long long)unix_s);

    return EXIT_SUCCESS;
}

/**
 * `thermo reset`: 設定を全て消す
 *
 * @param[in] sh   シェル
 * @param[in] argc 使用しない
 * @param[in] argv 使用しない
 * @return 0 (成功), 負の errno (失敗)
 */
static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
    int err = wifi_cfg_reset(); /* エラーコード */

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (err != 0) {
        shell_error(sh, "Failed to erase the settings (err %d)", err);
        return err;
    }
    shell_print(sh, "settings erased");

    return EXIT_SUCCESS;
}

/** `thermo` コマンドのサブコマンド (set, show, sync, reset) */
SHELL_STATIC_SUBCMD_SET_CREATE(
        thermo_cmds,
        SHELL_CMD_ARG(set, NULL, "<ssid|psk> <value> : save a WiFi setting", cmd_set, 3, 0),
        SHELL_CMD(show, NULL, "show the WiFi settings (the password is hidden)", cmd_show),
        SHELL_CMD(sync, NULL, "get the time over WiFi now and set the RTC", cmd_sync),
        SHELL_CMD(reset, NULL, "erase the WiFi settings", cmd_reset), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(thermo, &thermo_cmds, "Thermo node WiFi settings (for the time)", NULL);
