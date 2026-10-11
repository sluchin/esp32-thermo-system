/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief FatFS の ff.h の代わり (FATFS の型だけ. ファイルシステムを持たないテストで使う)
 */

#ifndef THERMO_NODE_TESTS_BACKLOG_FSERR_STUB_FF_H
#define THERMO_NODE_TESTS_BACKLOG_FSERR_STUB_FF_H

/** FatFS のファイルシステムの代わり */
typedef struct {
    int unused; /**< 使わない */
} FATFS;

#endif /* THERMO_NODE_TESTS_BACKLOG_FSERR_STUB_FF_H */
