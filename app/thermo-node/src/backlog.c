/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief SD カードに保存する測定値の記録 (送信済みフラグつき. ストア・アンド・フォワード)
 *
 * ファイル backlog.bin は記録の数が CONFIG_THERMO_BACKLOG_MAX_RECORDS 件の輪. 最初は末尾に
 * 追記して大きくなり, その数に達したら, 最も古い記録の位置から上書きする. 記録は 16 バイト
 * (リトルエンディアン).
 *  - 0 から 3: 測った UNIX 時刻 (符号なし 32 ビット)
 *  - 4 から 5: 温度 (10 倍した値)
 *  - 6 から 7: 湿度 (10 倍した値)
 *  - 8: 送信済みフラグ (0: 未送信, 1: 送信済み. 送り終えたときに, この 1 バイトだけを書き換える)
 *  - 9 から 11: 予約 (0)
 *  - 12 から 15: 先頭の 8 バイトの CRC-32 (フラグは含めない)
 *
 * 記録の位置は 2 通りで表す. 物理の位置 (ファイルの中の順番) と, 論理の位置 (古い順. 0
 * が最も古い). ファイルが輪になっていないとき (記録の数が上限に達するまで) は同じ.
 * 上限に達したあとは次に上書きする位置 (head) が最も古い記録.
 */

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <errno.h>   /* ENODEV EINVAL EIO ENOENT */
#include <ff.h>      /* FATFS */
#include <stdbool.h> /* bool */
#include <stdint.h>  /* uint8_t uint32_t int64_t UINT32_MAX */
#include <stdlib.h>  /* EXIT_SUCCESS */

#include "backlog.h"
#include "sdlog.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(thermo_backlog, THERMO_LOG_LEVEL);

/** SD カードのマウントポイント (CONFIG_THERMO_BACKLOG_MOUNT) */
#define MOUNT_POINT CONFIG_THERMO_BACKLOG_MOUNT
/** 記録のファイル */
#define DATA_PATH   MOUNT_POINT "/backlog.bin"

/** 記録 1 件のバイト数 */
#define RECORD_SIZE 16U
/** 記録の, CRC を求める範囲のバイト数 (フラグの前まで) */
#define CRC_RANGE   8U
/** 送信済みフラグの値 */
#define FLAG_SENT   1U

/** 記録の中の位置: 測った UNIX 時刻 */
#define OFF_UNIX 0U
/** 記録の中の位置: 温度 */
#define OFF_TEMP 4U
/** 記録の中の位置: 湿度 */
#define OFF_HUMI 6U
/** 記録の中の位置: 送信済みフラグ */
#define OFF_FLAG 8U
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
/** ファイルにある記録の数 (上限は BACKLOG_MAX_RECORDS) */
static unsigned int count;
/** 次に書く物理の位置 (上限に達していなければ count と同じ. 達していれば, 最も古い記録の位置) */
static unsigned int head;
/** 未送信の記録の数 */
static unsigned int pending;
/** 最も古い未送信の記録の論理の位置 (これより前は全て送信済み. pending が 0 のときは使わない)
 */
static unsigned int first_pos;

static unsigned int physical(unsigned int pos);
static int read_slot(struct fs_file_t *file, unsigned int phys, uint8_t *buf);
static bool decode(const uint8_t *buf, struct backlog_record *rec, bool *sent);
static void encode(uint8_t *out, const struct backlog_record *rec, bool sent);
static int open_file(struct fs_file_t *file, fs_mode_t flags);
static void scan(unsigned int slots);
static int write_record(struct fs_file_t *file, unsigned int phys, const uint8_t *buf);
static int mark_sent(struct fs_file_t *file, unsigned int phys);

/**
 * @brief SD カードをマウントして, 保存してある記録の数と, 未送信の数を調べる
 *
 * ファイルの全ての記録を読んで, 最も新しい記録 (次に書く位置), 未送信の数, 最も古い未送信の記録を
 * 調べる. CRC の合わない記録は無視する (未送信にも数えない). 書きかけの記録 (16 バイトに満たない
 * 端) は切り捨てる.
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      SD カードがない (またはマウントできない)
 */
