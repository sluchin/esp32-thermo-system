/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief sdlog.c の単体テスト
 *
 * RAM ディスクの FatFS (本物のファイルシステム) と, 本物のログ (LOG_INF と LOG_ERR) で,
 * 次を確認する. 状態 (SD カードが使えるか, 起動の 1 分の窓) をテストの間で引き継ぐので, テストは
 * 名前の順 (test_1 から test_7) に実行される前提で並べてある.
 *  - sdlog_start() の前は書かずに溜めておくこと. 始めたら, 溜めたログを書くこと
 *  - boot.log は全てのレベルのログを作り直してから追記すること. error.log は
 * エラーだけを追記すること
 *  - 溜められなかったログをエラーとして知らせること
 *  - 書き込みに失敗したら, 次の sdlog_start() まで止めること (カードが外れたとき, 容量がないとき)
 *  - 起動の 1 分がたつと, boot.log に書かないこと. エラーはそのあとも error.log に書くこと
 *  - error.log が上限に達したら, error.old に移すこと
 */

#include <zephyr/ztest.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <ff.h>        /* FATFS */
#include <stdint.h>    /* uint8_t */
#include <stdlib.h>    /* EXIT_SUCCESS */
#include <string.h>    /* strstr memset */
#include <sys/types.h> /* ssize_t */

#include "sdlog.h"

LOG_MODULE_REGISTER(test_sdlog, LOG_LEVEL_DBG);

/** 起動のログのファイル */
#define BOOT_PATH      "/RAM:/boot.log"
/** エラーのログのファイル */
#define ERROR_PATH     "/RAM:/error.log"
/** 上限に達したエラーのログの移動先 */
#define ERROR_OLD_PATH "/RAM:/error.old"
/** 容量を使い切るためのファイル */
#define FILL_PATH      "/RAM:/fill.bin"
/** ファイルを読むバッファの大きさ [byte] */
#define READ_SIZE      4096U
/** 起動の窓 [s] (CONFIG_THERMO_SDLOG_BOOT_SEC) */
#define BOOT_SEC       60
/** エラーのログの上限 [byte] (CONFIG_THERMO_SDLOG_ERROR_MAX_BYTES) */
#define ERROR_MAX      1024U
/** 1 ブロックの大きさ [byte] (RAM ディスクのセクタ) */
#define BLOCK_SIZE     512U

/** FatFS のファイルシステム */
static FATFS fat_fs;
/** マウントの設定 (SD カードの代わりの RAM ディスク) */
static struct fs_mount_t mnt = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = "/RAM:",
};
/** ファイルを読んだ内容 */
static char content[READ_SIZE];

/**
 * ファイルの内容を content に読む (ファイルがなければ, 空にする)
 *
 * @param[in] path ファイル
 */
static void load(const char *path)
{
    struct fs_file_t file = {0}; /* ファイル */
    ssize_t got = 0;             /* 読んだバイト数 */

    (void)memset(content, 0, sizeof(content));
    fs_file_t_init(&file);
    if (fs_open(&file, path, FS_O_READ) != 0) {
        return;
    }
    got = fs_read(&file, content, sizeof(content) - 1U);
    zassert_true(got >= 0);
    zassert_equal(fs_close(&file), 0);
}

/**
 * ファイルの大きさを返す (ファイルがなければ 0)
 *
 * @param[in] path ファイル
 * @return 大きさ [byte]
 */
static size_t file_size(const char *path)
{
    struct fs_dirent entry = {.size = 0U}; /* ファイルの情報 */

    if (fs_stat(path, &entry) != 0) {
        return 0U;
    }

    return entry.size;
}

/**
 * ファイルに text が含まれていることを確認する
 *
 * @param[in] path ファイル
 * @param[in] text 含まれるはずの文字列
 */
static void expect_has(const char *path, const char *text)
{
    load(path);
    zassert_not_null(strstr(content, text), "%s has no \"%s\":\n%s", path, text, content);
}

/**
 * ファイルに text が含まれていないことを確認する
 *
 * @param[in] path ファイル
 * @param[in] text 含まれないはずの文字列
 */
static void expect_lacks(const char *path, const char *text)
{
    load(path);
    zassert_is_null(strstr(content, text), "%s has \"%s\":\n%s", path, text, content);
}

/**
 * スイートの前に, RAM ディスクをマウントする
 *
 * @return NULL
 */
static void *setup(void)
{
    zassert_equal(fs_mount(&mnt), 0);

    return NULL;
}

/**
 * sdlog_start() の前は書かない. 前のファイルも, 触らない
 */
ZTEST(sdlog, test_1_not_started)
{
    struct fs_file_t file = {0};                    /* 前の起動のログに見立てたファイル */
    static const char stale[] = "stale boot log\n"; /* 前の内容 */

    fs_file_t_init(&file);
    zassert_equal(fs_open(&file, BOOT_PATH, FS_O_CREATE | FS_O_WRITE), 0);
    zassert_equal(fs_write(&file, stale, sizeof(stale) - 1U), (ssize_t)(sizeof(stale) - 1U));
    zassert_equal(fs_close(&file), 0);

    LOG_INF("info before start");
    LOG_ERR("error before start");
    sdlog_flush();
    sdlog_panic(NULL);

    expect_has(BOOT_PATH, "stale boot log");
    expect_lacks(BOOT_PATH, "info before start");
    zassert_equal(file_size(ERROR_PATH), 0U);
}

