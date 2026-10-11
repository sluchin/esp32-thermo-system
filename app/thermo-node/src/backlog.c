/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief SD カードに溜める, 送れなかった測定値 (ストア・アンド・フォワード)
 *
 * 2 つのファイルを使う.
 *  - backlog.bin: 記録の追記だけのファイル (1 件 16 バイト. リトルエンディアン.
 *    測った UNIX 時刻 8 バイト, 温度 2 バイト, 湿度 2 バイト, 先頭の 12 バイトの CRC-32 4 バイト)
 *  - backlog.pos: 送り終えた記録の数 (4 バイト. リトルエンディアン)
 */

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <errno.h>   /* ENODEV ENOSPC EBADMSG EINVAL EIO */
#include <ff.h>      /* FATFS */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* uint8_t uint32_t int64_t */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* memset */

#include "backlog.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(thermo_backlog, THERMO_LOG_LEVEL);

/** SD カードのマウントポイント (CONFIG_THERMO_BACKLOG_MOUNT) */
#define MOUNT_POINT CONFIG_THERMO_BACKLOG_MOUNT
/** 記録のファイル */
#define DATA_PATH   MOUNT_POINT "/backlog.bin"
/** 送り終えた数のファイル */
#define POS_PATH    MOUNT_POINT "/backlog.pos"

/** 記録 1 件のバイト数 */
#define RECORD_SIZE 16U
/** 記録の, CRC を求める範囲のバイト数 (CRC の前まで) */
#define CRC_RANGE   12U
/** 送り終えた数のファイルのバイト数 */
#define POS_SIZE    4U

/** 記録の中の位置: 測った UNIX 時刻 */
#define OFF_UNIX 0U
/** 記録の中の位置: 温度 */
#define OFF_TEMP 8U
/** 記録の中の位置: 湿度 */
#define OFF_HUMI 10U
/** 記録の中の位置: CRC */
#define OFF_CRC  12U

/** FatFS のファイルシステム */
static FATFS fat_fs;
/** マウントの設定 */
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = MOUNT_POINT,
};
/** マウントしたか */
static bool mounted;
/** ファイルにある記録の数 */
static unsigned int total;
/** そのうち, 送り終えた数 */
static unsigned int sent;

static int open_file(struct fs_file_t *file, const char *path, fs_mode_t flags);
static void encode(uint8_t *out, const struct backlog_record *rec);
static int write_pos(unsigned int value);
static int read_pos(unsigned int *value);

/**
 * @brief SD カードをマウントして, 溜めた記録の数と, 送り終えた数を読み込む
 *
 * 書きかけの記録 (16 バイトに満たない端) は, 切り捨てる. 送り終えた数が, 記録の数より大きいときは,
 * 記録の数にする (重複して送らない).
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      SD カードがない (または, マウントできない)
 * @retval negative     ファイルの失敗 (負の errno)
 */
int backlog_init(void)
{
    struct fs_dirent entry = {.size = 0U}; /* ファイルの情報 */
    unsigned int pos = 0U;                 /* ファイルの送り終えた数 */
    int err = EXIT_SUCCESS;                /* エラーコード */

    /* やり直し (再初期化) のときは, 前のマウントを外してから, マウントし直す */
    if (mounted) {
        (void)fs_unmount(&mount);
    }
    mounted = false;
    total = 0U;
    sent = 0U;

    err = fs_mount(&mount);
    if (err != 0) {
        LOG_ERR("Could not mount the SD card (err %d)", err);
        return -ENODEV;
    }
    mounted = true;

    if (fs_stat(DATA_PATH, &entry) == 0) {
        total = (unsigned int)(entry.size / RECORD_SIZE);
    }
    if ((total > 0U) && (read_pos(&pos) == 0)) {
        sent = MIN(pos, total);
    }
    LOG_INF("SD card ready: %u records, %u sent", total, sent);

    return EXIT_SUCCESS;
}

/**
 * @brief 測定値を 1 件, 末尾に追加する
 *
 * @param[in] rec 測定値
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      SD カードを使えない
 * @retval -ENOSPC      溜められる数 (BACKLOG_MAX_RECORDS) に達した (捨てる)
 * @retval negative     ファイルの失敗 (負の errno)
 */
