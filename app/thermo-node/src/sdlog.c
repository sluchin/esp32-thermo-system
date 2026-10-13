/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief ログの SD カードへの保存 (起動の 1 分間の全てのログと, エラーのログ)
 *
 * ログのバックエンドの process() はログを出した文脈 (ISR を含む) で呼ばれるので, 文字列にして
 * リングバッファに入れるだけにする. SD カードへの書き込みは sdlog_flush() が行う (メインスレッド
 * から呼ぶ). バッファがいっぱいのときは新しい文字列を捨てる (起動の最初のログを残す).
 * 書き込みに失敗したら, 次の sdlog_start() まで, 書き込みを止める (カードが外れたときに, 失敗の
 * ログが溜まり続けないため).
 */

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <errno.h>     /* EIO */
#include <stdbool.h>   /* bool */
#include <stddef.h>    /* size_t */
#include <stdint.h>    /* uint8_t uint32_t int64_t */
#include <stdlib.h>    /* EXIT_SUCCESS */
#include <sys/types.h> /* ssize_t */

#include "sdlog.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(thermo_sdlog, THERMO_LOG_LEVEL);

/** SD カードのマウントポイント (CONFIG_THERMO_BACKLOG_MOUNT. backlog.c がマウントする) */
#define MOUNT_POINT    CONFIG_THERMO_BACKLOG_MOUNT
/** 起動のログのファイル */
#define BOOT_PATH      MOUNT_POINT "/boot.log"
/** エラーのログのファイル */
#define ERROR_PATH     MOUNT_POINT "/error.log"
/** 上限に達したエラーのログの移動先 */
#define ERROR_OLD_PATH MOUNT_POINT "/error.old"

/** 起動のログを溜める時間 [ms] */
#define BOOT_WINDOW_MS ((int64_t)CONFIG_THERMO_SDLOG_BOOT_SEC * 1000)
/** 文字列にするときの作業用のバッファの大きさ [byte] (これより長い行は分けて渡される) */
#define LINE_BUF_SIZE  32U
/** ファイルに書くときの 1 回の大きさ [byte] */
#define CHUNK_SIZE     128U

/** ログの書式: レベルと時刻 (起動からの時間) を付け, 改行は LF だけにする */
#define SDLOG_FLAGS                                                                                \
    (LOG_OUTPUT_FLAG_LEVEL | LOG_OUTPUT_FLAG_TIMESTAMP | LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP |        \
     LOG_OUTPUT_FLAG_CRLF_LFONLY)

/** 起動のログを溜めるリングバッファ */
RING_BUF_DECLARE(sdlog_boot_ring, CONFIG_THERMO_SDLOG_BOOT_BUF_SIZE);
/** エラーのログを溜めるリングバッファ */
RING_BUF_DECLARE(sdlog_error_ring, CONFIG_THERMO_SDLOG_ERROR_BUF_SIZE);
/** 起動のログで, 溜められずに捨てたバイト数 */
static atomic_t boot_dropped;
/** エラーのログで, 溜められずに捨てたバイト数 */
static atomic_t error_dropped;
/** SD カードが使えるか (sdlog_start() で true. 書き込みの失敗で false) */
static bool ready;
/** 起動のログのファイルを作り直したか (作り直したあとは追記する) */
static bool boot_created;

static int boot_out(uint8_t *data, size_t length, void *ctx);
static int error_out(uint8_t *data, size_t length, void *ctx);
static void store(struct ring_buf *ring, atomic_t *dropped, const uint8_t *data, size_t length);
static void backend_process(const struct log_backend *const backend, union log_msg_generic *msg);
static int write_ring(const char *path, struct ring_buf *ring, fs_mode_t flags);
static int flush_boot(void);
static int flush_error(void);
static void rotate_error(void);
static void report_dropped(const char *name, atomic_t *dropped);

/** 起動のログを文字列にするときの作業用のバッファ */
static uint8_t boot_line[LINE_BUF_SIZE];
/** エラーのログを文字列にするときの作業用のバッファ */
static uint8_t error_line[LINE_BUF_SIZE];
/** 起動のログを文字列にする出力 */
LOG_OUTPUT_DEFINE(boot_output, boot_out, boot_line, LINE_BUF_SIZE);
/** エラーのログを文字列にする出力 */
LOG_OUTPUT_DEFINE(error_output, error_out, error_line, LINE_BUF_SIZE);

