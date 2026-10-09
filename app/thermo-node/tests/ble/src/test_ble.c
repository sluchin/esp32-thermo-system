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
 *  - ble_init() が GATT サービスと, 接続のコールバックを登録すること
 *  - ble_advertise() が正しいアドバタイズデータとスキャン応答データで, アドバタイズを始めること
 *  - GATT サービスの属性 (UUID, 特性の性質) と, 温度の読み取り (read) のコールバック
 *  - ble_notify_temperature() が温度を更新して, 通知 (notify) すること
 *  - 切断のコールバックがアドバタイズを再開すること
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
#define EXPECTED_NAME     "Thermo-Node"
/** アドバタイズデータの数 (フラグ, 名前) */
#define AD_COUNT          2U
/** GATT サービスの属性の数 (サービス, 特性の宣言, 特性の値, CCC, 時刻の特性の宣言, 時刻の特性の値)
 */
#define ATTR_COUNT        6U
/** 時刻の特性の宣言の属性の位置 */
#define ATTR_TIME_DECL    4U
/** 時刻の特性の値の属性の位置 */
#define ATTR_TIME_VALUE   5U
/** 特性の値の属性の位置 */
#define ATTR_VALUE        2U
/** テストで使う温度 [℃ の 10 倍] (-3.5 ℃. 下位 byte が 0xDD, 上位 byte が 0xFF) */
#define TEST_TEMP_X10     (-35)
/** テストで使う湿度 [% の 10 倍] (45.0 %. 下位 byte が 0xC2, 上位 byte が 0x01) */
#define TEST_HUMIDITY_X10 450U

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, bt_enable, bt_ready_cb_t)
FAKE_VALUE_FUNC(int, bt_le_adv_start, const struct bt_le_adv_param *, const struct bt_data *,
                size_t, const struct bt_data *, size_t)
FAKE_VALUE_FUNC(int, bt_gatt_service_register, struct bt_gatt_service *)
FAKE_VALUE_FUNC(int, bt_conn_cb_register, struct bt_conn_cb *)
FAKE_VALUE_FUNC(int, bt_gatt_notify_cb, struct bt_conn *, struct bt_gatt_notify_params *)
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_read, struct bt_conn *, const struct bt_gatt_attr *, void *,
                uint16_t, uint16_t, const void *, uint16_t)
/* 次の 4 つは属性の定義 (BT_GATT_PRIMARY_SERVICE() など) が参照する (ble.c は呼び出さない) */
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
/** 時刻の特性の UUID (128 bit) */
static const uint8_t time_uuid[] = {THERMO_UUID_TIME_VAL};

/** bt_le_adv_start() の引数 (呼び出しの後は引数の指す先が無効になるので, 写しを残す) */
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
 * UUID (128 bit) が期待する値と同じか調べる
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

/** 時刻のコールバックが受け取った値 */
static struct {
    unsigned int count; /**< 呼ばれた回数 */
    int64_t unix_s;     /**< 受け取った UTC の UNIX 時刻 [s] */
    int ret;            /**< コールバックが返す値 */
} time_received;

/**
 * 時刻のコールバック (受け取った時刻を記録して, 設定した値を返す)
 *
 * @param[in] unix_s UTC の UNIX 時刻 [s]
 * @return time_received.ret
 */
static int on_time(int64_t unix_s)
{
    time_received.count++;
    time_received.unix_s = unix_s;

    return time_received.ret;
}

/**
 * 時刻の特性に, 値を書き込む (write)
 *
 * @param[in] service ble_init() で登録されたサービス
 * @param[in] value   書き込む値
 * @param[in] len     値の大きさ
 * @param[in] offset  書き込みの開始位置
 * @return 書き込んだ大きさ (失敗なら, 負の ATT エラー)
 */
static ssize_t write_time_attr(const struct bt_gatt_service *service, const uint8_t *value,
                               uint16_t len, uint16_t offset)
{
    const struct bt_gatt_attr *attr = &service->attrs[ATTR_TIME_VALUE]; /* 時刻の値の属性 */

    return attr->write(NULL, attr, value, len, offset, 0U);
}

