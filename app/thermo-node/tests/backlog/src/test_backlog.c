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
 *  - 記録を追記して, 古い順に読み出せること (測った時刻, 温度, 湿度がそのまま戻ること)
 *  - 送信済みの記録は読み出しの対象にならないこと. 送り終えた記録のフラグがファイルに書かれて,
 *    再起動 (backlog_init()) のあとも保たれること
 *  - 記録の数が上限に達したら, 最も古い記録から上書きすること (再起動のあとも,
 * 順序と未送信の数が合うこと)
 *  - 壊れた記録 (CRC の不一致) と, 書きかけの記録 (端の 16 バイトに満たない部分) を扱うこと
 *  - 指定の時刻以降の記録を古い順に読み出せること (送信済みも含む)
 *  - 範囲の外の時刻を拒否すること
 */

#include <zephyr/ztest.h>
#include <zephyr/fs/fs.h>
#include <errno.h>  /* ENODEV EINVAL ENOENT EIO */
#include <stdint.h> /* int64_t UINT32_MAX */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* memset */

#include "backlog.h"

/** 記録のファイル */
#define DATA_PATH   "/RAM:/backlog.bin"
/** 記録 1 件のバイト数 */
#define RECORD_SIZE 16U
/** 試験の時刻の始まり (UNIX 時刻 [s]) */
#define BASE_UNIX   1700000000LL
/** 時刻の間隔 [s] */
#define STEP_S      10LL

/**
 * 試験用の記録を作る
 *
 * @param[in] n 何番目か (時刻と温度に使う)
 * @return 記録
 */
static struct backlog_record rec(unsigned int n)
{
    struct backlog_record r = {
        .unix_s = BASE_UNIX + ((int64_t)n * STEP_S),
        .temp_x10 = (int16_t)(200 + (int)n),
        .humidity_x10 = (uint16_t)(450U + n),
    }; /* 記録 */

    return r;
}

/**
 * 記録を指定の数だけ, 追記する
 *
 * @param[in] first 最初の番号
 * @param[in] count 数
 * @param[in] sent  送信済みとして保存するか
 */
static void append_n(unsigned int first, unsigned int count, bool sent)
{
    unsigned int i = 0U; /* ループ用の添字 */

    for (i = 0U; i < count; i++) {
        struct backlog_record r = rec(first + i); /* 記録 */

        zassert_equal(backlog_append(&r, sent), EXIT_SUCCESS);
    }
}

/**
 * 送っていない記録の, 古いほうから index 番目が番号 n の記録と同じことを確認する
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
 * ファイルの指定の位置に, バイト列を書く (壊れた記録や書きかけの記録を作るために使う)
 *
 * @param[in] offset 位置 [バイト]
 * @param[in] data   書く内容
 * @param[in] size   バイト数
 */
static void poke(off_t offset, const uint8_t *data, size_t size)
{
    struct fs_file_t file = {0}; /* 記録のファイル */

    fs_file_t_init(&file);
    zassert_equal(fs_open(&file, DATA_PATH, FS_O_RDWR), 0);
    zassert_equal(fs_seek(&file, offset, FS_SEEK_SET), 0);
    zassert_equal(fs_write(&file, data, size), (ssize_t)size);
    zassert_equal(fs_close(&file), 0);
}

/**
 * ファイルの指定の位置のバイトを反転する (記録を壊す)
 *
 * @param[in] offset 位置 [バイト]
 */
static void flip(off_t offset)
{
    struct fs_file_t file = {0}; /* 記録のファイル */
    uint8_t byte = 0U;           /* 読み出したバイト */

    fs_file_t_init(&file);
    zassert_equal(fs_open(&file, DATA_PATH, FS_O_RDWR), 0);
    zassert_equal(fs_seek(&file, offset, FS_SEEK_SET), 0);
    zassert_equal(fs_read(&file, &byte, 1U), 1);
    byte = (uint8_t)~byte;
    zassert_equal(fs_seek(&file, offset, FS_SEEK_SET), 0);
    zassert_equal(fs_write(&file, &byte, 1U), 1);
    zassert_equal(fs_close(&file), 0);
}

/** backlog_foreach_since() で集めた記録 */
static struct backlog_record visited[16];
/** 集めた記録の数 */
static unsigned int visited_count;

/**
 * backlog_foreach_since() に渡す関数 (記録を visited に集める)
 *
 * @param[in] r   記録
 * @param[in] ctx 使用しない
 */
static void collect(const struct backlog_record *r, void *ctx)
{
    ARG_UNUSED(ctx);
    visited[visited_count++] = *r;
}

