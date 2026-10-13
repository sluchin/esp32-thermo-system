/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_VERSION_H
#define THERMO_VERSION_H

/**
 * @file
 * @brief ファームウェアのバージョンと git のハッシュ (thermo-node と thermo-gateway で共有する)
 *
 * 値は、ビルドのときに app/version.cmake が、マクロとして渡す (リポジトリ直下の VERSION と、
 * git describe --always --dirty). 渡されなかったとき (単体テストなど) は unknown.
 */

#ifndef THERMO_VERSION
/** ファームウェアのバージョン (リポジトリ直下の VERSION) */
#define THERMO_VERSION "unknown"
#endif

#ifndef THERMO_GIT_HASH
/** git のハッシュ (コミットしていない変更があれば、末尾に -dirty が付く) */
#define THERMO_GIT_HASH "unknown"
#endif

#endif /* THERMO_VERSION_H */
