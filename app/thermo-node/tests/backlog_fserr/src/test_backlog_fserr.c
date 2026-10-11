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
 *  - 追記: 開く, 書き込み (短い, 負), 閉じる
 *  - 読み出し: 開く, 移動, 読み出し (短い, 負)
 *  - 送り終えたことの記録: 開く, 切り詰め, 書き込み (短い)
 */

#include <zephyr/fff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>
#include <errno.h>  /* ENOENT EIO EACCES ENOSPC */
#include <stdlib.h> /* EXIT_SUCCESS */

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
FAKE_VALUE_FUNC(int, fs_truncate, struct fs_file_t *, off_t)
FAKE_VALUE_FUNC(int, fs_unlink, const char *)

/** 記録のファイルの, 最初の記録の数 */
#define INITIAL_RECORDS 2U
/** 記録 1 件のバイト数 */
#define RECORD_SIZE     16U

/**
 * fs_stat() のモック動作 (記録が INITIAL_RECORDS 件のファイルがあることにする)
 *
 * @param[in]  path  使用しない
 * @param[out] entry ファイルの情報
 * @return 0
 */
static int stat_two_records(const char *path, struct fs_dirent *entry)
{
    ARG_UNUSED(path);

    entry->size = INITIAL_RECORDS * RECORD_SIZE;

    return EXIT_SUCCESS;
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
 * 各テストの前に, モックを元に戻して, 記録が 2 件ある状態で初期化する
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);

    RESET_FAKE(fs_mount);
    RESET_FAKE(fs_unmount);
    RESET_FAKE(fs_stat);
    RESET_FAKE(fs_open);
    RESET_FAKE(fs_close);
    RESET_FAKE(fs_read);
    RESET_FAKE(fs_write);
    RESET_FAKE(fs_seek);
    RESET_FAKE(fs_truncate);
    RESET_FAKE(fs_unlink);
    FFF_RESET_HISTORY();

    /* 送り終えた数のファイルは, ない (ENOENT) ので, 送り終えた数は 0 */
    fs_open_fake.return_val = -ENOENT;
    fs_stat_fake.custom_fake = stat_two_records;
    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), INITIAL_RECORDS);
    fs_open_fake.return_val = EXIT_SUCCESS;
}

ZTEST_SUITE(backlog_fserr, NULL, NULL, before, NULL, NULL);

/** 追記: 開けないと, その errno を返して, 数えない */
ZTEST(backlog_fserr, test_append_open_failure)
{
    struct backlog_record r = rec(); /* 記録 */

    fs_open_fake.return_val = -EACCES;

    zassert_equal(backlog_append(&r), -EACCES);
    zassert_equal(backlog_pending(), INITIAL_RECORDS);
}

/** 追記: 書き込みが短いと -EIO, 負の値のときは, その値を返して, 数えない */
ZTEST(backlog_fserr, test_append_write_failure)
{
    struct backlog_record r = rec(); /* 記録 */

    fs_write_fake.return_val = 4;
    zassert_equal(backlog_append(&r), -EIO);

    fs_write_fake.return_val = -ENOSPC;
    zassert_equal(backlog_append(&r), -ENOSPC);

    zassert_equal(backlog_pending(), INITIAL_RECORDS);
}

/** 追記: 閉じるのに失敗すると, その errno を返して, 数えない */
ZTEST(backlog_fserr, test_append_close_failure)
{
    struct backlog_record r = rec(); /* 記録 */

    fs_write_fake.return_val = RECORD_SIZE;
    fs_close_fake.return_val = -EIO;

    zassert_equal(backlog_append(&r), -EIO);
    zassert_equal(backlog_pending(), INITIAL_RECORDS);
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

    fs_seek_fake.return_val = -EIO;

    zassert_equal(backlog_read(&r, 0U), -EIO);
    zassert_equal(fs_read_fake.call_count, 0U);
}

/** 読み出し: 短いと -EIO, 負の値のときは, その値を返す */
ZTEST(backlog_fserr, test_read_short_failure)
{
    struct backlog_record r = {0}; /* 読み出した記録 */

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

/** 送り終えたことの記録: 切り詰めに失敗すると, その errno を返す (書き込まない) */
ZTEST(backlog_fserr, test_commit_truncate_failure)
{
    fs_truncate_fake.return_val = -EIO;

    zassert_equal(backlog_commit(1U), -EIO);
    zassert_equal(fs_write_fake.call_count, 0U);
}

/** 送り終えたことの記録: 書き込みが短いと -EIO */
ZTEST(backlog_fserr, test_commit_write_failure)
{
    fs_write_fake.return_val = 1;

    zassert_equal(backlog_commit(1U), -EIO);
}