/**
 * 各テストの前に, ファイルを消して, 初期化する (最初は空)
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)fs_unlink(DATA_PATH);
    zassert_equal(backlog_init(), EXIT_SUCCESS);
    (void)memset(visited, 0, sizeof(visited));
    visited_count = 0U;
}

/** 最初は空. 追記すると, 古い順に, そのまま読み出せる (負の温度と, 湿度なしも通る) */
ZTEST(backlog, test_append_and_read)
{
    struct backlog_record neg = {
        .unix_s = BASE_UNIX + 100, .temp_x10 = -35, .humidity_x10 = 0xFFFFU};
    struct backlog_record got = {0}; /* 読み出した記録 */

    zassert_equal(backlog_pending(), 0U);
    append_n(0U, 3U, false);
    zassert_equal(backlog_append(&neg, false), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 4U);

    expect_record(0U, 0U);
    expect_record(1U, 1U);
    expect_record(2U, 2U);
    zassert_equal(backlog_read(&got, 3U), EXIT_SUCCESS);
    zassert_equal(got.temp_x10, -35);
    zassert_equal(got.humidity_x10, 0xFFFFU);
    zassert_equal(got.unix_s, BASE_UNIX + 100);
}

/** 範囲の外の番号は -EINVAL */
ZTEST(backlog, test_read_out_of_range)
{
    struct backlog_record got = {0}; /* 読み出した記録 */

    append_n(0U, 2U, false);

    zassert_equal(backlog_read(&got, 2U), -EINVAL);
    zassert_equal(backlog_read(&got, 100U), -EINVAL);
}

/** 範囲の外の時刻 (負, 32 ビットを超える) は保存しない */
ZTEST(backlog, test_append_rejects_bad_time)
{
    struct backlog_record r = rec(0U); /* 記録 */

    r.unix_s = -1;
    zassert_equal(backlog_append(&r, false), -EINVAL);
    r.unix_s = (int64_t)UINT32_MAX + 1;
    zassert_equal(backlog_append(&r, false), -EINVAL);
    r.unix_s = (int64_t)UINT32_MAX;
    zassert_equal(backlog_append(&r, true), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 0U);
}

/** 送信済みとして保存した記録は未送信に数えず, 読み出しでも飛ばす */
ZTEST(backlog, test_sent_records_are_skipped)
{
    append_n(0U, 1U, false);
    append_n(1U, 1U, true);
    append_n(2U, 1U, false);

    zassert_equal(backlog_pending(), 2U);
    expect_record(0U, 0U);
    expect_record(1U, 2U);
}

/** 送り終えた数 (commit) はファイルのフラグに書かれて, 再起動 (init) のあとも, 続きから読める */
ZTEST(backlog, test_commit_and_restart)
{
    append_n(0U, 4U, false);

    zassert_equal(backlog_commit(0U), EXIT_SUCCESS); /* 何もしない */
    zassert_equal(backlog_commit(2U), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 2U);
    expect_record(0U, 2U);

    zassert_equal(backlog_init(), EXIT_SUCCESS); /* 再起動 */
    zassert_equal(backlog_pending(), 2U);
    expect_record(0U, 2U);
    expect_record(1U, 3U);

    /* 多すぎる数は -EINVAL */
    zassert_equal(backlog_commit(3U), -EINVAL);
}

/** 送り終えたことにするとき, 間にある送信済みの記録は飛ばす */
ZTEST(backlog, test_commit_skips_sent_records)
{
    append_n(0U, 1U, false);
    append_n(1U, 1U, true);
    append_n(2U, 2U, false);

    zassert_equal(backlog_commit(2U), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 1U);
    expect_record(0U, 3U);
}

/** 全て送り終えたら, 未送信は 0 (再起動のあとも). 記録は残り, 次の記録が続く */
ZTEST(backlog, test_commit_all)
{
    append_n(0U, 3U, false);
    zassert_equal(backlog_commit(3U), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 0U);

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 0U);

    append_n(3U, 1U, false);
    expect_record(0U, 3U);
}

/** 上限 (5 件) を超えたら, 最も古い記録から上書きする. 未送信の数は残った分 */
ZTEST(backlog, test_ring_overwrites_oldest)
{
    append_n(0U, 7U, false);

    zassert_equal(backlog_pending(), BACKLOG_MAX_RECORDS);
    expect_record(0U, 2U);
    expect_record(4U, 6U);

    /* 再起動のあとも, 古い順と未送信の数が同じ */
    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), BACKLOG_MAX_RECORDS);
    expect_record(0U, 2U);
    expect_record(4U, 6U);

    /* 再起動のあとの追記も, 最も古い記録から上書きする */
    append_n(7U, 1U, false);
    expect_record(0U, 3U);
    expect_record(4U, 7U);
}

/** 送信済みの古い記録を上書きしても, 未送信の数は減らない */
ZTEST(backlog, test_ring_overwrites_sent_first)
{
    append_n(0U, 2U, true);
    append_n(2U, 3U, false);
    zassert_equal(backlog_pending(), 3U);

    append_n(5U, 1U, false); /* 記録 0 (送信済み) を上書き */
    zassert_equal(backlog_pending(), 4U);
    expect_record(0U, 2U);
}

/** 輪になったあとの送り終えた数も, ファイルに書かれて, 再起動のあとも, 続きから読める */
ZTEST(backlog, test_commit_after_wrap)
{
    append_n(0U, 7U, false);
    zassert_equal(backlog_commit(2U), EXIT_SUCCESS);
    expect_record(0U, 4U);

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 3U);
    expect_record(0U, 4U);
    expect_record(2U, 6U);
}

