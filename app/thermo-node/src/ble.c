/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-node の BLE 実装 (アドバタイズと、GATT での温度の配信)
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "ble.h"
#include "thermo_ble_uuid.h"

LOG_MODULE_REGISTER(ble_thermo_node);

/** アドバタイズデータ: フラグ (LE 一般発見可能、BR/EDR 非対応) とデバイス名 */
static const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1u),
};

/**
 * スキャン応答データ: Thermo サービスの UUID (128 bit)
 *
 * UUID (128 bit) は 18 byte あるので、デバイス名と合わせると、アドバタイズデータ (31 byte)
 * に入らない。ゲートウェイは、アクティブスキャンで、スキャン応答も受信して、ノードを見分ける。
 */
static const struct bt_data sd[] = {
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, THERMO_UUID_SERVICE_VAL),
};

/** 温度の特性の値 (ADC の生値)。読み取り (read) で返す */
static uint16_t temperature_raw;

/**
 * 温度の特性の読み取り (read) のコールバック
 *
 * @param[in] conn 接続
 * @param[in] attr 特性の値の属性
 * @param[out] buf 値を書き込むバッファ
 * @param[in] len バッファの大きさ
 * @param[in] offset 読み取りの開始位置
 * @return 読み取った大きさ (失敗なら、負の ATT エラー)
 */
static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                                uint16_t len, uint16_t offset)
{
    uint16_t value = sys_cpu_to_le16(temperature_raw);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

/**
 * Thermo サービスの属性
 *
 * 0: サービス、1: 特性の宣言、2: 特性の値 (温度)、3: CCC (通知の設定)
 */
static struct bt_gatt_attr thermo_attrs[] = {
        BT_GATT_PRIMARY_SERVICE(THERMO_UUID_SERVICE),
        BT_GATT_CHARACTERISTIC(THERMO_UUID_TEMPERATURE, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                               BT_GATT_PERM_READ, read_temperature, NULL, NULL),
        BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
};

/** 温度の特性の値の属性の位置 (thermo_attrs) */
#define ATTR_INDEX_TEMPERATURE_VALUE 2

/** Thermo サービス */
static struct bt_gatt_service thermo_service = BT_GATT_SERVICE(thermo_attrs);

/**
 * 接続のコールバック
 *
 * @param[in] conn 接続
 * @param[in] err 接続の結果 (0 なら成功)
 */
static void connected(struct bt_conn *conn, uint8_t err)
{
    ARG_UNUSED(conn);

    if (err != 0u) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }
    LOG_INF("Connected");
}

/**
 * 切断のコールバック (接続が切れたら、ゲートウェイから検出されるよう、アドバタイズを再開する)
 *
 * @param[in] conn 接続
 * @param[in] reason 切断の理由
 */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    int err = EXIT_SUCCESS;

    ARG_UNUSED(conn);

    LOG_INF("Disconnected (reason 0x%02x)", reason);

    err = ble_advertise();
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Failed to restart advertising (err %d)", err);
    }
}

/** 接続のコールバック */
static struct bt_conn_cb conn_callbacks = {
        .connected = connected,
        .disconnected = disconnected,
};

/* Bluetooth を有効にして、GATT サービスと接続のコールバックを登録する */
int ble_init(void)
{
    int err = EXIT_SUCCESS;

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

/* ゲートウェイから見つけられるよう、アドバタイズを始める */
int ble_advertise(void)
{
    int err = EXIT_SUCCESS;

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err != 0) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return err;
    }

    LOG_INF("Advertising started");
    return EXIT_SUCCESS;
}

/* 温度を、通知する (接続している相手がいなければ、何もしない) */
int ble_notify_temperature(uint16_t raw)
{
    uint16_t value = sys_cpu_to_le16(raw);
    int err = EXIT_SUCCESS;

    temperature_raw = raw;

    err = bt_gatt_notify(NULL, &thermo_attrs[ATTR_INDEX_TEMPERATURE_VALUE], &value, sizeof(value));
    if (err == -ENOTCONN) {
        /* 接続しているゲートウェイがいない (値は更新したので、あとで読み取れる) */
        LOG_DBG("No connection to notify");
        return EXIT_SUCCESS;
    }
    if (err != 0) {
        LOG_ERR("Notify failed (err %d)", err);
        return err;
    }

    return EXIT_SUCCESS;
}