/**
 * 始めたら, 溜めたログを書く. boot.log は作り直し, 全てのレベルのログが入る. error.log には
 * エラーだけが入る
 */
ZTEST(sdlog, test_2_start_writes_buffered)
{
    sdlog_start();
    sdlog_flush();

    expect_lacks(BOOT_PATH, "stale boot log");
    expect_has(BOOT_PATH, "<inf> test_sdlog: info before start");
    expect_has(BOOT_PATH, "<err> test_sdlog: error before start");
    expect_has(ERROR_PATH, "<err> test_sdlog: error before start");
    expect_lacks(ERROR_PATH, "info before start");

    /* 2 回目からは追記する */
    LOG_INF("second info");
    LOG_ERR("second error");
    sdlog_flush();
    expect_has(BOOT_PATH, "info before start");
    expect_has(BOOT_PATH, "second info");
    expect_has(ERROR_PATH, "error before start");
    expect_has(ERROR_PATH, "second error");
    expect_lacks(ERROR_PATH, "second info");
}

/**
 * 溜められなかったログは捨てて, エラーとして知らせる
 */
ZTEST(sdlog, test_3_overflow_reported)
{
    int i = 0; /* ループ用の添字 */

    for (i = 0; i < 20; i++) {
        LOG_INF("filler %02d abcdefghijklmnopqrstuvwxyz", i);
    }
    sdlog_flush(); /* 書いてから, 捨てた分を知らせる */
    sdlog_flush(); /* 知らせたログを書く */

    expect_has(BOOT_PATH, "filler 00 ");
    expect_lacks(BOOT_PATH, "filler 19 ");
    expect_has(BOOT_PATH, "The boot log buffer was full");
    expect_has(ERROR_PATH, "The boot log buffer was full");
}

/**
 * カードが外れて (アンマウント) 書けないときは止める. マウントし直して sdlog_start() すると,
 * 溜めたログを書く
 */
ZTEST(sdlog, test_4_unmounted_stops_until_restart)
{
    zassert_equal(fs_unmount(&mnt), 0);
    LOG_INF("info while unmounted");
    sdlog_flush(); /* 開けない: 止める */
    zassert_equal(fs_mount(&mnt), 0);
    sdlog_flush(); /* 止まったまま */
    expect_lacks(BOOT_PATH, "info while unmounted");

    sdlog_start();
    sdlog_flush();
    expect_has(BOOT_PATH, "info while unmounted");
    expect_has(BOOT_PATH, "Could not write the log to the SD card");
}

/**
 * 起動から 1 分がたつと, boot.log には書かない. エラーは error.log に書く
 */
ZTEST(sdlog, test_5_after_boot_window)
{
    (void)k_sleep(K_SECONDS(BOOT_SEC + 1));
    LOG_INF("late info");
    LOG_ERR("late error");
    sdlog_flush();

    expect_lacks(BOOT_PATH, "late info");
    expect_lacks(BOOT_PATH, "late error");
    expect_has(ERROR_PATH, "late error");
    expect_lacks(ERROR_PATH, "late info");
}

/**
 * error.log が上限に達したら, error.old に移して, 新しい error.log に書く
 */
ZTEST(sdlog, test_6_error_log_rotates)
{
    int i = 0; /* ループ用の添字 */

    zassert_equal(file_size(ERROR_OLD_PATH), 0U);
    for (i = 0; i < 60; i++) {
        LOG_ERR("rotate %02d abcdefghijklmnopqrstuvwxyz", i);
        sdlog_flush();
    }

    zassert_true(file_size(ERROR_OLD_PATH) >= ERROR_MAX);
    zassert_true(file_size(ERROR_PATH) < (ERROR_MAX + BLOCK_SIZE));
    expect_has(ERROR_PATH, "rotate 59 ");
    expect_lacks(ERROR_PATH, "late error");
}

/**
 * 容量がなくて書けないときも, 止める. 空けて sdlog_start() すると, 書く
 */
ZTEST(sdlog, test_7_disk_full_stops_until_restart)
{
    struct fs_file_t file = {0};      /* 容量を使い切るファイル */
    uint8_t block[BLOCK_SIZE] = {0U}; /* 書く内容 */
    size_t before = 0U;               /* 止まったあとの error.log の大きさ [byte] */
    int i = 0;                        /* ループ用の添字 */

    fs_file_t_init(&file);
    zassert_equal(fs_open(&file, FILL_PATH, FS_O_CREATE | FS_O_WRITE), 0);
    while (fs_write(&file, block, sizeof(block)) == (ssize_t)sizeof(block)) {
    }
    zassert_equal(fs_close(&file), 0);

    for (i = 0; i < 40; i++) {
        LOG_ERR("disk full %02d abcdefghijklmnopqrstuvwxyz", i);
        sdlog_flush();
    }
    zassert_equal(fs_unlink(FILL_PATH), 0);

    /* 空けても, 止まったまま */
    before = file_size(ERROR_PATH);
    LOG_ERR("after the disk was freed");
    sdlog_flush();
    zassert_equal(file_size(ERROR_PATH), before);

    /* 始め直すと, 溜めたログを書く. 溜め切れなかった分の知らせも, 次の呼び出しで書く */
    sdlog_start();
    sdlog_flush();
    sdlog_flush();
    expect_has(ERROR_PATH, "disk full");
    expect_has(ERROR_PATH, "The error log buffer was full");
}

ZTEST_SUITE(sdlog, NULL, setup, NULL, NULL, NULL);