/** ログのバックエンドの関数 */
static const struct log_backend_api sdlog_api = {
    .process = backend_process,
    .panic = sdlog_panic,
};

/** ログのバックエンド (起動のときから動く) */
LOG_BACKEND_DEFINE(sdlog_backend, sdlog_api, true);

/**
 * @brief SD カードが使えるようになったことを知らせる (マウントしたあとに呼ぶ)
 *
 * これ以降の sdlog_flush() がファイルに書く. それまでに溜めたログも書く.
 */
void sdlog_start(void)
{
    ready = true;
}

/**
 * @brief 溜めたログを SD カードのファイルに書く (メインスレッドから, 1 秒ごとに呼ぶ)
 *
 * SD カードが使えないとき (sdlog_start() の前, または書き込みに失敗したあと) は何もしない
 * (ログは RAM に溜めたまま). 失敗したら, ログを出して, 次の sdlog_start() まで書き込みを止める.
 * 溜められずに捨てたログがあれば, エラーとして知らせる (次の呼び出しで書く).
 */
void sdlog_flush(void)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    if (!ready) {
        return;
    }

    err = flush_boot();
    if (err == EXIT_SUCCESS) {
        err = flush_error();
    }
    if (err != EXIT_SUCCESS) {
        ready = false;
        LOG_WRN("Could not write the log to the SD card (err %d), stop saving", err);
        return;
    }

    report_dropped("boot", &boot_dropped);
    report_dropped("error", &error_dropped);
}

/**
 * @brief ログのバックエンドの panic の関数 (何もしない)
 *
 * パニックのときはファイルシステムを触らない (SD カードに書くのはメインスレッドだけ).
 *
 * @param[in] backend 使用しない
 */
void sdlog_panic(const struct log_backend *const backend)
{
    ARG_UNUSED(backend);
}

/**
 * ログを 1 件, 文字列にして, リングバッファに入れる
 *
 * 起動から BOOT_WINDOW_MS の間は全てのログを起動のログに入れる. エラーはいつでも,
 * エラーのログに入れる.
 *
 * @param[in] backend 使用しない
 * @param[in] msg     ログ
 */
static void backend_process(const struct log_backend *const backend, union log_msg_generic *msg)
{
    log_format_func_t format = log_format_func_t_get(LOG_OUTPUT_TEXT); /* 文字列にする関数 */

    ARG_UNUSED(backend);

    if (k_uptime_get() < BOOT_WINDOW_MS) {
        format(&boot_output, &msg->log, SDLOG_FLAGS);
    }
    if (log_msg_get_level(&msg->log) == LOG_LEVEL_ERR) {
        format(&error_output, &msg->log, SDLOG_FLAGS);
    }
}

/**
 * 文字列にした起動のログをリングバッファに入れる (log_output の出力の関数)
 *
 * @param[in] data   文字列
 * @param[in] length 文字列の長さ [byte]
 * @param[in] ctx    使用しない
 *
 * @return length
 */
static int boot_out(uint8_t *data, size_t length, void *ctx)
{
    ARG_UNUSED(ctx);
    store(&sdlog_boot_ring, &boot_dropped, data, length);

    return (int)length;
}

/**
 * 文字列にしたエラーのログをリングバッファに入れる (log_output の出力の関数)
 *
 * @param[in] data   文字列
 * @param[in] length 文字列の長さ [byte]
 * @param[in] ctx    使用しない
 *
 * @return length
 */
static int error_out(uint8_t *data, size_t length, void *ctx)
{
    ARG_UNUSED(ctx);
    store(&sdlog_error_ring, &error_dropped, data, length);

    return (int)length;
}

/**
 * 文字列をリングバッファに入れる (入りきらない分は捨てて, 捨てた数を数える)
 *
 * @param[in,out] ring    リングバッファ
 * @param[in,out] dropped 捨てたバイト数
 * @param[in]     data    文字列
 * @param[in]     length  文字列の長さ [byte]
 */