int backlog_init(void)
{
    struct fs_dirent entry = {.size = 0U}; /* ファイルの情報 */
    unsigned int slots = 0U;               /* ファイルにある記録の数 */
    int err = EXIT_SUCCESS;                /* エラーコード */

    /* やり直し (再初期化) のときは前のマウントを外してから, マウントし直す */
    if (mounted) {
        (void)fs_unmount(&mount);
    }
    mounted = false;
    count = 0U;
    head = 0U;
    pending = 0U;
    first_pos = 0U;

    err = fs_mount(&mount);
    if (err != 0) {
        LOG_ERR("Could not mount the SD card (err %d)", err);
        return -ENODEV;
    }
    mounted = true;

    if (fs_stat(DATA_PATH, &entry) == 0) {
        slots = (unsigned int)MIN(entry.size / RECORD_SIZE, (size_t)BACKLOG_MAX_RECORDS);
    }
    scan(slots);
    LOG_INF("SD card ready: %u records, %u not sent", count, pending);
    /* マウントできたので, ログ (boot.log と error.log) も保存できる */
    sdlog_start();

    return EXIT_SUCCESS;
}

/**
 * @brief 測定値を 1 件, 保存する
 *
 * 記録の数が上限に達していれば, 最も古い記録の位置に上書きする (未送信の記録を上書きしたときは
 * その分, 未送信の数を減らす).
 *
 * @param[in] rec  測定値
 * @param[in] sent 送信済みとして保存するか (ゲートウェイに送った測定値は true)
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      SD カードを使えない
 * @retval -EINVAL      時刻が範囲の外 (0 から UINT32_MAX ではない)
 * @retval negative     ファイルの失敗 (負の errno)
 */
