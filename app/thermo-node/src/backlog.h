/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_BACKLOG_H
#define THERMO_NODE_BACKLOG_H

/**
 * @file
 * @brief SD カードに溜める, 送れなかった測定値 (ストア・アンド・フォワード)
 *
 * BLE でゲートウェイにつながらず, WiFi の MQTT も使えない間の測定値を, SD カード (FatFS) の
 * ファイルに, 追記だけで溜める. 送れるようになったら, 古い順に読み出して, 送り終えた数を
 * backlog_commit() で知らせる (全て送り終えたら, ファイルを消す). 1 件は, 16 バイトの固定長
 * (測った UNIX 時刻, 温度, 湿度, CRC). 電源が切れたときの, 書きかけの記録は, 読み出すときに
 * 捨てる. 使わないときは, CONFIG_THERMO_BACKLOG=n でビルドする (このファイルの機能は, ビルド
 * されない).
 */

#include <zephyr/sys/util.h> /* ARG_UNUSED */
#include <errno.h>           /* ENODEV */
#include <stdint.h>          /* int64_t int16_t uint16_t */

/** 溜める測定値 1 件 */
struct backlog_record {
    int64_t unix_s;        /**< 測った UNIX 時刻 [s] (UTC) */
    int16_t temp_x10;      /**< 温度 [℃ の 10 倍] */
    uint16_t humidity_x10; /**< 湿度 [% の 10 倍] (THERMO_HUMIDITY_NONE は, 測れない) */
};

#ifdef CONFIG_THERMO_BACKLOG

/** 溜められる測定値の数 (CONFIG_THERMO_BACKLOG_MAX_RECORDS) */
#define BACKLOG_MAX_RECORDS ((unsigned int)CONFIG_THERMO_BACKLOG_MAX_RECORDS)

int backlog_init(void);

int backlog_append(const struct backlog_record *rec);

unsigned int backlog_pending(void);

int backlog_read(struct backlog_record *rec, unsigned int index);

int backlog_commit(unsigned int count);

#else

/*
 * CONFIG_THERMO_BACKLOG=n のとき (SD カードを使わないビルド): 何も溜めない. 呼び出し側 (direct.c)
 * は, SD カードがないときと同じに扱う.
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
 * @param[in] rec 使用しない
 * @return -ENODEV
 */
static inline int backlog_append(const struct backlog_record *rec)
{
    ARG_UNUSED(rec);

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

#endif /* CONFIG_THERMO_BACKLOG */

#endif /* THERMO_NODE_BACKLOG_H */
