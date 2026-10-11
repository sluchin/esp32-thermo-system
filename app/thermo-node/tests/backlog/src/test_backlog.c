/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief backlog.c の単体テスト
 *
 * RAM ディスクの FatFS (本物のファイルシステム) で, 次を確認する.
 *  - 記録を追記して, 古い順に読み出せること (測った時刻, 温度, 湿度が, そのまま戻ること)
 *  - 送り終えた数 (commit) を保って, 再起動 (backlog_init()) のあとも, 続きから読めること
 *  - 全て送り終えたら, ファイルを消すこと (次は, 新しいファイル)
 *  - 溜められる数の上限で, 捨てること
 *  - 壊れた記録 (CRC の不一致) と, 書きかけの記録 (端の 16 バイトに満たない部分) を扱うこと
 *  - SD カードがない (マウントできない) とき, 使えないこと
 */

#include <zephyr/ztest.h>
#include <zephyr/fs/fs.h>
#include <errno.h>  /* ENODEV ENOSPC EINVAL EBADMSG */
#include <stdint.h> /* int64_t */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* memset */

#include "backlog.h"

/** 記録のファイル */
#define DATA_PATH   "/RAM:/backlog.bin"
/** 送り終えた数のファイル */
#define POS_PATH    "/RAM:/backlog.pos"
/** 記録 1 件のバイト数 */
#define RECORD_SIZE 16U

/**
 * 試験用の記録を作る
 *
 * @param[in] n 何番目か (時刻と温度に使う)
 * @return 記録
 */
static struct backlog_record rec(unsigned int n)
{
    struct backlog_record r = {
        .unix_s = 1790000000LL + (int64_t)n,
        .temp_x10 = (int16_t)(200 + (int)n),
        .humidity_x10 = (uint16_t)(450U + n),
    }; /* 記録 */

    return r;
}

/**
 * 記録を, 指定の数だけ, 追記する
 *
 * @param[in] first 最初の番号
 * @param[in] count 数
 */
static void append_n(unsigned int first, unsigned int count)
{
    unsigned int i = 0U; /* ループ用の添字 */

    for (i = 0U; i < count; i++) {
        struct backlog_record r = rec(first + i); /* 記録 */

        zassert_equal(backlog_append(&r), EXIT_SUCCESS);
    }
}

/**
 * 古い順の index 番目の記録を読んで, 番号 n の記録と同じことを確認する
 *
 * @param[in] index 古いほうからの順番
 * @param[in] n     期待する記録の番号
 */
static void expect_record(unsigned int index, unsigned int n)
{
    struct backlog_record got = {0};     /* 読み出した記録 */
    struct backlog_record want = rec(n); /* 期待する記録 */

    zassert_equal(backlog_read(&got, index), EXIT_SUCCESS);
    zassert_equal(got.unix_s, want.unix_s);
    zassert_equal(got.temp_x10, want.temp_x10);
    zassert_equal(got.humidity_x10, want.humidity_x10);
}

/**
 * 各テストの前に, ファイルを消して, 初期化する (最初は, 空)
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)fs_unlink(DATA_PATH);
    (void)fs_unlink(POS_PATH);
    zassert_equal(backlog_init(), EXIT_SUCCESS);
}

/** 最初は, 空. 追記すると, 古い順に, そのまま読み出せる (負の温度と, 湿度なしも通る) */
ZTEST(backlog, test_append_and_read)
{
    struct backlog_record neg = {.unix_s = 1790000100LL, .temp_x10 = -35, .humidity_x10 = 0xFFFFU};
    struct backlog_record got = {0}; /* 読み出した記録 */

    zassert_equal(backlog_pending(), 0U);
    append_n(0U, 3U);
    zassert_equal(backlog_append(&neg), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 4U);

    expect_record(0U, 0U);
    expect_record(1U, 1U);
    expect_record(2U, 2U);
    zassert_equal(backlog_read(&got, 3U), EXIT_SUCCESS);
    zassert_equal(got.temp_x10, -35);
    zassert_equal(got.humidity_x10, 0xFFFFU);
    zassert_equal(got.unix_s, 1790000100LL);
}

/** 範囲の外の番号は, -EINVAL */
ZTEST(backlog, test_read_out_of_range)
{
    struct backlog_record got = {0}; /* 読み出した記録 */

    append_n(0U, 2U);

    zassert_equal(backlog_read(&got, 2U), -EINVAL);
    zassert_equal(backlog_read(&got, 100U), -EINVAL);
}

