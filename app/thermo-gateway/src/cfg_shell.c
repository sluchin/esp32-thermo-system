/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief 設定を入力するためのシェルのコマンド (`thermo`)
 *
 * 使い方:
 * @code
 *   thermo set <ssid|psk|endpoint|client_id> <値>
 *   cred buf load               (Zephyr のコマンド. PEM を貼り付けて, Ctrl-C で終える)
 *   cred add 1 CA default strt  (Zephyr のコマンド. 証明書の登録. CA, CLIENT, PK の 3 つ)
 *   thermo save-certs
 *   thermo show
 *   thermo apply
 *   thermo reset
 * @endcode
 */

#include <zephyr/shell/shell.h>
#include <errno.h>  /* EINVAL */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "cfg.h"
#include "cloud.h"

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
    int key = cfg_key_from_name(argv[1]); /* 設定項目の番号 */
    int err = EXIT_SUCCESS;               /* エラーコード */

    ARG_UNUSED(argc);

    if (key < 0) {
        shell_error(sh, "Unknown item '%s' (ssid, psk, endpoint, client_id)", argv[1]);
        return -EINVAL;
    }

    err = cfg_set((enum cfg_key)key, argv[2]);
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
    size_t i = 0U; /* ループ用の添字 */

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    for (i = 0U; i < (size_t)CFG_KEY_COUNT; i++) {
        if ((enum cfg_key)i == CFG_KEY_PSK) {
            shell_print(sh, "%s: %s", cfg_key_name(CFG_KEY_PSK),
                        ((cfg_get(CFG_KEY_PSK)[0] == '\0') ? "(empty)" : "********"));
        } else {
            shell_print(sh, "%s: %s", cfg_key_name((enum cfg_key)i), cfg_get((enum cfg_key)i));
        }
    }
    for (i = 0U; i < (size_t)CFG_CRED_COUNT; i++) {
        shell_print(sh, "%s: %s", cfg_cred_name((enum cfg_cred)i),
                    (cfg_has_cred((enum cfg_cred)i) ? "registered" : "missing"));
    }
    shell_print(sh, "ready to connect: %s", (cfg_is_complete() ? "yes" : "no"));

    return EXIT_SUCCESS;
}

/**
 * `thermo save-certs`: 登録した証明書を, フラッシュに保存する
 *
 * @param[in] sh   シェル
 * @param[in] argc 使用しない
 * @param[in] argv 使用しない
 * @return 0 (成功), 負の errno (失敗)
 */
static int cmd_save_certs(const struct shell *sh, size_t argc, char **argv)
{
    int err = cfg_save_credentials(); /* エラーコード */

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (err != 0) {
        shell_error(sh,
                    "Failed to save the certificates (err %d). Register ca, cert and key "
                    "with the 'cred add' command (security tag %d) first.",
                    err, CFG_TLS_SEC_TAG);
        return err;
    }
    shell_print(sh, "certificates saved");

    return EXIT_SUCCESS;
}

/**
 * `thermo apply`: 設定を反映する (接続をやり直す)
 *
 * @param[in] sh   シェル
 * @param[in] argc 使用しない
 * @param[in] argv 使用しない
 * @return 0
 */
static int cmd_apply(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    cloud_reconnect();
    shell_print(sh, "reconnecting");

    return EXIT_SUCCESS;
}

/**
 * `thermo reset`: 設定と証明書を, 全て消す
 *
 * @param[in] sh   シェル
 * @param[in] argc 使用しない
 * @param[in] argv 使用しない
 * @return 0 (成功), 負の errno (失敗)
 */
static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
    int err = cfg_reset(); /* エラーコード */

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (err != 0) {
        shell_error(sh, "Failed to erase the settings (err %d)", err);
        return err;
    }
    shell_print(sh, "settings erased");

    return EXIT_SUCCESS;
}

/** `thermo` コマンドのサブコマンド (set, show, save-certs, apply, reset) */
SHELL_STATIC_SUBCMD_SET_CREATE(
        thermo_cmds,
        SHELL_CMD_ARG(set, NULL, "<ssid|psk|endpoint|client_id> <value> : save a setting", cmd_set,
                      3, 0),
        SHELL_CMD(show, NULL, "show the settings (the password is hidden)", cmd_show),
        /*
         * コマンド名のハイフンは, clang-format が前後に空白を入れて ("save - certs"),
         * 別の名前にしてしまうので整形しない
         */
        /* clang-format off */
        SHELL_CMD(save-certs, NULL, "save the certificates added with 'cred add'", cmd_save_certs),
        /* clang-format on */
        SHELL_CMD(apply, NULL, "reconnect with the current settings", cmd_apply),
        SHELL_CMD(reset, NULL, "erase all the settings and certificates", cmd_reset),
        SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(thermo, &thermo_cmds, "Thermo gateway settings", NULL);
