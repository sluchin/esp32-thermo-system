/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef THERMO_GATEWAY_CFG_H
#define THERMO_GATEWAY_CFG_H

/**
 * @file
 * @brief 実行時の設定 (WiFi、AWS IoT Core のエンドポイント、証明書) の保存と読み出し
 *
 * 設定は、Zephyr のシェル (`thermo` コマンド。cfg_shell.c) で入力して、フラッシュ (settings の
 * NVS) に保存する。TLS の証明書は、Zephyr の `cred` コマンドで、いったん RAM に登録してから、
 * `thermo save-certs` で、フラッシュに保存する。
 */

#include <zephyr/net/tls_credentials.h>
#include <stdbool.h> /* bool */
#include <stddef.h>

/** TLS の証明書を登録する、セキュリティタグ (CA、クライアント証明書、秘密鍵で共通) */
#define CFG_TLS_SEC_TAG 1

/** SSID の最大長 [byte] (NUL を除く) */
#define CFG_SSID_MAX      32u
/** WiFi のパスワードの最大長 [byte] (NUL を除く) */
#define CFG_PSK_MAX       64u
/** AWS IoT Core のエンドポイントの最大長 [byte] (NUL を除く) */
#define CFG_ENDPOINT_MAX  128u
/** クライアント ID の最大長 [byte] (NUL を除く) */
#define CFG_CLIENT_ID_MAX 64u
/** 証明書 (CA、クライアント証明書) 1 つの最大サイズ [byte] (PEM。NUL を含む) */
#define CFG_CERT_MAX      1536u
/** 秘密鍵の最大サイズ [byte] (PEM。NUL を含む。RSA 2048 bit の PEM は約 1700 byte) */
#define CFG_KEY_PEM_MAX   1792u

/** 設定の項目 */
enum cfg_key {
    CFG_KEY_SSID,      /**< WiFi の SSID */
    CFG_KEY_PSK,       /**< WiFi のパスワード (空なら、オープンネットワーク) */
    CFG_KEY_ENDPOINT,  /**< AWS IoT Core のエンドポイント */
    CFG_KEY_CLIENT_ID, /**< MQTT のクライアント ID (トピックにも使う) */
    CFG_KEY_COUNT,     /**< 項目の数 */
};

/** 証明書の種類 */
enum cfg_cred {
    CFG_CRED_CA,   /**< AWS のルート CA 証明書 (サーバの確認用) */
    CFG_CRED_CERT, /**< クライアント証明書 (デバイス証明書) */
    CFG_CRED_KEY,  /**< クライアントの秘密鍵 */
    CFG_CRED_COUNT /**< 種類の数 */
};

/**
 * @brief 設定を、フラッシュから読み込む (証明書は、TLS の認証情報に登録する)。
 *
 * @retval EXIT_SUCCESS 成功 (保存された設定が、なくても成功)
 * @retval negative     失敗 (負の errno)
 */
int cfg_init(void);

/**
 * @brief 項目の名前 ("ssid" など) から、項目を探す。
 *
 * @param[in] name 項目の名前
 * @return 項目 (enum cfg_key)。ない場合は -ENOENT
 */
int cfg_key_from_name(const char *name);

/**
 * @brief 項目の名前を返す。
 *
 * @param[in] key 項目
 * @return 項目の名前 (NUL で終わる)
 */
const char *cfg_key_name(enum cfg_key key);

/**
 * @brief 項目の値を返す。
 *
 * @param[in] key 項目
 * @return 値 (NUL で終わる。未設定なら空の文字列)
 */
const char *cfg_get(enum cfg_key key);

/**
 * @brief 項目の値を設定して、フラッシュに保存する。
 *
 * @param[in] key   項目
 * @param[in] value 値 (空でもよい)
 * @retval EXIT_SUCCESS 成功
 * @retval -EINVAL      値が長すぎる
 * @retval negative     保存に失敗 (負の errno)
 */
int cfg_set(enum cfg_key key, const char *value);

/**
 * @brief 証明書の種類の名前 ("ca" など) から、種類を探す。
 *
 * @param[in] name 名前
 * @return 種類 (enum cfg_cred)。ない場合は -ENOENT
 */
int cfg_cred_from_name(const char *name);

/**
 * @brief 証明書の種類の名前を返す。
 *
 * @param[in] cred 種類
 * @return 名前 (NUL で終わる)
 */
const char *cfg_cred_name(enum cfg_cred cred);

/**
 * @brief 証明書が、TLS の認証情報に登録されているか調べる。
 *
 * @param[in] cred 種類
 * @return 登録されていれば true
 */
bool cfg_has_cred(enum cfg_cred cred);

/**
 * @brief TLS の認証情報に登録された証明書を、フラッシュに保存する。
 *
 * 保存した証明書は、次の起動から、cfg_init() が、自動で登録する。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval -ENOENT      登録されていない証明書がある
 * @retval negative     失敗 (負の errno)
 */
int cfg_save_credentials(void);

/**
 * @brief 接続に必要な設定が、全て揃っているか調べる。
 *
 * SSID、エンドポイント、クライアント ID が空でなく、3 つの証明書が登録されていること。
 *
 * @return 揃っていれば true
 */
bool cfg_is_complete(void);

/**
 * @brief 設定と証明書を全て消す (フラッシュからも消す)。
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno。最初のエラーを返すが、全ての項目の削除を試みる)
 */
int cfg_reset(void);

#endif /* THERMO_GATEWAY_CFG_H */