/** 送り終えた数 (commit) を進めると, 次の読み出しは, 続きから. 再起動 (init) のあとも, 続きから */
ZTEST(backlog, test_commit_and_restart)
{
    append_n(0U, 4U);

    zassert_equal(backlog_commit(0U), EXIT_SUCCESS); /* 何もしない */
    zassert_equal(backlog_commit(2U), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 2U);
    expect_record(0U, 2U);

    /* 再起動: ファイルから, 記録の数と, 送り終えた数を読み直す */
    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 2U);
    expect_record(0U, 2U);
    expect_record(1U, 3U);

    /* 続けて追記しても, 順序が保たれる */
    append_n(4U, 1U);
    zassert_equal(backlog_pending(), 3U);
    expect_record(2U, 4U);
}

/** 全て送り終えたら, ファイルを消す (次の追記は, 新しいファイル). 多すぎる commit は, -EINVAL */
ZTEST(backlog, test_commit_all_removes_the_files)
{
    struct fs_dirent entry = {.size = 0U}; /* ファイルの情報 */

    append_n(0U, 3U);
    zassert_equal(backlog_commit(4U), -EINVAL);
    zassert_equal(backlog_pending(), 3U);

    zassert_equal(backlog_commit(3U), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 0U);
    zassert_not_equal(fs_stat(DATA_PATH, &entry), 0);

    append_n(10U, 1U);
    zassert_equal(backlog_pending(), 1U);
    expect_record(0U, 10U);
}

/** 溜められる数に達したら, 追記しない (-ENOSPC). 送り終えれば, また溜められる */
ZTEST(backlog, test_full_drops_new_records)
{
    struct backlog_record r = rec(99U); /* 捨てられる記録 */

    append_n(0U, BACKLOG_MAX_RECORDS);
    zassert_equal(backlog_append(&r), -ENOSPC);
    zassert_equal(backlog_pending(), BACKLOG_MAX_RECORDS);

    zassert_equal(backlog_commit(1U), EXIT_SUCCESS);
    zassert_equal(backlog_append(&r), EXIT_SUCCESS);
}

/** 壊れた記録 (CRC が合わない) は, -EBADMSG */
ZTEST(backlog, test_corrupted_record)
{
    struct fs_file_t file = {0};     /* 記録のファイル */
    struct backlog_record got = {0}; /* 読み出した記録 */
    uint8_t bad = 0x55U;             /* 壊す値 */

    append_n(0U, 2U);
    fs_file_t_init(&file);
    zassert_equal(fs_open(&file, DATA_PATH, FS_O_RDWR), 0);
    zassert_equal(fs_seek(&file, (off_t)(RECORD_SIZE + 2U), FS_SEEK_SET), 0); /* 2 件目の中 */
    zassert_equal(fs_write(&file, &bad, 1U), 1);
    zassert_equal(fs_close(&file), 0);

    expect_record(0U, 0U);
    zassert_equal(backlog_read(&got, 1U), -EBADMSG);
}

/** 書きかけの記録 (端の, 16 バイトに満たない部分) は, 切り捨てる (init で, 数えない) */
ZTEST(backlog, test_partial_record_is_ignored)
{
    struct fs_file_t file = {0};       /* 記録のファイル */
    uint8_t junk[5] = {1, 2, 3, 4, 5}; /* 書きかけの端 */

    append_n(0U, 2U);
    fs_file_t_init(&file);
    zassert_equal(fs_open(&file, DATA_PATH, FS_O_WRITE | FS_O_APPEND), 0);
    zassert_equal(fs_write(&file, junk, sizeof(junk)), (ssize_t)sizeof(junk));
    zassert_equal(fs_close(&file), 0);

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 2U);
}

/** 送り終えた数のファイルが, 記録の数より大きいとき (壊れたとき) は, 全て送り終えたことにする */
ZTEST(backlog, test_position_beyond_total)
{
    struct fs_file_t file = {0};    /* 送り終えた数のファイル */
    uint8_t pos[4] = {99, 0, 0, 0}; /* 99 */

    append_n(0U, 2U);
    fs_file_t_init(&file);
    zassert_equal(fs_open(&file, POS_PATH, FS_O_CREATE | FS_O_WRITE), 0);
    zassert_equal(fs_write(&file, pos, sizeof(pos)), (ssize_t)sizeof(pos));
    zassert_equal(fs_close(&file), 0);

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 0U);
}

/** 送り終えた数のファイルが短いときは, 先頭から (全て, 未送信) */
ZTEST(backlog, test_short_position_file)
{
    struct fs_file_t file = {0}; /* 送り終えた数のファイル */
    uint8_t pos[2] = {1, 0};     /* 短い値 */

    append_n(0U, 2U);
    fs_file_t_init(&file);
    zassert_equal(fs_open(&file, POS_PATH, FS_O_CREATE | FS_O_WRITE), 0);
    zassert_equal(fs_write(&file, pos, sizeof(pos)), (ssize_t)sizeof(pos));
    zassert_equal(fs_close(&file), 0);

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 2U);
}

ZTEST_SUITE(backlog, NULL, NULL, before, NULL, NULL);
