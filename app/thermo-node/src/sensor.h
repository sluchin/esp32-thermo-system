/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_SENSOR_H
#define THERMO_NODE_SENSOR_H

/**
 * @file
 * @brief thermo-node の温度センサ (ADC) 制御
 */

#include <stdint.h> /* uint16_t */

/**
 * @brief センサを初期化する。
 *
 * Devicetree に ADC チャンネルが無い場合 (シミュレータ等) は警告をログ出力するのみで成功とする。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENODEV      ADC コントローラが利用可能でない
 * @retval negative     ADC チャンネル設定の失敗 (負の errno)
 */
int sensor_init(void);

/**
 * @brief 温度の生値 (ADC カウント) を読み取る。
 *
 * ADC 未設定の場合は乱数によるシミュレーション値を返す。
 *
 * @param[out] value 読み取った生値の格納先 (NULL 不可)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     ADC 読み取りの失敗 (負の errno)
 */
int sensor_read_temperature(uint16_t *value);

#endif /* THERMO_NODE_SENSOR_H */