/**
 * 各テストの前にモックを初期状態に戻す
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
    ble_set_time_callback(NULL);
    (void)memset(&time_received, 0, sizeof(time_received));
}

/** ble_init() は bt_enable(NULL) を呼んで GATT サービスと, 接続のコールバックを登録する */
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

/** ble_init() は bt_enable() のエラーを返して, 何も登録しない */
ZTEST(ble_node, test_init_enable_failure)
{
    bt_enable_fake.return_val = -EIO;

    /* 期待: 有効にできなければ, サービスもコールバックも登録しない */
    zassert_equal(ble_init(), -EIO);
    zassert_equal(bt_enable_fake.call_count, 1U);
    zassert_equal(bt_gatt_service_register_fake.call_count, 0U);
    zassert_equal(bt_conn_cb_register_fake.call_count, 0U);
}

/** ble_init() は GATT サービスの登録のエラーを返して, 接続のコールバックは登録しない */
ZTEST(ble_node, test_init_service_failure)
{
    bt_gatt_service_register_fake.return_val = -ENOMEM;

    /* 期待: サービスの登録に失敗したら, コールバックは登録しない */
    zassert_equal(ble_init(), -ENOMEM);
    zassert_equal(bt_gatt_service_register_fake.call_count, 1U);
    zassert_equal(bt_conn_cb_register_fake.call_count, 0U);
}

/** ble_init() は接続のコールバックの登録のエラーを返す */
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

/** 温度の特性の読み取り (read) は最後に更新した温度と湿度を, 2 byte ずつのリトルエンディアンで返す
 */
ZTEST(ble_node, test_read_temperature)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    const struct bt_gatt_attr *attr = &service->attrs[ATTR_VALUE];  /* 値の属性 */
    uint8_t buf[THERMO_TEMPERATURE_SIZE] = {0};                     /* 出力バッファ */
    ssize_t len = 0;                                                /* 長さ [バイト] */

    /* 0 に更新したあとは, 0 (値は ble.c の static 変数なので, 前のテストの値が残っている) */
    zassert_equal(ble_notify_temperature(0, 0U), EXIT_SUCCESS);
    len = attr->read(NULL, attr, buf, sizeof(buf), 0);
    zassert_equal(len, THERMO_TEMPERATURE_SIZE);
    zassert_equal(buf[0], 0U);
    zassert_equal(buf[1], 0U);
    zassert_equal(buf[2], 0U);
    zassert_equal(buf[3], 0U);

    /* 更新したあと (通知する相手がいなくても値は更新される). 負の温度は 2 の補数 */
    bt_gatt_notify_cb_fake.return_val = -ENOTCONN;
    zassert_equal(ble_notify_temperature(TEST_TEMP_X10, TEST_HUMIDITY_X10), EXIT_SUCCESS);
    len = attr->read(NULL, attr, buf, sizeof(buf), 0);
    zassert_equal(len, THERMO_TEMPERATURE_SIZE);
    zassert_equal(buf[0], 0xDDU, "temperature low byte");
    zassert_equal(buf[1], 0xFFU, "temperature high byte");
    zassert_equal(buf[2], 0xC2U, "humidity low byte");
    zassert_equal(buf[3], 0x01U, "humidity high byte");

    /* 湿度がないときは, 湿度が 0xFFFF */
    zassert_equal(ble_notify_temperature(TEST_TEMP_X10, THERMO_HUMIDITY_NONE), EXIT_SUCCESS);
    len = attr->read(NULL, attr, buf, sizeof(buf), 0);
    zassert_equal(len, THERMO_TEMPERATURE_SIZE);
    zassert_equal(buf[2], 0xFFU);
    zassert_equal(buf[3], 0xFFU);

    /* 範囲外の位置からの読み取りは, エラー */
    zassert_true(attr->read(NULL, attr, buf, sizeof(buf), 5) < 0);
}

