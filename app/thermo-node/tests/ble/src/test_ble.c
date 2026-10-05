/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief ble.c の単体テスト
 *
 * Bluetooth スタックの関数 (bt_enable, bt_le_adv_start, bt_gatt_service_register など) を,
 * FFF のモックに置き換えて, 次を確認する.
 *  - ble_init() が, GATT サービスと, 接続のコールバックを登録すること
 *  - ble_advertise() が, 正しいアドバタイズデータとスキャン応答データで, アドバタイズを始めること
 *  - GATT サービスの属性 (UUID, 特性の性質) と, 温度の読み取り (read) のコールバック
 *  - ble_notify_temperature() が, 温度を更新して, 通知 (notify) すること
 *  - 切断のコールバックが, アドバタイズを再開すること
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <errno.h>  /* EINVAL EIO ENOMEM EALREADY ENOTCONN */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* memset memcpy memcmp strlen */

#include "ble.h"
#include "thermo_ble_uuid.h"

DEFINE_FFF_GLOBALS

/** 期待するデバイス名 (CMakeLists.txt で定義) */
#define EXPECTED_NAME "Thermo-Node"
/** アドバタイズデータの数 (フラグ, 名前) */
#define AD_COUNT      2U
/** GATT サービスの属性の数 (サービス, 特性の宣言, 特性の値, CCC) */
#define ATTR_COUNT    4U
/** 特性の値の属性の位置 */
#define ATTR_VALUE    2U
/** テストで使う温度の生値 */
#define TEST_RAW      0x04D2U

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, bt_enable, bt_ready_cb_t)
FAKE_VALUE_FUNC(int, bt_le_adv_start, const struct bt_le_adv_param *, const struct bt_data *,
                size_t, const struct bt_data *, size_t)
FAKE_VALUE_FUNC(int, bt_gatt_service_register, struct bt_gatt_service *)
FAKE_VALUE_FUNC(int, bt_conn_cb_register, struct bt_conn_cb *)
FAKE_VALUE_FUNC(int, bt_gatt_notify_cb, struct bt_conn *, struct bt_gatt_notify_params *)
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_read, struct bt_conn *, const struct bt_gatt_attr *, void *,
                uint16_t, uint16_t, const void *, uint16_t)
/* 次の 4 つは, 属性の定義 (BT_GATT_PRIMARY_SERVICE() など) が参照する (ble.c は, 呼び出さない) */
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_read_service, struct bt_conn *, const struct bt_gatt_attr *,
                void *, uint16_t, uint16_t)
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_read_chrc, struct bt_conn *, const struct bt_gatt_attr *,
                void *, uint16_t, uint16_t)
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_read_ccc, struct bt_conn *, const struct bt_gatt_attr *,
                void *, uint16_t, uint16_t)
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_write_ccc, struct bt_conn *, const struct bt_gatt_attr *,
                const void *, uint16_t, uint16_t, uint8_t)

/** Thermo サービスの UUID (128 bit) */
static const uint8_t service_uuid[] = {THERMO_UUID_SERVICE_VAL};
/** 温度の特性の UUID (128 bit) */
static const uint8_t temperature_uuid[] = {THERMO_UUID_TEMPERATURE_VAL};

/** bt_le_adv_start() の引数 (呼び出しの後は, 引数の指す先が無効になるので, 写しを残す) */
static struct {
    struct bt_le_adv_param param;        /**< アドバタイズパラメータ */
    struct bt_data ad[AD_COUNT];         /**< アドバタイズデータ */
    uint8_t name[sizeof(EXPECTED_NAME)]; /**< 名前の写し (終端付き) */
    uint8_t flags;                       /**< フラグの写し */
    size_t ad_len;                       /**< アドバタイズデータの数 */
    struct bt_data sd;                   /**< スキャン応答データ (1 つ目) */
    uint8_t sd_data[16];                 /**< スキャン応答データの写し (UUID) */
    size_t sd_len;                       /**< スキャン応答データの数 */
} captured;

