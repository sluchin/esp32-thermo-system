/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_SHELL_EXTRA_H
#define THERMO_NODE_SHELL_EXTRA_H

/**
 * @file
 * @brief `thermo` コマンドに足す, thermo-node 固有のサブコマンド
 *
 * app/common/cfg_shell.c が, このファイルがあれば読み込んで, THERMO_SHELL_EXTRA を, サブコマンドの
 * 並びの最後に足す. thermo-node は, `thermo sync` (WiFi で時刻を取って RTC に設定する) を足す.
 */

#include <zephyr/shell/shell.h>
#include <stddef.h> /* size_t */

int node_cmd_sync(const struct shell *sh, size_t argc, char **argv);

/** サブコマンドに足す (末尾のコンマを含む) */
#define THERMO_SHELL_EXTRA                                                                         \
    SHELL_CMD(sync, NULL, "get the time over WiFi now and set the RTC", node_cmd_sync),

#endif /* THERMO_NODE_SHELL_EXTRA_H */