/** ble_notify_temperature() は特性の値の属性に温度と湿度 (リトルエンディアン) を通知する */
ZTEST(ble_node, test_notify_success)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */

    /* 期待: 温度の特性の値に, 温度と湿度を, リトルエンディアンの 4 byte で, 通知する */
    zassert_equal(ble_notify_temperature(TEST_TEMP_X10, TEST_HUMIDITY_X10), EXIT_SUCCESS);
    zassert_equal(bt_gatt_notify_cb_fake.call_count, 1U);
    zassert_equal(notified.attr, &service->attrs[ATTR_VALUE]);
    zassert_equal(notified.len, THERMO_TEMPERATURE_SIZE);
    zassert_equal(notified.data[0], 0xDDU);
    zassert_equal(notified.data[1], 0xFFU);
    zassert_equal(notified.data[2], 0xC2U);
    zassert_equal(notified.data[3], 0x01U);
}

/** 接続しているゲートウェイがいなくても (ENOTCONN), 成功を返す */
ZTEST(ble_node, test_notify_not_connected)
{
    (void)init_and_get_service();
    bt_gatt_notify_cb_fake.return_val = -ENOTCONN;

    /* 期待: 接続している相手がいなければ (-ENOTCONN), 成功として扱う */
    zassert_equal(ble_notify_temperature(TEST_TEMP_X10, TEST_HUMIDITY_X10), EXIT_SUCCESS);
    zassert_equal(bt_gatt_notify_cb_fake.call_count, 1U);
}

/** 通知のそのほかのエラーは, そのまま返す */
ZTEST(ble_node, test_notify_failure)
{
    (void)init_and_get_service();
    bt_gatt_notify_cb_fake.return_val = -EIO;

    /* 期待: ほかの失敗はそのエラーを返す */
    zassert_equal(ble_notify_temperature(TEST_TEMP_X10, TEST_HUMIDITY_X10), -EIO);
}

/** ble_advertise() は接続可能なアドバタイズを, フラグと名前のデータ付きで始める */
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

/** ble_advertise() は bt_le_adv_start() のエラーをそのまま返す */
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

/** 切断のコールバックでは, アドバタイズを始めない (接続のオブジェクトが, まだ解放されていないため)
 */
ZTEST(ble_node, test_disconnected_does_not_advertise)
{
    struct bt_conn_cb *cb = NULL; /* コールバックの登録情報 */

    zassert_equal(ble_init(), EXIT_SUCCESS);
    cb = bt_conn_cb_register_fake.arg0_val;

    cb->disconnected(NULL, 0x08U);
    zassert_equal(bt_le_adv_start_fake.call_count, 0U);
}

/** 接続のオブジェクトが解放されたら, アドバタイズを再開する (失敗しても, ログを出すだけ) */
ZTEST(ble_node, test_recycled_restarts_advertising)
{
    struct bt_conn_cb *cb = NULL; /* コールバックの登録情報 */

    zassert_equal(ble_init(), EXIT_SUCCESS);
    cb = bt_conn_cb_register_fake.arg0_val;
    zassert_not_null(cb->recycled);

    cb->recycled();
    zassert_equal(bt_le_adv_start_fake.call_count, 1U);

    /* アドバタイズの再開に失敗しても, ログを出すだけ */
    bt_le_adv_start_fake.return_val = -ENOMEM;
    cb->recycled();
    zassert_equal(bt_le_adv_start_fake.call_count, 2U);

    /* すでにアドバタイズしているとき (EALREADY) は, エラーにしない */
    bt_le_adv_start_fake.return_val = -EALREADY;
    cb->recycled();
    zassert_equal(bt_le_adv_start_fake.call_count, 3U);
}