/** bt_gatt_notify_cb() の引数 (写し) */
static struct {
    const struct bt_gatt_attr *attr;       /**< 通知する属性 */
    uint8_t data[THERMO_TEMPERATURE_SIZE]; /**< 通知するデータの写し */
    uint16_t len;                          /**< 通知するデータの大きさ */
} notified;

/**
 * bt_le_adv_start() のモック動作 (引数の写しを残す)
 *
 * @param[in] param アドバタイズパラメータ
 * @param[in] ad アドバタイズデータ
 * @param[in] ad_len アドバタイズデータの数
 * @param[in] sd スキャン応答データ
 * @param[in] sd_len スキャン応答データの数
 * @return 0
 */
static int capture_adv_start(const struct bt_le_adv_param *param, const struct bt_data *ad,
                             size_t ad_len, const struct bt_data *sd, size_t sd_len)
{
    size_t i = 0U; /* ループ用の添字 */

    (void)memset(&captured, 0, sizeof(captured));
    captured.param = *param;
    captured.ad_len = ad_len;
    captured.sd_len = sd_len;
    for (i = 0U; (i < ad_len) && (i < AD_COUNT); i++) {
        captured.ad[i] = ad[i];
    }
    if ((ad_len >= 1U) && (ad[0].data_len == 1U)) {
        captured.flags = ad[0].data[0];
    }
    if ((ad_len >= 2U) && (ad[1].data_len < sizeof(captured.name))) {
        (void)memcpy(captured.name, ad[1].data, ad[1].data_len);
    }
    if ((sd_len >= 1U) && (sd[0].data_len == sizeof(captured.sd_data))) {
        captured.sd = sd[0];
        (void)memcpy(captured.sd_data, sd[0].data, sizeof(captured.sd_data));
    }
    return 0;
}

/**
 * bt_gatt_notify_cb() のモック動作 (引数の写しを残す)
 *
 * @param[in] conn 使用しない
 * @param[in] params 通知のパラメータ
 * @return bt_gatt_notify_cb_fake.return_val (既定は 0)
 */
static int capture_notify(struct bt_conn *conn, struct bt_gatt_notify_params *params)
{
    ARG_UNUSED(conn);

    notified.attr = params->attr;
    notified.len = params->len;
    if (params->len <= sizeof(notified.data)) {
        (void)memcpy(notified.data, params->data, params->len);
    }
    /* custom_fake があると, return_val は使われないので, ここで返す */
    return bt_gatt_notify_cb_fake.return_val;
}

/**
 * bt_gatt_attr_read() のモック動作 (本物と同じく, offset から, 値をバッファへ写す)
 *
 * @param[in] conn 使用しない
 * @param[in] attr 使用しない
 * @param[out] buf 書き込むバッファ
 * @param[in] buf_len バッファの大きさ
 * @param[in] offset 読み取りの開始位置
 * @param[in] value 値
 * @param[in] value_len 値の大きさ
 * @return 読み取った大きさ (offset が範囲外なら, 負の値)
 */
static ssize_t fake_attr_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                              uint16_t buf_len, uint16_t offset, const void *value,
                              uint16_t value_len)
{
    uint16_t len = 0U; /* 長さ [バイト] */

    ARG_UNUSED(conn);
    ARG_UNUSED(attr);

    if (offset > value_len) {
        return -EINVAL;
    }
    len = (uint16_t)MIN(buf_len, value_len - offset);
    (void)memcpy(buf, (const uint8_t *)value + offset, len);
    return len;
}

/**
 * UUID (128 bit) が, 期待する値と同じか調べる
 *
 * @param[in] uuid UUID
 * @param[in] expected 期待する値 (16 byte)
 * @return 同じなら true
 */
static bool uuid128_equals(const struct bt_uuid *uuid, const uint8_t *expected)
{
    return (uuid->type == BT_UUID_TYPE_128) && (memcmp(BT_UUID_128(uuid)->val, expected, 16U) == 0);
}

/**
 * ble_init() を成功させて, 登録された GATT サービスを返す
 *
 * @return Thermo サービス
 */
