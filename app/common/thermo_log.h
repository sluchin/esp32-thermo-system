/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_LOG_H
#define THERMO_LOG_H

/**
 * @file
 * @brief アプリのログのレベル (thermo-node と thermo-gateway で共有する)
 *
 * アプリのモジュールは, LOG_MODULE_REGISTER() のレベルに THERMO_LOG_LEVEL を渡す.
 * cmake の -DTHERMO_DEBUG_LOG=ON を付けると, アプリのソースだけに THERMO_LOG_LEVEL=4 (DBG) が付く.
 * Zephyr のモジュール (Bluetooth, ネットワークなど) は, CONFIG_LOG_DEFAULT_LEVEL のままで,
 * DBG のログは出ない (出すと, ログの量が多すぎて, 動かなくなる).
 */

#ifndef THERMO_LOG_LEVEL
/** アプリのモジュールのログのレベル (既定は CONFIG_LOG_DEFAULT_LEVEL) */
#define THERMO_LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#endif

#endif /* THERMO_LOG_H */
