/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief 時刻を WiFi で取るためのシェルのコマンド (`thermo sync`)
 *
 * 設定のコマンド (`thermo set`, `thermo show`, `thermo reset` など) は, thermo-gateway と
 * 共通のコード (app/common/cfg_shell.c) にある. この `sync` は, shell_extra.h で,
 * そのコマンドに足す.
 *
 * 使い方:
 * @code
 *   thermo sync
 * @endcode
 */

#include <zephyr/shell/shell.h>
#include <errno.h>  /* EINVAL */
#include <stdint.h> /* int64_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "node_time.h"
#include "shell_extra.h"
#include "wifi_time.h"

/**
 * `thermo sync`: WiFi と SNTP で時刻を取って, RTC に設定する (すぐに実行する. 最大 25 秒かかる)
 *
 * @param[in] sh   シェル
 * @param[in] argc 使用しない
 * @param[in] argv 使用しない
 * @return 0 (成功), 負の errno (失敗)
 */
int node_cmd_sync(const struct shell *sh, size_t argc, char **argv)
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
