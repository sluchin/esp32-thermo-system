/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_NODE_NOTES_H
#define THERMO_NODE_NOTES_H

/**
 * @file
 * @brief 音名から周波数 [Hz] への表 (平均律. A4 = 440 Hz)
 *
 * メロディを, 周波数の数値ではなく, 音名で書くための表. NOTE_C4 は, 中央のド (C4) で,
 * 末尾の数字は, オクターブ. 半音は S (シャープ) で書く (NOTE_CS4 は, ド#). フラットは,
 * 同じ高さの, シャープの名前を使う (レ♭ は NOTE_CS, ミ♭ は NOTE_DS, ソ♭ は NOTE_FS,
 * ラ♭ は NOTE_GS, シ♭ は NOTE_AS). 休符は, NOTE_REST.
 */

/** 休符 (音を出さない) */
#define NOTE_REST 0U

#define NOTE_C3  131U  /**< ド (オクターブ 3) [Hz] */
#define NOTE_CS3 139U  /**< ド# (オクターブ 3) [Hz] */
#define NOTE_D3  147U  /**< レ (オクターブ 3) [Hz] */
#define NOTE_DS3 156U  /**< レ# (オクターブ 3) [Hz] */
#define NOTE_E3  165U  /**< ミ (オクターブ 3) [Hz] */
#define NOTE_F3  175U  /**< ファ (オクターブ 3) [Hz] */
#define NOTE_FS3 185U  /**< ファ# (オクターブ 3) [Hz] */
#define NOTE_G3  196U  /**< ソ (オクターブ 3) [Hz] */
#define NOTE_GS3 208U  /**< ソ# (オクターブ 3) [Hz] */
#define NOTE_A3  220U  /**< ラ (オクターブ 3) [Hz] */
#define NOTE_AS3 233U  /**< ラ# (オクターブ 3) [Hz] */
#define NOTE_B3  247U  /**< シ (オクターブ 3) [Hz] */
#define NOTE_C4  262U  /**< ド (オクターブ 4) [Hz] */
#define NOTE_CS4 277U  /**< ド# (オクターブ 4) [Hz] */
#define NOTE_D4  294U  /**< レ (オクターブ 4) [Hz] */
#define NOTE_DS4 311U  /**< レ# (オクターブ 4) [Hz] */
#define NOTE_E4  330U  /**< ミ (オクターブ 4) [Hz] */
#define NOTE_F4  349U  /**< ファ (オクターブ 4) [Hz] */
#define NOTE_FS4 370U  /**< ファ# (オクターブ 4) [Hz] */
#define NOTE_G4  392U  /**< ソ (オクターブ 4) [Hz] */
#define NOTE_GS4 415U  /**< ソ# (オクターブ 4) [Hz] */
#define NOTE_A4  440U  /**< ラ (オクターブ 4) [Hz] */
#define NOTE_AS4 466U  /**< ラ# (オクターブ 4) [Hz] */
#define NOTE_B4  494U  /**< シ (オクターブ 4) [Hz] */
#define NOTE_C5  523U  /**< ド (オクターブ 5) [Hz] */
#define NOTE_CS5 554U  /**< ド# (オクターブ 5) [Hz] */
#define NOTE_D5  587U  /**< レ (オクターブ 5) [Hz] */
#define NOTE_DS5 622U  /**< レ# (オクターブ 5) [Hz] */
#define NOTE_E5  659U  /**< ミ (オクターブ 5) [Hz] */
#define NOTE_F5  698U  /**< ファ (オクターブ 5) [Hz] */
#define NOTE_FS5 740U  /**< ファ# (オクターブ 5) [Hz] */
#define NOTE_G5  784U  /**< ソ (オクターブ 5) [Hz] */
#define NOTE_GS5 831U  /**< ソ# (オクターブ 5) [Hz] */
#define NOTE_A5  880U  /**< ラ (オクターブ 5) [Hz] */
#define NOTE_AS5 932U  /**< ラ# (オクターブ 5) [Hz] */
#define NOTE_B5  988U  /**< シ (オクターブ 5) [Hz] */
#define NOTE_C6  1047U /**< ド (オクターブ 6) [Hz] */
#define NOTE_CS6 1109U /**< ド# (オクターブ 6) [Hz] */
#define NOTE_D6  1175U /**< レ (オクターブ 6) [Hz] */
#define NOTE_DS6 1245U /**< レ# (オクターブ 6) [Hz] */
#define NOTE_E6  1319U /**< ミ (オクターブ 6) [Hz] */
#define NOTE_F6  1397U /**< ファ (オクターブ 6) [Hz] */
#define NOTE_FS6 1480U /**< ファ# (オクターブ 6) [Hz] */
#define NOTE_G6  1568U /**< ソ (オクターブ 6) [Hz] */
#define NOTE_GS6 1661U /**< ソ# (オクターブ 6) [Hz] */
#define NOTE_A6  1760U /**< ラ (オクターブ 6) [Hz] */
#define NOTE_AS6 1865U /**< ラ# (オクターブ 6) [Hz] */
#define NOTE_B6  1976U /**< シ (オクターブ 6) [Hz] */
#define NOTE_C7  2093U /**< ド (オクターブ 7) [Hz] */
#define NOTE_CS7 2217U /**< ド# (オクターブ 7) [Hz] */
#define NOTE_D7  2349U /**< レ (オクターブ 7) [Hz] */
#define NOTE_DS7 2489U /**< レ# (オクターブ 7) [Hz] */
#define NOTE_E7  2637U /**< ミ (オクターブ 7) [Hz] */
#define NOTE_F7  2794U /**< ファ (オクターブ 7) [Hz] */
#define NOTE_FS7 2960U /**< ファ# (オクターブ 7) [Hz] */
#define NOTE_G7  3136U /**< ソ (オクターブ 7) [Hz] */
#define NOTE_GS7 3322U /**< ソ# (オクターブ 7) [Hz] */
#define NOTE_A7  3520U /**< ラ (オクターブ 7) [Hz] */
#define NOTE_AS7 3729U /**< ラ# (オクターブ 7) [Hz] */
#define NOTE_B7  3951U /**< シ (オクターブ 7) [Hz] */
#define NOTE_C8  4186U /**< ド (オクターブ 8) [Hz] */

#endif /* THERMO_NODE_NOTES_H */