static const struct bt_gatt_service *init_and_get_service(void)
{
    zassert_equal(ble_init(), EXIT_SUCCESS);
    zassert_equal(bt_gatt_service_register_fake.call_count, 1U);
    return bt_gatt_service_register_fake.arg0_val;
}

/**
 * 各テストの前に, モックを初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(bt_enable);
    RESET_FAKE(bt_le_adv_start);
    RESET_FAKE(bt_gatt_service_register);
    RESET_FAKE(bt_conn_cb_register);
    RESET_FAKE(bt_gatt_notify_cb);
    RESET_FAKE(bt_gatt_attr_read);
    RESET_FAKE(bt_gatt_attr_read_service);
    RESET_FAKE(bt_gatt_attr_read_chrc);
    RESET_FAKE(bt_gatt_attr_read_ccc);
    RESET_FAKE(bt_gatt_attr_write_ccc);
    FFF_RESET_HISTORY();
    (void)memset(&captured, 0, sizeof(captured));
    (void)memset(&notified, 0, sizeof(notified));
    bt_gatt_attr_read_fake.custom_fake = fake_attr_read;
    bt_gatt_notify_cb_fake.custom_fake = capture_notify;
}

/** ble_init() は, bt_enable(NULL) を呼んで, GATT サービスと, 接続のコールバックを登録する */
ZTEST(ble_node, test_init_success)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */

    /* 期待: bt_enable() (NULL), GATT サービスの登録 (属性の数), 接続のコールバックの登録 */
    zassert_equal(bt_enable_fake.call_count, 1U);
    zassert_is_null(bt_enable_fake.arg0_val);
    zassert_equal(service->attr_count, ATTR_COUNT);
    zassert_equal(bt_conn_cb_register_fake.call_count, 1U);
    zassert_not_null(bt_conn_cb_register_fake.arg0_val->connected);
    zassert_not_null(bt_conn_cb_register_fake.arg0_val->disconnected);
}

/** ble_init() は, bt_enable() のエラーを返して, 何も登録しない */
ZTEST(ble_node, test_init_enable_failure)
{
    bt_enable_fake.return_val = -EIO;

    /* 期待: 有効にできなければ, サービスもコールバックも登録しない */
    zassert_equal(ble_init(), -EIO);
    zassert_equal(bt_enable_fake.call_count, 1U);
    zassert_equal(bt_gatt_service_register_fake.call_count, 0U);
    zassert_equal(bt_conn_cb_register_fake.call_count, 0U);
}

/** ble_init() は, GATT サービスの登録のエラーを返して, 接続のコールバックは登録しない */
ZTEST(ble_node, test_init_service_failure)
{
    bt_gatt_service_register_fake.return_val = -ENOMEM;

    /* 期待: サービスの登録に失敗したら, コールバックは登録しない */
    zassert_equal(ble_init(), -ENOMEM);
    zassert_equal(bt_gatt_service_register_fake.call_count, 1U);
    zassert_equal(bt_conn_cb_register_fake.call_count, 0U);
}

/** ble_init() は, 接続のコールバックの登録のエラーを返す */
ZTEST(ble_node, test_init_callback_failure)
{
    bt_conn_cb_register_fake.return_val = -EALREADY;

    /* 期待: コールバックの登録の失敗を返す */
    zassert_equal(ble_init(), -EALREADY);
    zassert_equal(bt_conn_cb_register_fake.call_count, 1U);
}

