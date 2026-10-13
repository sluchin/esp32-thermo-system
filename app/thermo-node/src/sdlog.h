/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_SDLOG_H
#define THERMO_NODE_SDLOG_H

/**
 * @file
 * @brief ログの SD カードへの保存 (起動の 1 分間の全てのログと, エラーのログ)
 *
 * Zephyr のログのバックエンドとして動く. ログは 1 行ずつ文字列にして, RAM のリングバッファに
 * 溜める (ログを出したスレッドでは SD カードに書かない). 書き込みは sdlog_flush() で行う
 * (メインループが 1 秒ごとに呼ぶ). 保存するファイルは次の 2 つ (SD カードのマウントポイントは
 * CONFIG_THERMO_BACKLOG_MOUNT).
 *  - boot.log: 起動から CONFIG_THERMO_SDLOG_BOOT_SEC 秒の間の, 全てのレベルのログ
 * (起動のたびに作り直す)
 *  - error.log: エラー (LOG_ERR) のログ (起動のあとも, 追記する).
 * CONFIG_THERMO_SDLOG_ERROR_MAX_BYTES に 達したら, error.old に移して (古い error.old は消す),
 * 新しい error.log に書く SD カードが使えるようになる (sdlog_start()) まで, ログは RAM に溜める.
 * 溜め切れなかったログは捨てて, エラーのログに知らせる. 使わないときは CONFIG_THERMO_SDLOG=n
 * でビルドする.
 */

#ifdef CONFIG_THERMO_SDLOG

#include <zephyr/logging/log_backend.h>

void sdlog_start(void);

void sdlog_flush(void);

void sdlog_panic(const struct log_backend *const backend);

#else

/** 何もしない (ログを SD カードに保存しない) */
static inline void sdlog_start(void)
{
}

/** 何もしない (ログを SD カードに保存しない) */
static inline void sdlog_flush(void)
{
}

#endif /* CONFIG_THERMO_SDLOG */

#endif /* THERMO_NODE_SDLOG_H */