/** 時刻の特性は, 書き込み (write) だけを許可する (読み取りと通知は, できない) */
ZTEST(ble_node, test_time_attributes)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    const struct bt_gatt_attr *attrs = service->attrs;              /* サービスの属性の配列 */
    /* キャラクタリスティックの定義 */
    const struct bt_gatt_chrc *chrc = (const struct bt_gatt_chrc *)attrs[ATTR_TIME_DECL].user_data;

    zassert_equal(service->attr_count, ATTR_COUNT);
    zassert_equal(BT_UUID_16(attrs[ATTR_TIME_DECL].uuid)->val, BT_UUID_GATT_CHRC_VAL);
    zassert_equal(chrc->properties, BT_GATT_CHRC_WRITE);
    zassert_true(uuid128_equals(chrc->uuid, time_uuid));
    zassert_true(uuid128_equals(attrs[ATTR_TIME_VALUE].uuid, time_uuid));
    zassert_equal(attrs[ATTR_TIME_VALUE].perm, BT_GATT_PERM_WRITE);
    zassert_is_null(attrs[ATTR_TIME_VALUE].read);
    zassert_not_null(attrs[ATTR_TIME_VALUE].write);
}

/** 時刻の書き込みは, uint32 のリトルエンディアンの UTC の UNIX 時刻を, コールバックに渡す */
ZTEST(ble_node, test_write_time_success)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    /* 2026-10-08 05:00:00 UTC (1791435600 = 0x6AC72350) */
    const uint8_t value[THERMO_TIME_SIZE] = {0x50U, 0x23U, 0xC7U, 0x6AU};

    ble_set_time_callback(on_time);
    time_received.ret = EXIT_SUCCESS;

    /* 期待: 4 byte 書き込めて, コールバックが, 時刻を受け取る */
    zassert_equal(write_time_attr(service, value, sizeof(value), 0U), THERMO_TIME_SIZE);
    zassert_equal(time_received.count, 1U);
    zassert_equal(time_received.unix_s, 1791435600LL);
}

/** コールバックが登録されていなければ, 時刻の書き込みを拒否する */
ZTEST(ble_node, test_write_time_without_callback)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    const uint8_t value[THERMO_TIME_SIZE] = {0x50U, 0x23U, 0xC7U, 0x6AU};

    zassert_equal(write_time_attr(service, value, sizeof(value), 0U),
                  BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED));
}

/** 大きさが 4 byte でない書き込みは, 拒否して, コールバックを呼ばない */
ZTEST(ble_node, test_write_time_bad_length)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    const uint8_t value[THERMO_TIME_SIZE + 1U] = {0};

    ble_set_time_callback(on_time);

    zassert_equal(write_time_attr(service, value, THERMO_TIME_SIZE - 1U, 0U),
                  BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN));
    zassert_equal(write_time_attr(service, value, THERMO_TIME_SIZE + 1U, 0U),
                  BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN));
    zassert_equal(time_received.count, 0U);
}

/** 先頭でない位置からの書き込みは, 拒否して, コールバックを呼ばない */
ZTEST(ble_node, test_write_time_bad_offset)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    const uint8_t value[THERMO_TIME_SIZE] = {0};

    ble_set_time_callback(on_time);

    zassert_equal(write_time_attr(service, value, sizeof(value), 1U),
                  BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
    zassert_equal(time_received.count, 0U);
}

/** 設定できない範囲の時刻 (-ERANGE) は, 値が許されないというエラーで拒否する */
ZTEST(ble_node, test_write_time_out_of_range)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    const uint8_t value[THERMO_TIME_SIZE] = {0};

    ble_set_time_callback(on_time);
    time_received.ret = -ERANGE;

    zassert_equal(write_time_attr(service, value, sizeof(value), 0U),
                  BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED));
    zassert_equal(time_received.count, 1U);
}

/** 時刻を設定できなかったとき (RTC の通信の失敗など) は, 原因不明のエラーで拒否する */
ZTEST(ble_node, test_write_time_failure)
{
    const struct bt_gatt_service *service = init_and_get_service(); /* サービス */
    const uint8_t value[THERMO_TIME_SIZE] = {0};

    ble_set_time_callback(on_time);
    time_received.ret = -EIO;

    zassert_equal(write_time_attr(service, value, sizeof(value), 0U),
                  BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
}

ZTEST_SUITE(ble_node, NULL, NULL, before, NULL, NULL);