/** Thermo サービスは, 温度の特性 (読み取りと通知) と, CCC を持つ */
ZTEST(ble_node, test_service_attributes)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    const struct bt_gatt_attr *attrs = service->attrs;              /* サービスの属性の配列 */
    /* キャラクタリスティックの定義 */
    const struct bt_gatt_chrc *chrc = (const struct bt_gatt_chrc *)attrs[1].user_data;

    /* 0: プライマリサービス (Thermo サービスの UUID) */
    zassert_equal(attrs[0].uuid->type, BT_UUID_TYPE_16);
    zassert_equal(BT_UUID_16(attrs[0].uuid)->val, BT_UUID_GATT_PRIMARY_VAL);
    zassert_true(uuid128_equals((const struct bt_uuid *)attrs[0].user_data, service_uuid));

    /* 1: 特性の宣言 (読み取りと通知. 温度の特性の UUID) */
    zassert_equal(BT_UUID_16(attrs[1].uuid)->val, BT_UUID_GATT_CHRC_VAL);
    zassert_equal(chrc->properties, (BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY));
    zassert_true(uuid128_equals(chrc->uuid, temperature_uuid));

    /* 2: 特性の値 (読み取りだけ許可する) */
    zassert_true(uuid128_equals(attrs[ATTR_VALUE].uuid, temperature_uuid));
    zassert_equal(attrs[ATTR_VALUE].perm, BT_GATT_PERM_READ);
    zassert_not_null(attrs[ATTR_VALUE].read);

    /* 3: CCC (通知の設定) */
    zassert_equal(BT_UUID_16(attrs[3].uuid)->val, BT_UUID_GATT_CCC_VAL);
}

/** 温度の特性の読み取り (read) は, 最後に更新した温度を, 2 byte のリトルエンディアンで返す */
ZTEST(ble_node, test_read_temperature)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    const struct bt_gatt_attr *attr = &service->attrs[ATTR_VALUE];  /* 値の属性 */
    uint8_t buf[THERMO_TEMPERATURE_SIZE] = {0};                     /* 出力バッファ */
    ssize_t len = 0;                                                /* 長さ [バイト] */

    /* 0 に更新したあとは, 0 (温度は, ble.c の static 変数なので, 前のテストの値が残っている) */
    zassert_equal(ble_notify_temperature(0U), EXIT_SUCCESS);
    len = attr->read(NULL, attr, buf, sizeof(buf), 0);
    zassert_equal(len, THERMO_TEMPERATURE_SIZE);
    zassert_equal(buf[0], 0U);
    zassert_equal(buf[1], 0U);

    /* 更新したあと (通知する相手がいなくても, 値は更新される) */
    bt_gatt_notify_cb_fake.return_val = -ENOTCONN;
    zassert_equal(ble_notify_temperature(TEST_RAW), EXIT_SUCCESS);
    len = attr->read(NULL, attr, buf, sizeof(buf), 0);
    zassert_equal(len, THERMO_TEMPERATURE_SIZE);
    zassert_equal(buf[0], (uint8_t)(TEST_RAW & 0xFFU), "low byte");
    zassert_equal(buf[1], (uint8_t)(TEST_RAW >> 8), "high byte");

    /* 範囲外の位置からの読み取りは, エラー */
    zassert_true(attr->read(NULL, attr, buf, sizeof(buf), 3) < 0);
}

/** ble_notify_temperature() は, 特性の値の属性に, 温度 (リトルエンディアン) を通知する */
ZTEST(ble_node, test_notify_success)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */

    /* 期待: 温度の特性の値に, リトルエンディアンの 2 byte で, 通知する */
    zassert_equal(ble_notify_temperature(TEST_RAW), EXIT_SUCCESS);
    zassert_equal(bt_gatt_notify_cb_fake.call_count, 1U);
    zassert_equal(notified.attr, &service->attrs[ATTR_VALUE]);
    zassert_equal(notified.len, THERMO_TEMPERATURE_SIZE);
    zassert_equal(notified.data[0], (uint8_t)(TEST_RAW & 0xFFU));
    zassert_equal(notified.data[1], (uint8_t)(TEST_RAW >> 8));
}

/** 接続しているゲートウェイがいなくても (ENOTCONN), 成功を返す */
ZTEST(ble_node, test_notify_not_connected)
{
    (void)init_and_get_service();
    bt_gatt_notify_cb_fake.return_val = -ENOTCONN;

    /* 期待: 接続している相手がいなければ (-ENOTCONN), 成功として扱う */
    zassert_equal(ble_notify_temperature(TEST_RAW), EXIT_SUCCESS);
    zassert_equal(bt_gatt_notify_cb_fake.call_count, 1U);
}

