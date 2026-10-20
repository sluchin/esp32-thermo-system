/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BACKLOG_H
#define THERMO_NODE_BACKLOG_H

/**
 * @file
 * @brief SD カードに保存する測定値の記録 (送信済みフラグつき. ストア・アンド・フォワード)
 *
 * 全ての測定値を SD カード (FatFS) の 1 つのファイルに保存する. ファイルは記録の数が
 * CONFIG_THERMO_BACKLOG_MAX_RECORDS の輪で, いっぱいになったら, 最も古い記録から上書きする
 * (古い記録は送り終えたものが多いので, 未送信の記録は最後まで残る). 1 件は 16 バイトの
 * 固定長 (リトルエンディアン): 測った UNIX 時刻 4 バイト, 温度 2 バイト, 湿度 2 バイト,
 * 送信済みフラグ 1 バイト, 気圧 2 バイト, 予約 1 バイト, CRC-32 4 バイト (先頭の 8 バイトと
 * 気圧の CRC. フラグは含めず, 送り終えたら, その 1 バイトだけを書き換える. 気圧なしの記録は,
 * 先頭の 8 バイトだけの CRC で, 気圧を足す前の記録と同じ形). ゲートウェイ (BLE)
 * に送った測定値は送信済みで 保存する. BLE にも WiFi
 * にもつながっていない間の測定値は未送信で保存して, つながったときに, backlog_read()
 * で古い順に読み出して送り, 送り終えた数を backlog_commit() で知らせる. 電源が切れたときの,
 * 書きかけの記録 (16 バイトに満たない端) は捨てる. グラフの履歴を戻すために, 直近の記録を
 * backlog_foreach_since() で読み出せる. 使わないときは CONFIG_THERMO_BACKLOG=n で ビルドする
 * (このファイルの機能はビルドされない).
 */

#include <zephyr/sys/util.h> /* ARG_UNUSED */
#include <errno.h>           /* ENODEV */
#include <stdbool.h>         /* bool */
#include <stdint.h>          /* int64_t int16_t uint16_t */

/** 保存する測定値 1 件 */
struct backlog_record {
    int64_t unix_s;        /**< 測った UNIX 時刻 [s] (UTC. 0 から UINT32_MAX) */
    int16_t temp_x10;      /**< 温度 [℃ の 10 倍] */
    uint16_t humidity_x10; /**< 湿度 [% の 10 倍] (THERMO_HUMIDITY_NONE は測れない) */
    uint16_t pressure_x10; /**< 気圧 [hPa の 10 倍] (THERMO_PRESSURE_NONE は測れない) */
};

/**
 * backlog_foreach_since() が記録 1 件ごとに呼ぶ関数
 *
 * @param[in] rec 記録 (古い順)
 * @param[in] ctx 呼び出し側が渡した値
 */
typedef void (*backlog_visit_t)(const struct backlog_record *rec, void *ctx);

#ifdef CONFIG_THERMO_BACKLOG

/** 保存できる測定値の数 (CONFIG_THERMO_BACKLOG_MAX_RECORDS. 超えたら, 古い記録から上書きする) */
#define BACKLOG_MAX_RECORDS ((unsigned int)CONFIG_THERMO_BACKLOG_MAX_RECORDS)

int backlog_init(void);

int backlog_append(const struct backlog_record *rec, bool sent);

unsigned int backlog_pending(void);

int backlog_read(struct backlog_record *rec, unsigned int index);

int backlog_commit(unsigned int count);

int backlog_foreach_since(int64_t since_unix_s, backlog_visit_t visit, void *ctx);

#else

/*
 * CONFIG_THERMO_BACKLOG=n のとき (SD カードを使わないビルド): 何も保存しない. 呼び出し側
 * (direct.c) は SD カードがないときと同じに扱う.
 */

/**
 * 何もしない (SD カードを使わない)
 *
 * @return -ENODEV
 */
static inline int backlog_init(void)
{
    return -ENODEV;
}

/**
 * 何もしない (SD カードを使わない)
 *
 * @param[in] rec  使用しない
 * @param[in] sent 使用しない
 * @return -ENODEV
 */
static inline int backlog_append(const struct backlog_record *rec, bool sent)
{
    ARG_UNUSED(rec);
    ARG_UNUSED(sent);

    return -ENODEV;
}

/**
 * 何もしない (SD カードを使わない)
 *
 * @return 0
 */
static inline unsigned int backlog_pending(void)
{
    return 0U;
}

/**
 * 何もしない (SD カードを使わない)
 *
 * @param[out] rec   使用しない
 * @param[in]  index 使用しない
 * @return -ENODEV
 */
static inline int backlog_read(struct backlog_record *rec, unsigned int index)
{
    ARG_UNUSED(rec);
    ARG_UNUSED(index);

    return -ENODEV;
}

/**
 * 何もしない (SD カードを使わない)
 *
 * @param[in] count 使用しない
 * @return -ENODEV
 */
static inline int backlog_commit(unsigned int count)
{
    ARG_UNUSED(count);

    return -ENODEV;
}

/**
 * 何もしない (SD カードを使わない)
 *
 * @param[in] since_unix_s 使用しない
 * @param[in] visit        使用しない
 * @param[in] ctx          使用しない
 * @return -ENODEV
 */
static inline int backlog_foreach_since(int64_t since_unix_s, backlog_visit_t visit, void *ctx)
{
    ARG_UNUSED(since_unix_s);
    ARG_UNUSED(visit);
    ARG_UNUSED(ctx);

    return -ENODEV;
}

#endif /* CONFIG_THERMO_BACKLOG */

#endif /* THERMO_NODE_BACKLOG_H */
