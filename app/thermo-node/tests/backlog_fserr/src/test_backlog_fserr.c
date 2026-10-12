/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief backlog.c の単体テスト (ファイルの操作の失敗)
 *
 * fs_* の関数を FFF のモックに置き換えて, 次の失敗が, 呼び出し元に伝わることを確認する.
 *  - 起動: 開く, 読み出し (短い)
 *  - 追記: 開く, 移動, 書き込み (短い, 負), 閉じる, 輪になったときの上書きされる記録の読み出し
 *  - 読み出し: 開く, 移動, 読み出し (短い, 負)
 *  - 送り終えたことの記録: 開く, 読み出し, 移動, 書き込み (短い, 負)
 *  - 時刻以降の記録の読み出し: 開く, 読み出し (二分探索の途中, 渡す途中)
 */

#include <zephyr/fff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>
#include <errno.h>  /* ENOENT EIO EACCES ENOSPC */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* memset */

#include "backlog.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, fs_mount, struct fs_mount_t *)
FAKE_VALUE_FUNC(int, fs_unmount, struct fs_mount_t *)
FAKE_VALUE_FUNC(int, fs_stat, const char *, struct fs_dirent *)
FAKE_VALUE_FUNC(int, fs_open, struct fs_file_t *, const char *, fs_mode_t)
FAKE_VALUE_FUNC(int, fs_close, struct fs_file_t *)
FAKE_VALUE_FUNC(ssize_t, fs_read, struct fs_file_t *, void *, size_t)
FAKE_VALUE_FUNC(ssize_t, fs_write, struct fs_file_t *, const void *, size_t)
FAKE_VALUE_FUNC(int, fs_seek, struct fs_file_t *, off_t, int)

/** 記録のファイルの, 最初の記録の数 */
#define INITIAL_RECORDS 2U
/** 輪が満杯になる記録の数 (CONFIG_THERMO_BACKLOG_MAX_RECORDS) */
#define FULL_RECORDS    5U
/** 記録 1 件のバイト数 */
#define RECORD_SIZE     16U

/** 記録のファイルの大きさ [バイト] (fs_stat() が返す) */
static size_t file_size;
/** この回数目以降の fs_read() は, -EIO を返す (0 は, 失敗させない) */
static unsigned int read_fails_from;
/** この回数目の fs_seek() は, -EIO を返す (0 は, 失敗させない) */
static unsigned int seek_fails_at;

/**
 * fs_stat() のモック動作 (file_size の大きさのファイルがあることにする)
 *
 * @param[in]  path  使用しない
 * @param[out] entry ファイルの情報
 * @return 0
 */
static int fake_stat(const char *path, struct fs_dirent *entry)
{
    ARG_UNUSED(path);

    entry->size = file_size;

    return EXIT_SUCCESS;
}

/**
 * fs_read() のモック動作 (読むたびに, 時刻が 1 ずつ進む, 未送信の有効な記録を返す)
 *
 * @param[in]  file 使用しない
 * @param[out] dst  読み出した記録
 * @param[in]  size 使用しない
 * @return RECORD_SIZE (read_fails_from 回目以降は -EIO)
 */
static ssize_t fake_read(struct fs_file_t *file, void *dst, size_t size)
{
    uint8_t *buf = (uint8_t *)dst; /* 読み出し先 */

    ARG_UNUSED(file);
    ARG_UNUSED(size);

    if ((read_fails_from > 0U) && (fs_read_fake.call_count >= read_fails_from)) {
        return -EIO;
    }
    (void)memset(buf, 0, RECORD_SIZE);
    sys_put_le32(1000U + fs_read_fake.call_count, &buf[0]);
    sys_put_le16(235U, &buf[4]);
    sys_put_le16(450U, &buf[6]);
    sys_put_le32(crc32_ieee(buf, 8U), &buf[12]);

    return (ssize_t)RECORD_SIZE;
}

/**
 * fs_read() のモック動作 (全て 0 の記録を返す. CRC が合わないので, 壊れた記録になる)
 *
 * @param[in]  file 使用しない
 * @param[out] dst  読み出した記録
 * @param[in]  size 使用しない
 * @return RECORD_SIZE
 */
static ssize_t fake_read_zero(struct fs_file_t *file, void *dst, size_t size)
{
    ARG_UNUSED(file);
    ARG_UNUSED(size);
    (void)memset(dst, 0, RECORD_SIZE);

    return (ssize_t)RECORD_SIZE;
}

/**
 * fs_seek() のモック動作 (seek_fails_at 回目だけ, 失敗する)
 *
 * @param[in] file   使用しない
 * @param[in] offset 使用しない
 * @param[in] whence 使用しない
 * @return 0 (失敗させる回は -EIO)
 */