/** 通知のそのほかのエラーは, そのまま返す */
ZTEST(ble_node, test_notify_failure)
{
    (void)init_and_get_service();
    bt_gatt_notify_cb_fake.return_val = -EIO;

    /* 期待: ほかの失敗は, そのエラーを返す */
    zassert_equal(ble_notify_temperature(TEST_RAW), -EIO);
}

/** ble_advertise() は, 接続可能なアドバタイズを, フラグと名前のデータ付きで始める */
ZTEST(ble_node, test_advertise_success)
{
    bt_le_adv_start_fake.custom_fake = capture_adv_start;

    zassert_equal(ble_advertise(), EXIT_SUCCESS);
    zassert_equal(bt_le_adv_start_fake.call_count, 1U);

    /* 接続可能 (BT_LE_ADV_CONN_FAST_1) */
    zassert_true((captured.param.options & BT_LE_ADV_OPT_CONN) != 0U);
    zassert_equal(captured.param.interval_min, BT_GAP_ADV_FAST_INT_MIN_1);
    zassert_equal(captured.param.interval_max, BT_GAP_ADV_FAST_INT_MAX_1);

    /* アドバタイズデータ: フラグ, 完全なデバイス名 */
    zassert_equal(captured.ad_len, AD_COUNT);
    zassert_equal(captured.ad[0].type, BT_DATA_FLAGS);
    zassert_equal(captured.flags, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR));
    zassert_equal(captured.ad[1].type, BT_DATA_NAME_COMPLETE);
    zassert_equal(captured.ad[1].data_len, strlen(EXPECTED_NAME));
    zassert_mem_equal(captured.name, EXPECTED_NAME, strlen(EXPECTED_NAME));

    /* スキャン応答データ: Thermo サービスの UUID (128 bit) */
    zassert_equal(captured.sd_len, 1U);
    zassert_equal(captured.sd.type, BT_DATA_UUID128_ALL);
    zassert_mem_equal(captured.sd_data, service_uuid, sizeof(service_uuid));
}

/** ble_advertise() は, bt_le_adv_start() のエラーを, そのまま返す */
ZTEST(ble_node, test_advertise_failure)
{
    bt_le_adv_start_fake.return_val = -ENOMEM;

    /* 期待: アドバタイズを始められなければ, そのエラーを返す */
    zassert_equal(ble_advertise(), -ENOMEM);
    zassert_equal(bt_le_adv_start_fake.call_count, 1U);
}

/** 接続のコールバック: 接続しても, アドバタイズは再開しない */
ZTEST(ble_node, test_connected_callback)
{
    struct bt_conn_cb *cb = NULL; /* コールバックの登録情報 */

    zassert_equal(ble_init(), EXIT_SUCCESS);
    cb = bt_conn_cb_register_fake.arg0_val;

    cb->connected(NULL, 0U);
    cb->connected(NULL, 0x3EU); /* 接続に失敗 (ログを出すだけ) */
    zassert_equal(bt_le_adv_start_fake.call_count, 0U);
}

/** 切断のコールバック: 接続が切れたら, アドバタイズを再開する */
ZTEST(ble_node, test_disconnected_restarts_advertising)
{
    struct bt_conn_cb *cb = NULL; /* コールバックの登録情報 */

    zassert_equal(ble_init(), EXIT_SUCCESS);
    cb = bt_conn_cb_register_fake.arg0_val;

    cb->disconnected(NULL, 0x13U);
    zassert_equal(bt_le_adv_start_fake.call_count, 1U);

    /* アドバタイズの再開に失敗しても, ログを出すだけ */
    bt_le_adv_start_fake.return_val = -ENOMEM;
    cb->disconnected(NULL, 0x13U);
    zassert_equal(bt_le_adv_start_fake.call_count, 2U);
}

ZTEST_SUITE(ble_node, NULL, NULL, before, NULL, NULL);
