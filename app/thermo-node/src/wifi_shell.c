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
 *   thermo sd
 * @endcode
 *
 * `thermo sd` は, SD カードが使えるかを確かめる (使えなければ, マウントをやり直す). 測定値の保存の
 * 準備 (direct_init()) が失敗していたら, それも表示する.
 */

#include <zephyr/shell/shell.h>
#include <errno.h>  /* EINVAL */
#include <stdint.h> /* int64_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "direct.h"
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

#ifdef CONFIG_THERMO_DIRECT_MQTT
/**
 * `thermo sd`: SD カードが使えるかを確かめる (使えなければ, マウントをやり直す)
 *
 * 失敗の理由は, 続けて出る, ログ (Could not mount the SD card (err ...)) に出る.
 *
 * @param[in] sh   シェル
 * @param[in] argc 使用しない
 * @param[in] argv 使用しない
 * @return 0 (成功), 負の errno (失敗)
 */
int node_cmd_sd(const struct shell *sh, size_t argc, char **argv)
{
    unsigned int pending = 0U;   /* 未送信の記録の数 */
    int init_err = EXIT_SUCCESS; /* direct_init() の結果 */
    int err = EXIT_SUCCESS;      /* エラーコード */

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    err = direct_sd_check(&pending);
    if (err != EXIT_SUCCESS) {
        shell_error(sh, "SD card is not available (err %d, see the log above)", err);
        return err;
    }
    shell_print(sh, "SD card is ready (%u not sent)", pending);
    init_err = direct_init_status();
    if (init_err != EXIT_SUCCESS) {
        shell_warn(sh, "Saving is off: direct_init() failed at the start (err %d)", init_err);
    }

    return EXIT_SUCCESS;
}
#endif /* CONFIG_THERMO_DIRECT_MQTT */