static int fake_seek(struct fs_file_t *file, off_t offset, int whence)
{
    ARG_UNUSED(file);
    ARG_UNUSED(offset);
    ARG_UNUSED(whence);

    return (((seek_fails_at > 0U) && (fs_seek_fake.call_count == seek_fails_at)) ? -EIO : 0);
}

/**
 * 試験用の記録を作る
 *
 * @return 記録
 */
static struct backlog_record rec(void)
{
    struct backlog_record r = {
        .unix_s = 1790000000LL,
        .temp_x10 = 235,
        .humidity_x10 = 450U,
    }; /* 記録 */

    return r;
}

/**
 * backlog_foreach_since() に渡す関数 (何もしない)
 *
 * @param[in] r   使用しない
 * @param[in] ctx 使用しない
 */
static void ignore(const struct backlog_record *r, void *ctx)
{
    ARG_UNUSED(r);
    ARG_UNUSED(ctx);
}

/**
 * 全てのモックを元に戻して, 記録が records 件あるファイルで, 初期化する
 *
 * @param[in] records ファイルにある記録の数
 */
static void reinit(unsigned int records)
{
    RESET_FAKE(fs_mount);
    RESET_FAKE(fs_unmount);
    RESET_FAKE(fs_stat);
    RESET_FAKE(fs_open);
    RESET_FAKE(fs_close);
    RESET_FAKE(fs_read);
    RESET_FAKE(fs_write);
    RESET_FAKE(fs_seek);
    FFF_RESET_HISTORY();

    file_size = (size_t)records * RECORD_SIZE;
    read_fails_from = 0U;
    seek_fails_at = 0U;
    fs_stat_fake.custom_fake = fake_stat;
    fs_read_fake.custom_fake = fake_read;
    fs_seek_fake.custom_fake = fake_seek;
    fs_write_fake.return_val = (ssize_t)RECORD_SIZE;
    zassert_equal(backlog_init(), EXIT_SUCCESS);
}

/**
 * 各テストの前に, モックを元に戻して, 記録が 2 件 (どちらも未送信) ある状態で初期化する
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    reinit(INITIAL_RECORDS);
    zassert_equal(backlog_pending(), INITIAL_RECORDS);
}

ZTEST_SUITE(backlog_fserr, NULL, NULL, before, NULL, NULL);

/** 起動: ファイルを開けないと, 記録は 0 件 (成功する) */
ZTEST(backlog_fserr, test_init_open_failure)
{
    RESET_FAKE(fs_open);
    fs_open_fake.return_val = -ENOENT;

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 0U);
}

/** 起動: 読み出しが短いと, そこまでの記録だけを数える */
ZTEST(backlog_fserr, test_init_short_read)
{
    read_fails_from = fs_read_fake.call_count + 2U; /* 1 件目は読める. 2 件目から -EIO */

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 1U);
}

/** 起動: 輪が満杯で, 全ての記録が壊れていても, 先頭から上書きを始める (未送信は 0 件) */
ZTEST(backlog_fserr, test_init_full_ring_all_broken)
{
    struct backlog_record r = rec(); /* 記録 */

    file_size = (size_t)FULL_RECORDS * RECORD_SIZE;
    fs_read_fake.custom_fake = fake_read_zero;

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 0U);
    zassert_equal(backlog_append(&r, false), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 1U);
}

/** 追記: 開けないと, その errno を返して, 数えない */
ZTEST(backlog_fserr, test_append_open_failure)
{
    struct backlog_record r = rec(); /* 記録 */

    fs_open_fake.return_val = -EACCES;

    zassert_equal(backlog_append(&r, false), -EACCES);
    zassert_equal(backlog_pending(), INITIAL_RECORDS);
}

/** 追記: 移動できないと, その errno を返して, 書かず, 数えない */
ZTEST(backlog_fserr, test_append_seek_failure)
{
    struct backlog_record r = rec(); /* 記録 */

    seek_fails_at = fs_seek_fake.call_count + 1U;

    zassert_equal(backlog_append(&r, false), -EIO);
    zassert_equal(fs_write_fake.call_count, 0U);
    zassert_equal(backlog_pending(), INITIAL_RECORDS);
}

/** 追記: 書き込みが短いと -EIO, 負の値のときは, その値を返して, 数えない */
ZTEST(backlog_fserr, test_append_write_failure)
{
    struct backlog_record r = rec(); /* 記録 */

    fs_write_fake.return_val = 4;
    zassert_equal(backlog_append(&r, false), -EIO);

    fs_write_fake.return_val = -ENOSPC;
    zassert_equal(backlog_append(&r, false), -ENOSPC);

    zassert_equal(backlog_pending(), INITIAL_RECORDS);
}

