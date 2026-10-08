/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の BLE 実装 (アドバタイズと, GATT での温度の配信)
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>  /* ENOTCONN ERANGE */
#include <stdint.h> /* uint16_t uint8_t uint32_t int64_t */
#include <stdlib.h> /* EXIT_SUCCESS */

#include "ble.h"
#include "thermo_ble_uuid.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(ble_thermo_node, THERMO_LOG_LEVEL);

/** アドバタイズデータ: フラグ (LE 一般発見可能, BR/EDR 非対応) とデバイス名 */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
};

/**
 * スキャン応答データ: Thermo サービスの UUID (128 bit)
 *
 * UUID (128 bit) は 18 byte あるので, デバイス名と合わせると, アドバタイズデータ (31 byte)
 * に入らない. ゲートウェイはアクティブスキャンで, スキャン応答も受信して, ノードを見分ける.
 */
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, THERMO_UUID_SERVICE_VAL),
};

/** 最後に通知した温度 [℃ の 10 倍]. 読み取り (read) で返す */
static int16_t latest_temp_x10;
/** 最後に通知した湿度 [% の 10 倍]. 通知の前は, 湿度なし. 読み取り (read) で返す */
static uint16_t latest_humidity_x10 = THERMO_HUMIDITY_NONE;

#ifdef CONFIG_THERMO_RTC
/** ゲートウェイから時刻を受け取ったときのコールバック (NULL なら, 書き込みを拒否する) */
static ble_time_cb_t time_cb;

static ssize_t write_time(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                          uint16_t len, uint16_t offset, uint8_t flags);
#endif

static void pack_value(uint8_t *out, int16_t temp_x10, uint16_t humidity_x10);
static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                                uint16_t len, uint16_t offset);
static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);

/**
 * Thermo サービスの属性
 *
 * 0: サービス, 1: 特性の宣言, 2: 特性の値 (温度), 3: CCC (通知の設定),
 * (CONFIG_THERMO_RTC のとき) 4: 特性の宣言, 5: 特性の値 (時刻. 書き込みだけ)
 */
static struct bt_gatt_attr thermo_attrs[] = {
    BT_GATT_PRIMARY_SERVICE(THERMO_UUID_SERVICE),
    BT_GATT_CHARACTERISTIC(THERMO_UUID_TEMPERATURE, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, read_temperature, NULL, NULL),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
#ifdef CONFIG_THERMO_RTC
    BT_GATT_CHARACTERISTIC(THERMO_UUID_TIME, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL,
                           write_time, NULL),
#endif
};

/** 温度の特性の値の属性の位置 (thermo_attrs) */
#define ATTR_INDEX_TEMPERATURE_VALUE 2

/** Thermo サービス */
static struct bt_gatt_service thermo_service = BT_GATT_SERVICE(thermo_attrs);

/** 接続のコールバック */
static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

/**
 * @brief Bluetooth スタックを初期化して, Thermo サービス (GATT) を登録する
 *
 * 接続のコールバックも登録する. 接続が切れたら, アドバタイズを再開する.
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_init(void)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    err = bt_enable(NULL);
    if (err != 0) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    err = bt_gatt_service_register(&thermo_service);
    if (err != 0) {
        LOG_ERR("GATT service register failed (err %d)", err);
        return err;
    }

    err = bt_conn_cb_register(&conn_callbacks);
    if (err != 0) {
        LOG_ERR("Connection callback register failed (err %d)", err);
        return err;
    }
    LOG_INF("Bluetooth initialized");

    return EXIT_SUCCESS;
}

/**
 * @brief 接続可能なアドバタイズを開始する
 *
 * ゲートウェイから検出されるよう, デバイス名を含むアドバタイズデータと,
 * Thermo サービスの UUID を含むスキャン応答データを送信する.
 * 事前に ble_init() を呼び出しておくこと.
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_advertise(void)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err != 0) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return err;
    }
    LOG_INF("Advertising started");

    return EXIT_SUCCESS;
}

/**
 * @brief 温度と湿度を更新して, 接続しているゲートウェイへ通知 (notify) する
 *
 * 通知を購読しているゲートウェイがいなくても, 値は更新される (読み取り (read) で取得できる).
 * 接続しているゲートウェイがいないときは, 成功を返す.
 *
 * @param[in] temp_x10     温度 [℃ の 10 倍]
 * @param[in] humidity_x10 湿度 [% の 10 倍] (湿度がなければ THERMO_HUMIDITY_NONE)
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     通知に失敗 (負の errno)
 */