int backlog_append(const struct backlog_record *rec, bool sent)
{
    struct fs_file_t file = {0};                      /* 記録のファイル */
    struct backlog_record old = {0};                  /* 上書きされる記録 */
    uint8_t buf[RECORD_SIZE] = {0};                   /* 書き込む記録 */
    const bool full = (count >= BACKLOG_MAX_RECORDS); /* 輪になっているか */
    bool old_sent = true;         /* 上書きされる記録が送信済みか (読めなければ, 送信済み) */
    int err = EXIT_SUCCESS;       /* エラーコード */
    int close_err = EXIT_SUCCESS; /* 閉じたときのエラーコード */

    if (!mounted) {
        return -ENODEV;
    }
    if ((rec->unix_s < 0) || ((int64_t)UINT32_MAX < rec->unix_s)) {
        return -EINVAL;
    }

    err = open_file(&file, FS_O_CREATE | FS_O_RDWR);
    if (err != 0) {
        return err;
    }
    if (full && (read_slot(&file, head, buf) == 0)) {
        /* 上書きする記録が未送信かを知る (読めない, 壊れている記録は送信済みとして扱う) */
        (void)decode(buf, &old, &old_sent);
    }
    encode(buf, rec, sent);
    err = write_record(&file, head, buf);
    close_err = fs_close(&file);
    if (err != 0) {
        return err;
    }
    if (close_err != 0) {
        LOG_ERR("Could not close the file (err %d)", close_err);
        return close_err;
    }

    if (full) {
        /* 最も古い記録を上書きした: 論理の位置が 1 つずつ前にずれる */
        head = (head + 1U) % BACKLOG_MAX_RECORDS;
        if (!old_sent) {
            pending--;
        }
        first_pos = ((first_pos > 0U) ? (first_pos - 1U) : 0U);
    } else {
        count++;
        head = ((count >= BACKLOG_MAX_RECORDS) ? 0U : count);
    }
    if (!sent) {
        if (pending == 0U) {
            first_pos = count - 1U;
        }
        pending++;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief まだ送っていない記録の数
 *
 * @return 送っていない記録の数 (SD カードを使えないときは 0)
 */
unsigned int backlog_pending(void)
{
    return pending;
}

/**
 * @brief 送っていない記録を古いほうから数えて, index 番目 (0 が一番古い) を読み出す
 *
 * @param[out] rec   記録
 * @param[in]  index 古いほうからの順番 (backlog_pending() より小さいこと)
 * @retval EXIT_SUCCESS 成功
 * @retval -EINVAL      index が範囲の外
 * @retval -ENOENT      記録が見つからない (ファイルが壊れている)
 * @retval negative     ファイルの失敗 (負の errno)
 */
int backlog_read(struct backlog_record *rec, unsigned int index)
{
    struct fs_file_t file = {0};    /* 記録のファイル */
    uint8_t buf[RECORD_SIZE] = {0}; /* 読み出した記録 */
    unsigned int pos = first_pos;   /* 論理の位置 */
    unsigned int seen = 0U;         /* 数えた未送信の記録の数 */
    bool sent = false;              /* 送信済みか */
    int err = EXIT_SUCCESS;         /* エラーコード */

    if (index >= pending) {
        return -EINVAL;
    }

    err = open_file(&file, FS_O_READ);
    if (err != 0) {
        return err;
    }
    for (; pos < count; pos++) {
        err = read_slot(&file, physical(pos), buf);
        if (err != 0) {
            break;
        }
        if (decode(buf, rec, &sent) && (!sent)) {
            if (seen == index) {
                (void)fs_close(&file);
                return EXIT_SUCCESS;
            }
            seen++;
        }
    }
    (void)fs_close(&file);

    return ((err != 0) ? err : -ENOENT);
}

/**
 * @brief 古いほうから count 件を送り終えたことにする (記録のフラグを書き換える)
 *
 * @param[in] n 送り終えた数 (backlog_pending() 以下)
 * @retval EXIT_SUCCESS 成功
 * @retval -EINVAL      n が多すぎる
 * @retval -EIO         記録が足りない (ファイルが壊れている)
 * @retval negative     ファイルの失敗 (負の errno)
 */
int backlog_commit(unsigned int n)
{
    struct fs_file_t file = {0};     /* 記録のファイル */
    uint8_t buf[RECORD_SIZE] = {0};  /* 読み出した記録 */
    struct backlog_record rec = {0}; /* 読み出した記録の内容 */
    unsigned int marked = 0U;        /* 送り終えたことにした数 */
    bool sent = false;               /* 送信済みか */
    int err = EXIT_SUCCESS;          /* エラーコード */

    if (n > pending) {
        return -EINVAL;
    }
    if (n == 0U) {
        return EXIT_SUCCESS;
    }

    err = open_file(&file, FS_O_RDWR);
    if (err != 0) {
        return err;
    }
    while ((marked < n) && (first_pos < count)) {
        err = read_slot(&file, physical(first_pos), buf);
        if (err != 0) {
            break;
        }
        if (decode(buf, &rec, &sent) && (!sent)) {
            err = mark_sent(&file, physical(first_pos));
            if (err != 0) {
                break;
            }
            marked++;
            pending--;
        }
        first_pos++;
    }
    (void)fs_close(&file);
    if (err != 0) {
        LOG_ERR("Could not mark the records as sent (err %d)", err);
        return err;
    }
    if (marked < n) {
        /* 数えた未送信の記録がファイルに見つからない: 数を合わせる */
        LOG_ERR("The records on the SD card are broken (%u of %u marked)", marked, n);
        pending -= (n - marked);
        return -EIO;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief 指定の時刻以降の記録を古い順に, 関数に渡す (送信済みも含む)
 *
 * 記録は時刻の順に並んでいるものとして, 二分探索で, 最初の記録を見つける.
 *
 * @param[in] since_unix_s この UNIX 時刻 [s] 以降の記録を渡す
 * @param[in] visit        記録ごとに呼ぶ関数
 * @param[in] ctx          visit に渡す値
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      SD カードを使えない
 * @retval negative     ファイルの失敗 (負の errno)
 */
int backlog_foreach_since(int64_t since_unix_s, backlog_visit_t visit, void *ctx)
{
    struct fs_file_t file = {0};     /* 記録のファイル */
    struct backlog_record rec = {0}; /* 読み出した記録 */
    uint8_t buf[RECORD_SIZE] = {0};  /* 読み出した記録 */
    unsigned int lo = 0U;            /* 探す範囲の下端 (論理の位置) */
    unsigned int hi = count;         /* 探す範囲の上端 */
    unsigned int mid = 0U;           /* 探す位置 */
    bool sent = false;               /* 送信済みか */
    int err = EXIT_SUCCESS;          /* エラーコード */

    if (!mounted) {
        return -ENODEV;
    }
    if (count == 0U) {
        return EXIT_SUCCESS;
    }

    err = open_file(&file, FS_O_READ);
    if (err != 0) {
        return err;
    }
    while (lo < hi) {
        mid = lo + ((hi - lo) / 2U);
        err = read_slot(&file, physical(mid), buf);
        if (err != 0) {
            break;
        }
        /* 壊れた記録は新しいものとして扱う (探す位置が早すぎるほうに寄る. 余分に渡すだけ) */
        if ((!decode(buf, &rec, &sent)) || (since_unix_s <= rec.unix_s)) {
            hi = mid;
        } else {
            lo = mid + 1U;
        }
    }
    for (; (err == 0) && (lo < count); lo++) {
        err = read_slot(&file, physical(lo), buf);
        if ((err == 0) && decode(buf, &rec, &sent)) {
            visit(&rec, ctx);
        }
    }
    (void)fs_close(&file);

    return err;
}

/**
 * 論理の位置 (古い順) を物理の位置 (ファイルの中の順番) にする
 *
 * @param[in] pos 論理の位置 (count より小さいこと)
 * @return 物理の位置
 */
static unsigned int physical(unsigned int pos)
{
    const unsigned int oldest = ((count >= BACKLOG_MAX_RECORDS) ? head : 0U); /* 最も古い記録 */

    return (oldest + pos) % count;
}

/**
 * 物理の位置の記録 (16 バイト) を読む
 *
 * @param[in]  file 開いたファイル
 * @param[in]  phys 物理の位置
 * @param[out] buf  読み出した記録 (RECORD_SIZE バイト)
 * @retval EXIT_SUCCESS 成功
 * @retval -EIO         読み出しが短い
 * @retval negative     失敗 (負の errno)
 */
static int read_slot(struct fs_file_t *file, unsigned int phys, uint8_t *buf)
{
    ssize_t got = 0;        /* 読み出したバイト数 */
    int err = EXIT_SUCCESS; /* エラーコード */

    err = fs_seek(file, (off_t)(phys * RECORD_SIZE), FS_SEEK_SET);
    if (err != 0) {
        return err;
    }
    got = fs_read(file, buf, RECORD_SIZE);
    if (got < 0) {
        return (int)got;
    }

    return ((got == (ssize_t)RECORD_SIZE) ? EXIT_SUCCESS : -EIO);
}

/**
 * 16 バイトの記録を読む (CRC が合わなければ, 無効)
 *
 * @param[in]  buf  記録 (RECORD_SIZE バイト)
 * @param[out] rec  記録の内容 (有効なときだけ)
 * @param[out] sent 送信済みか (有効なときだけ)
 * @retval true  有効な記録
 * @retval false CRC が合わない
 */
static bool decode(const uint8_t *buf, struct backlog_record *rec, bool *sent)
{
    if (sys_get_le32(&buf[OFF_CRC]) != crc32_ieee(buf, CRC_RANGE)) {
        return false;
    }
    rec->unix_s = (int64_t)sys_get_le32(&buf[OFF_UNIX]);
    rec->temp_x10 = (int16_t)sys_get_le16(&buf[OFF_TEMP]);
    rec->humidity_x10 = sys_get_le16(&buf[OFF_HUMI]);
    *sent = (buf[OFF_FLAG] == FLAG_SENT);

    return true;
}

/**
 * 記録を 16 バイトに符号化する (末尾に CRC を付ける)
 *
 * @param[out] out  書き込み先 (RECORD_SIZE バイト)
 * @param[in]  rec  記録
 * @param[in]  sent 送信済みか
 */
static void encode(uint8_t *out, const struct backlog_record *rec, bool sent)
{
    sys_put_le32((uint32_t)rec->unix_s, &out[OFF_UNIX]);
    sys_put_le16((uint16_t)rec->temp_x10, &out[OFF_TEMP]);
    sys_put_le16(rec->humidity_x10, &out[OFF_HUMI]);
    out[OFF_FLAG] = (sent ? FLAG_SENT : 0U);
    out[OFF_FLAG + 1U] = 0U;
    out[OFF_FLAG + 2U] = 0U;
    out[OFF_FLAG + 3U] = 0U;
    sys_put_le32(crc32_ieee(out, CRC_RANGE), &out[OFF_CRC]);
}

/**
 * 記録のファイルを開く
 *
 * @param[out] file  ファイル
 * @param[in]  flags 開き方 (FS_O_*)
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
static int open_file(struct fs_file_t *file, fs_mode_t flags)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    fs_file_t_init(file);
    err = fs_open(file, DATA_PATH, flags);
    if (err != 0) {
        LOG_ERR("Could not open %s (err %d)", DATA_PATH, err);
    }

    return err;
}

/**
 * ファイルの記録を全て読んで, 状態 (count, head, pending, first_pos) を求める
 *
 * @param[in] slots ファイルにある記録の数
 */
static void scan(unsigned int slots)
{
    struct fs_file_t file = {0};     /* 記録のファイル */
    struct backlog_record rec = {0}; /* 読み出した記録 */
    uint8_t buf[RECORD_SIZE] = {0};  /* 読み出した記録 */
    int64_t newest = -1;             /* 最も新しい記録の時刻 */
    int64_t oldest_unsent = -1;      /* 最も古い未送信の記録の時刻 */
    unsigned int newest_phys = 0U;   /* 最も新しい記録の物理の位置 */
    unsigned int unsent_phys = 0U;   /* 最も古い未送信の記録の物理の位置 */
    unsigned int phys = 0U;          /* 物理の位置 */
    bool sent = false;               /* 送信済みか */

    if ((slots == 0U) || (open_file(&file, FS_O_READ) != 0)) {
        return;
    }
    for (phys = 0U; phys < slots; phys++) {
        if (fs_read(&file, buf, RECORD_SIZE) != (ssize_t)RECORD_SIZE) {
            break;
        }
        count++;
        if (!decode(buf, &rec, &sent)) {
            continue;
        }
        if (newest <= rec.unix_s) {
            newest = rec.unix_s;
            newest_phys = phys;
        }
        if ((!sent) && ((oldest_unsent < 0) || (rec.unix_s < oldest_unsent))) {
            oldest_unsent = rec.unix_s;
            unsent_phys = phys;
        }
        if (!sent) {
            pending++;
        }
    }
    (void)fs_close(&file);

    if (count >= BACKLOG_MAX_RECORDS) {
        head = ((newest >= 0) ? ((newest_phys + 1U) % BACKLOG_MAX_RECORDS) : 0U);
    } else {
        head = count;
    }
    if (pending > 0U) {
        first_pos = ((unsent_phys + count) - ((count >= BACKLOG_MAX_RECORDS) ? head : 0U)) % count;
    }
}

/**
 * 物理の位置に, 記録を書く
 *
 * @param[in] file 開いたファイル
 * @param[in] phys 物理の位置
 * @param[in] buf  記録 (RECORD_SIZE バイト)
 * @retval EXIT_SUCCESS 成功
 * @retval -EIO         書き込みが短い
 * @retval negative     失敗 (負の errno)
 */
static int write_record(struct fs_file_t *file, unsigned int phys, const uint8_t *buf)
{
    ssize_t written = 0;    /* 書き込んだバイト数 */
    int err = EXIT_SUCCESS; /* エラーコード */

    err = fs_seek(file, (off_t)(phys * RECORD_SIZE), FS_SEEK_SET);
    if (err != 0) {
        return err;
    }
    written = fs_write(file, buf, RECORD_SIZE);
    if (written < 0) {
        LOG_ERR("Could not write the record (%d)", (int)written);
        return (int)written;
    }

    return ((written == (ssize_t)RECORD_SIZE) ? EXIT_SUCCESS : -EIO);
}

/**
 * 物理の位置の記録の, 送信済みフラグを書き換える
 *
 * @param[in] file 開いたファイル (書き込める)
 * @param[in] phys 物理の位置
 * @retval EXIT_SUCCESS 成功
 * @retval -EIO         書き込みが短い
 * @retval negative     失敗 (負の errno)
 */
static int mark_sent(struct fs_file_t *file, unsigned int phys)
{
    const uint8_t flag = FLAG_SENT; /* 書き込むフラグ */
    ssize_t written = 0;            /* 書き込んだバイト数 */
    int err = EXIT_SUCCESS;         /* エラーコード */

    err = fs_seek(file, (off_t)((phys * RECORD_SIZE) + OFF_FLAG), FS_SEEK_SET);
    if (err != 0) {
        return err;
    }
    written = fs_write(file, &flag, sizeof(flag));
    if (written < 0) {
        return (int)written;
    }

    return ((written == (ssize_t)sizeof(flag)) ? EXIT_SUCCESS : -EIO);
}