/** 追記: 閉じるのに失敗すると, その errno を返して, 数えない */
ZTEST(backlog_fserr, test_append_close_failure)
{
    struct backlog_record r = rec(); /* 記録 */

    fs_close_fake.return_val = -EIO;

    zassert_equal(backlog_append(&r, false), -EIO);
    zassert_equal(backlog_pending(), INITIAL_RECORDS);
}

/** 追記: 輪が満杯のとき, 上書きされる記録を読めなくても (送信済みとして扱って), 上書きする */
ZTEST(backlog_fserr, test_append_full_unreadable_old_record)
{
    struct backlog_record r = rec(); /* 記録 */

    reinit(FULL_RECORDS);
    zassert_equal(backlog_pending(), FULL_RECORDS);
    fs_read_fake.custom_fake = NULL;
    fs_read_fake.return_val = -EIO;

    zassert_equal(backlog_append(&r, true), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), FULL_RECORDS); /* 上書きされた記録を, 未送信として数えない */
}

/** 読み出し: 開けないと, その errno を返す */
ZTEST(backlog_fserr, test_read_open_failure)
{
    struct backlog_record r = {0}; /* 読み出した記録 */

    fs_open_fake.return_val = -EACCES;

    zassert_equal(backlog_read(&r, 0U), -EACCES);
}

/** 読み出し: 移動に失敗すると, その errno を返す (読み出さない) */
ZTEST(backlog_fserr, test_read_seek_failure)
{
    struct backlog_record r = {0}; /* 読み出した記録 */

    seek_fails_at = fs_seek_fake.call_count + 1U;
    fs_read_fake.call_count = 0U;

    zassert_equal(backlog_read(&r, 0U), -EIO);
    zassert_equal(fs_read_fake.call_count, 0U);
}

/** 読み出し: 短いと -EIO, 負の値のときは, その値を返す */
ZTEST(backlog_fserr, test_read_short_failure)
{
    struct backlog_record r = {0}; /* 読み出した記録 */

    fs_read_fake.custom_fake = NULL;
    fs_read_fake.return_val = 4;
    zassert_equal(backlog_read(&r, 0U), -EIO);

    fs_read_fake.return_val = -EACCES;
    zassert_equal(backlog_read(&r, 0U), -EACCES);
}

/** 送り終えたことの記録: 開けないと, その errno を返す */
ZTEST(backlog_fserr, test_commit_open_failure)
{
    fs_open_fake.return_val = -EACCES;

    zassert_equal(backlog_commit(1U), -EACCES);
}

/** 送り終えたことの記録: 記録を読めないと, その errno を返す (書き込まない) */
ZTEST(backlog_fserr, test_commit_read_failure)
{
    fs_read_fake.custom_fake = NULL;
    fs_read_fake.return_val = -EIO;

    zassert_equal(backlog_commit(1U), -EIO);
    zassert_equal(fs_write_fake.call_count, 0U);
}

/** 送り終えたことの記録: フラグの位置へ移動できないと, その errno を返す (書き込まない) */
ZTEST(backlog_fserr, test_commit_seek_failure)
{
    seek_fails_at = fs_seek_fake.call_count + 2U; /* 1 回目は, 記録を読むための移動 */

    zassert_equal(backlog_commit(1U), -EIO);
    zassert_equal(fs_write_fake.call_count, 0U);
    zassert_equal(backlog_pending(), INITIAL_RECORDS);
}

/** 送り終えたことの記録: 書き込みが短いと -EIO, 負の値のときは, その値を返す */
ZTEST(backlog_fserr, test_commit_write_failure)
{
    fs_write_fake.return_val = 0;
    zassert_equal(backlog_commit(1U), -EIO);

    fs_write_fake.return_val = -ENOSPC;
    zassert_equal(backlog_commit(1U), -ENOSPC);
    zassert_equal(backlog_pending(), INITIAL_RECORDS);
}

/** 時刻以降の記録の読み出し: 開けないと, その errno を返す */
ZTEST(backlog_fserr, test_foreach_open_failure)
{
    fs_open_fake.return_val = -EACCES;

    zassert_equal(backlog_foreach_since(0, ignore, NULL), -EACCES);
}

/** 時刻以降の記録の読み出し: 探している途中で読めないと, その errno を返す */
ZTEST(backlog_fserr, test_foreach_search_failure)
{
    fs_read_fake.custom_fake = NULL;
    fs_read_fake.return_val = -EIO;

    zassert_equal(backlog_foreach_since(0, ignore, NULL), -EIO);
}

/** 時刻以降の記録の読み出し: 記録を渡している途中で読めないと, その errno を返す */
ZTEST(backlog_fserr, test_foreach_read_failure)
{
    read_fails_from = fs_read_fake.call_count + 3U; /* 探すのに 2 回 (2 件) 読んだあと */

    zassert_equal(backlog_foreach_since(0, ignore, NULL), -EIO);
}