int ble_notify_temperature(int16_t temp_x10, uint16_t humidity_x10)
{
    uint8_t value[THERMO_TEMPERATURE_SIZE] = {0}; /* 温度と湿度 (リトルエンディアン) */
    int err = EXIT_SUCCESS;                       /* エラーコード */

    latest_temp_x10 = temp_x10;
    latest_humidity_x10 = humidity_x10;
    pack_value(value, temp_x10, humidity_x10);

    LOG_HEXDUMP_DBG(value, sizeof(value), "Notify");
    err = bt_gatt_notify(NULL, &thermo_attrs[ATTR_INDEX_TEMPERATURE_VALUE], value, sizeof(value));
    if (err == -ENOTCONN) {
        /* 接続しているゲートウェイがいない (値は更新したのであとで読み取れる) */
        LOG_DBG("No connection to notify");
        return EXIT_SUCCESS;
    }
    if (err != 0) {
        LOG_ERR("Notify failed (err %d)", err);
        return err;
    }

    return EXIT_SUCCESS;
}

#ifdef CONFIG_THERMO_RTC
/**
 * @brief ゲートウェイから時刻を受け取ったときのコールバックを登録する
 *
 * @param[in] cb コールバック (NULL なら, 時刻の書き込みを拒否する)
 */
void ble_set_time_callback(ble_time_cb_t cb)
{
    time_cb = cb;
}
#endif

/**
 * 温度の特性の読み取り (read) のコールバック
 *
 * @param[in] conn 接続
 * @param[in] attr 特性の値の属性
 * @param[out] buf 値を書き込むバッファ
 * @param[in] len バッファの大きさ
 * @param[in] offset 読み取りの開始位置
 * @return 読み取った大きさ (失敗なら, 負の ATT エラー)
 */
static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                                uint16_t len, uint16_t offset)
{
    uint8_t value[THERMO_TEMPERATURE_SIZE] = {0}; /* 温度と湿度 (リトルエンディアン) */

    pack_value(value, latest_temp_x10, latest_humidity_x10);
    LOG_HEXDUMP_DBG(value, sizeof(value), "Read response");

    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

#ifdef CONFIG_THERMO_RTC
/**
 * 時刻の特性の書き込み (write) のコールバック (ゲートウェイが送った UTC の時刻を, 設定する)
 *
 * Bluetooth のスレッドから呼ばれる. 値は, UTC の UNIX 時刻 [s] (uint32, リトルエンディアン).
 * 認証はしないので, 近くの BLE 機器は, 誰でも書き込める (表示する時刻だけに影響する).
 *
 * @param[in] conn   接続
 * @param[in] attr   特性の値の属性
 * @param[in] buf    書き込まれた値
 * @param[in] len    値の大きさ
 * @param[in] offset 書き込みの開始位置
 * @param[in] flags  書き込みのフラグ
 *
 * @return 書き込んだ大きさ (失敗なら, 負の ATT エラー)
 */
static ssize_t write_time(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                          uint16_t len, uint16_t offset, uint8_t flags)
{
    uint32_t unix_s = 0U;   /* 受け取った UTC の UNIX 時刻 [s] */
    int err = EXIT_SUCCESS; /* エラーコード */

    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len != THERMO_TIME_SIZE) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    if (time_cb == NULL) {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }

    unix_s = sys_get_le32((const uint8_t *)buf);
    err = time_cb((int64_t)unix_s);
    if (err == -ERANGE) {
        LOG_WRN("The time from the gateway is out of range (%u)", unix_s);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Could not set the time (err %d)", err);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    LOG_INF("Time set by the gateway (UNIX time %u)", unix_s);

    return (ssize_t)len;
}
#endif

/**
 * 温度と湿度の特性の値 (THERMO_TEMPERATURE_SIZE バイト) を作る
 *
 * @param[out] out          出力先 (THERMO_TEMPERATURE_SIZE バイト以上)
 * @param[in]  temp_x10     温度 [℃ の 10 倍]
 * @param[in]  humidity_x10 湿度 [% の 10 倍]
 */
static void pack_value(uint8_t *out, int16_t temp_x10, uint16_t humidity_x10)
{
    sys_put_le16((uint16_t)temp_x10, &out[0]);
    sys_put_le16(humidity_x10, &out[2]);
}

/**
 * 接続のコールバック
 *
 * @param[in] conn 接続
 * @param[in] err 接続の結果 (0 なら成功)
 */
static void connected(struct bt_conn *conn, uint8_t err)
{
    ARG_UNUSED(conn);

    if (err != 0U) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }
    LOG_INF("Connected");
}

/**
 * 切断のコールバック (接続が切れたら, ゲートウェイから検出されるよう, アドバタイズを再開する)
 *
 * @param[in] conn 接続
 * @param[in] reason 切断の理由
 */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    ARG_UNUSED(conn);

    LOG_INF("Disconnected (reason 0x%02x)", reason);

    err = ble_advertise();
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Failed to restart advertising (err %d)", err);
    }
}