int backlog_append(const struct backlog_record *rec)
{
    struct fs_file_t file = {0}; /* 記録のファイル */
    uint8_t buf[RECORD_SIZE];    /* 書き込む記録 */
    ssize_t written = 0;         /* 書き込んだバイト数 */
    int err = EXIT_SUCCESS;      /* エラーコード */

    if (!mounted) {
        return -ENODEV;
    }
    if ((total - sent) >= BACKLOG_MAX_RECORDS) {
        return -ENOSPC;
    }

    encode(buf, rec);
    err = open_file(&file, DATA_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
    if (err != 0) {
        return err;
    }
    written = fs_write(&file, buf, sizeof(buf));
    err = fs_close(&file);
    if (written != (ssize_t)sizeof(buf)) {
        LOG_ERR("Could not write the record (%d)", (int)written);
        return ((written < 0) ? (int)written : -EIO);
    }
    if (err != 0) {
        LOG_ERR("Could not close the file (err %d)", err);
        return err;
    }
    total++;

    return EXIT_SUCCESS;
}

/**
 * @brief まだ送っていない記録の数
 *
 * @return 送っていない記録の数 (SD カードを使えないときは, 0)
 */
unsigned int backlog_pending(void)
{
    return total - sent;
}

/**
 * @brief 送っていない記録を, 古いほうから数えて, index 番目 (0 が一番古い) を読み出す
 *
 * @param[out] rec   記録
 * @param[in]  index 古いほうからの順番 (backlog_pending() より小さいこと)
 * @retval EXIT_SUCCESS 成功
 * @retval -EINVAL      index が範囲の外
 * @retval -EBADMSG     記録が壊れている (CRC が合わない)
 * @retval negative     ファイルの失敗 (負の errno)
 */
int backlog_read(struct backlog_record *rec, unsigned int index)
{
    struct fs_file_t file = {0}; /* 記録のファイル */
    uint8_t buf[RECORD_SIZE];    /* 読み出した記録 */
    ssize_t got = 0;             /* 読み出したバイト数 */
    int err = EXIT_SUCCESS;      /* エラーコード */

    if (index >= backlog_pending()) {
        return -EINVAL;
    }

    err = open_file(&file, DATA_PATH, FS_O_READ);
    if (err != 0) {
        return err;
    }
    err = fs_seek(&file, (off_t)((sent + index) * RECORD_SIZE), FS_SEEK_SET);
    if (err == 0) {
        got = fs_read(&file, buf, sizeof(buf));
    }
    (void)fs_close(&file);
    if (err != 0) {
        return err;
    }
    if (got != (ssize_t)sizeof(buf)) {
        return ((got < 0) ? (int)got : -EIO);
    }

    if (sys_get_le32(&buf[OFF_CRC]) != crc32_ieee(buf, CRC_RANGE)) {
        return -EBADMSG;
    }
    rec->unix_s = (int64_t)sys_get_le64(&buf[OFF_UNIX]);
    rec->temp_x10 = (int16_t)sys_get_le16(&buf[OFF_TEMP]);
    rec->humidity_x10 = sys_get_le16(&buf[OFF_HUMI]);

    return EXIT_SUCCESS;
}

/**
 * @brief 古いほうから count 件を, 送り終えたことにする
 *
 * 全て送り終えたら, 2 つのファイルを消す (次の記録は, 新しいファイルに追記する).
 *
 * @param[in] count 送り終えた数 (backlog_pending() 以下)
 * @retval EXIT_SUCCESS 成功
 * @retval -EINVAL      count が多すぎる
 * @retval negative     ファイルの失敗 (負の errno)
 */
int backlog_commit(unsigned int count)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    if (count > backlog_pending()) {
        return -EINVAL;
    }
    if (count == 0U) {
        return EXIT_SUCCESS;
    }

    sent += count;
    if (sent == total) {
        (void)fs_unlink(DATA_PATH);
        (void)fs_unlink(POS_PATH);
        total = 0U;
        sent = 0U;
        return EXIT_SUCCESS;
    }

    err = write_pos(sent);
    if (err != 0) {
        LOG_ERR("Could not save the position (err %d)", err);
    }

    return err;
}

/**
 * ファイルを開く
 *
 * @param[out] file  ファイル
 * @param[in]  path  パス
 * @param[in]  flags 開き方 (FS_O_*)
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
static int open_file(struct fs_file_t *file, const char *path, fs_mode_t flags)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    fs_file_t_init(file);
    err = fs_open(file, path, flags);
    if (err != 0) {
        LOG_ERR("Could not open %s (err %d)", path, err);
    }

    return err;
}

/**
 * 記録を, 16 バイトに符号化する (末尾に CRC を付ける)
 *
 * @param[out] out 書き込み先 (RECORD_SIZE バイト)
 * @param[in]  rec 記録
 */
static void encode(uint8_t *out, const struct backlog_record *rec)
{
    sys_put_le64((uint64_t)rec->unix_s, &out[OFF_UNIX]);
    sys_put_le16((uint16_t)rec->temp_x10, &out[OFF_TEMP]);
    sys_put_le16(rec->humidity_x10, &out[OFF_HUMI]);
    sys_put_le32(crc32_ieee(out, CRC_RANGE), &out[OFF_CRC]);
}

/**
 * 送り終えた数を, ファイルに書く (上書き)
 *
 * @param[in] value 送り終えた数
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
static int write_pos(unsigned int value)
{
    struct fs_file_t file = {0}; /* ファイル */
    uint8_t buf[POS_SIZE];       /* 書き込む値 */
    ssize_t written = 0;         /* 書き込んだバイト数 */
    int err = EXIT_SUCCESS;      /* エラーコード */

    err = open_file(&file, POS_PATH, FS_O_CREATE | FS_O_WRITE);
    if (err != 0) {
        return err;
    }
    sys_put_le32(value, buf);
    err = fs_truncate(&file, 0);
    if (err == 0) {
        written = fs_write(&file, buf, sizeof(buf));
    }
    (void)fs_close(&file);
    if (err != 0) {
        return err;
    }

    return ((written == (ssize_t)sizeof(buf)) ? EXIT_SUCCESS : -EIO);
}

/**
 * 送り終えた数を, ファイルから読む
 *
 * @param[out] value 送り終えた数
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (ファイルがない, 短いなど)
 */
static int read_pos(unsigned int *value)
{
    struct fs_file_t file = {0}; /* ファイル */
    uint8_t buf[POS_SIZE];       /* 読み出した値 */
    ssize_t got = 0;             /* 読み出したバイト数 */
    int err = EXIT_SUCCESS;      /* エラーコード */

    fs_file_t_init(&file);
    err = fs_open(&file, POS_PATH, FS_O_READ);
    if (err != 0) {
        return err;
    }
    got = fs_read(&file, buf, sizeof(buf));
    (void)fs_close(&file);
    if (got != (ssize_t)sizeof(buf)) {
        return -EIO;
    }
    *value = sys_get_le32(buf);

    return EXIT_SUCCESS;
}