/** 未送信の記録を上書きしたあとも, 最も古い未送信の記録の位置を保つ */
ZTEST(backlog, test_ring_keeps_first_unsent_after_overwrite)
{
    append_n(0U, 3U, true);
    append_n(3U, 2U, false); /* 満杯: 送信済み 3 件, 未送信 2 件 */
    append_n(5U, 3U, false); /* 送信済みを 3 件上書き */

    zassert_equal(backlog_pending(), 5U);
    expect_record(0U, 3U);
    expect_record(4U, 7U);

    append_n(8U, 1U, false); /* 未送信の記録 3 を上書き */
    zassert_equal(backlog_pending(), 5U);
    expect_record(0U, 4U);
}

/** 起動のとき, 壊れた記録 (CRC の不一致) は無視する (未送信にも数えず, 読み出しでも飛ばす) */
ZTEST(backlog, test_broken_record_is_ignored)
{
    append_n(0U, 3U, false);
    flip((off_t)(RECORD_SIZE + 1U)); /* 記録 1 の温度を壊す */

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 2U);
    expect_record(0U, 0U);
    expect_record(1U, 2U);
}

/** 書きかけの記録 (端の 16 バイトに満たない部分) は切り捨てて, 次の追記が上書きする */
ZTEST(backlog, test_partial_record_is_dropped)
{
    const uint8_t junk[5] = {1, 2, 3, 4, 5}; /* 書きかけの記録 */

    append_n(0U, 2U, false);
    poke((off_t)(2U * RECORD_SIZE), junk, sizeof(junk));

    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 2U);

    append_n(2U, 1U, false);
    zassert_equal(backlog_init(), EXIT_SUCCESS);
    zassert_equal(backlog_pending(), 3U);
    expect_record(2U, 2U);
}

/** 起動のあとに記録が壊れて数が合わなくなったら, commit は -EIO, 読み出しは -ENOENT */
ZTEST(backlog, test_records_broken_after_init)
{
    struct backlog_record got = {0}; /* 読み出した記録 */

    append_n(0U, 3U, false);
    flip((off_t)(RECORD_SIZE + 1U)); /* 記録 1 を壊す */

    zassert_equal(backlog_commit(3U), -EIO);
    zassert_equal(backlog_pending(), 0U);

    append_n(3U, 2U, false);
    flip((off_t)((3U * RECORD_SIZE) + 1U));
    flip((off_t)((4U * RECORD_SIZE) + 1U));
    zassert_equal(backlog_read(&got, 0U), -ENOENT);
}

/** 指定の時刻以降の記録を古い順に集める (送信済みも含む). 輪になったあとも, 同じ */
ZTEST(backlog, test_foreach_since)
{
    append_n(0U, 2U, true);
    append_n(2U, 3U, false);

    zassert_equal(backlog_foreach_since(BASE_UNIX + (2 * STEP_S), collect, NULL), EXIT_SUCCESS);
    zassert_equal(visited_count, 3U);
    zassert_equal(visited[0].unix_s, BASE_UNIX + (2 * STEP_S));
    zassert_equal(visited[2].unix_s, BASE_UNIX + (4 * STEP_S));

    visited_count = 0U;
    zassert_equal(backlog_foreach_since(0, collect, NULL), EXIT_SUCCESS);
    zassert_equal(visited_count, 5U);
    zassert_equal(visited[0].temp_x10, 200);
    zassert_equal(visited[4].temp_x10, 204);

    visited_count = 0U;
    zassert_equal(backlog_foreach_since(BASE_UNIX + (100 * STEP_S), collect, NULL), EXIT_SUCCESS);
    zassert_equal(visited_count, 0U);

    /* 輪になったあと (記録 2 から 6 が残る) */
    append_n(5U, 2U, false);
    visited_count = 0U;
    zassert_equal(backlog_foreach_since(BASE_UNIX + (3 * STEP_S), collect, NULL), EXIT_SUCCESS);
    zassert_equal(visited_count, 4U);
    zassert_equal(visited[0].temp_x10, 203);
    zassert_equal(visited[3].temp_x10, 206);
}

/** 記録がなければ, 何も渡さない. 壊れた記録は渡さない */
ZTEST(backlog, test_foreach_since_empty_and_broken)
{
    zassert_equal(backlog_foreach_since(0, collect, NULL), EXIT_SUCCESS);
    zassert_equal(visited_count, 0U);

    append_n(0U, 4U, true);
    flip((off_t)(RECORD_SIZE + 1U));        /* 記録 1 を壊す */
    flip((off_t)((2U * RECORD_SIZE) + 1U)); /* 記録 2 を壊す (二分探索の途中で当たる) */
    zassert_equal(backlog_foreach_since(0, collect, NULL), EXIT_SUCCESS);
    zassert_equal(visited_count, 2U);
}

ZTEST_SUITE(backlog, NULL, NULL, before, NULL, NULL);