static void store(struct ring_buf *ring, atomic_t *dropped, const uint8_t *data, size_t length)
{
    uint32_t put = ring_buf_put(ring, data, (uint32_t)length); /* 入れたバイト数 */

    if (put < length) {
        (void)atomic_add(dropped, (atomic_val_t)(length - put));
    }
}

/**
 * リングバッファの中身をファイルに書く
 *
 * ファイルを開けなかったときはリングバッファを残す (あとで, 書き直す). 開けたら, 全て取り出して
 * 書く (書けなかった分は捨てる).
 *
 * @param[in]     path  ファイル
 * @param[in,out] ring  リングバッファ
 * @param[in]     flags 開くときの追加のフラグ (FS_O_TRUNC または FS_O_APPEND)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
static int write_ring(const char *path, struct ring_buf *ring, fs_mode_t flags)
{
    struct fs_file_t file = {0};     /* ファイル */
    uint8_t chunk[CHUNK_SIZE] = {0}; /* 書き込む塊 */
    uint32_t size = 0U;              /* 塊の大きさ [byte] */
    ssize_t written = 0;             /* 書いたバイト数 */
    int err = EXIT_SUCCESS;          /* エラーコード */
    int close_err = EXIT_SUCCESS;    /* 閉じたときのエラーコード */

    fs_file_t_init(&file);
    err = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | flags);
    if (err != 0) {
        return err;
    }

    while ((err == EXIT_SUCCESS) && (!ring_buf_is_empty(ring))) {
        size = ring_buf_get(ring, chunk, sizeof(chunk));
        written = fs_write(&file, chunk, size);
        if (written != (ssize_t)size) {
            err = -EIO;
        }
    }
    close_err = fs_close(&file);

    return ((err != EXIT_SUCCESS) ? err : close_err);
}

/**
 * 溜めた起動のログを boot.log に書く (最初は作り直して, 2 回目からは追記する)
 *
 * @retval EXIT_SUCCESS 成功 (書くログがないときも)
 * @retval negative     失敗 (負の errno)
 */
static int flush_boot(void)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    if (ring_buf_is_empty(&sdlog_boot_ring)) {
        return EXIT_SUCCESS;
    }
    err = write_ring(BOOT_PATH, &sdlog_boot_ring, (boot_created ? FS_O_APPEND : FS_O_TRUNC));
    if (err == EXIT_SUCCESS) {
        boot_created = true;
    }

    return err;
}

/**
 * 溜めたエラーのログを error.log に追記する (上限に達していたら, 先に error.old に移す)
 *
 * @retval EXIT_SUCCESS 成功 (書くログがないときも)
 * @retval negative     失敗 (負の errno)
 */
static int flush_error(void)
{
    if (ring_buf_is_empty(&sdlog_error_ring)) {
        return EXIT_SUCCESS;
    }
    rotate_error();

    return write_ring(ERROR_PATH, &sdlog_error_ring, FS_O_APPEND);
}

/**
 * error.log が上限に達していたら, error.old に移す (古い error.old は消す. 失敗しても続ける)
 */
static void rotate_error(void)
{
    struct fs_dirent entry = {.size = 0U}; /* ファイルの情報 */
    int err = EXIT_SUCCESS;                /* エラーコード */

    err = fs_stat(ERROR_PATH, &entry);
    if ((err == 0) && (entry.size >= (size_t)CONFIG_THERMO_SDLOG_ERROR_MAX_BYTES)) {
        /* 移せなくても, error.log に追記を続ける (次の書き込みで, もう一度移す) */
        (void)fs_unlink(ERROR_OLD_PATH);
        (void)fs_rename(ERROR_PATH, ERROR_OLD_PATH);
    }
}

/**
 * 溜められずに捨てたログがあれば, エラーのログに出す (数え直す)
 *
 * @param[in]     name    リングバッファの名前
 * @param[in,out] dropped 捨てたバイト数
 */
static void report_dropped(const char *name, atomic_t *dropped)
{
    atomic_val_t lost = atomic_set(dropped, 0); /* 捨てたバイト数 */

    if (lost > 0) {
        LOG_ERR("The %s log buffer was full, %ld bytes were not saved", name, (long)lost);
    }
}
