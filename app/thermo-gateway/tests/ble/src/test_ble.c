/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief ble.c の単体テスト
 *
 * Bluetooth スタックの関数 (bt_le_scan_start, bt_conn_le_create, bt_gatt_discover など) を,
 * FFF のモックに置き換えて, 次を確認する.
 *  - ble_init() が接続のコールバックを登録すること
 *  - ble_scan() が正しいパラメータでスキャンを始めること
 *  - スキャンで Thermo サービスを持つノードだけに接続すること (台数の上限, 重複の無視を含む)
 *  - 接続したあと, サービス, 温度の特性, CCC を順に探索して, 通知を購読すること
 *  - 通知された温度 (リトルエンディアン) をコールバックに渡すこと
 *  - 切断や失敗のときに状態を戻して, スキャンを再開すること
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <errno.h>   /* EIO EALREADY ENOMEM EBUSY */
#include <stdbool.h> /* bool */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* memcpy memset memcmp */

#include "ble.h"
#include "thermo_ble_uuid.h"

DEFINE_FFF_GLOBALS

/** 期待するスキャン間隔 [ms] (ble.c の SCAN_INTERVAL_MS) */
#define EXPECTED_INTERVAL_MS 100
/** 期待するスキャンウィンドウ [ms] (ble.c の SCAN_WINDOW_MS) */
#define EXPECTED_WINDOW_MS   50
/** テストで使う受信信号強度 [dBm] */
#define TEST_RSSI            (-60)
/** 接続できるノードの数 (CMakeLists.txt の CONFIG_BT_MAX_CONN) */
#define MAX_NODES            CONFIG_BT_MAX_CONN
/** 使うダミーの接続の数 */
#define CONN_COUNT           4U
/** ワークキューが接続を始める処理を終えるまでの待ち時間 [ms] */
#define WORK_WAIT_MS         10
/** ble.c の CONNECT_RETRY_MS (接続を始め直すまでの間隔 [ms]) */
#define CONNECT_RETRY_MS     100
/** ble.c の CONNECT_RETRY_MAX (接続を始め直す最大の回数) */
#define CONNECT_RETRY_MAX    10U
/** テストで使う温度の生値 */
#define TEST_RAW             0x0ABCU

/**
 * アドバタイズデータの要素を解析する関数の型
 * (bt_data_parse() の引数. FFF の引数には, 関数ポインタの型が必要)
 */
typedef bool (*data_cb_t)(struct bt_data *data, void *user_data);

/**
 * スキャンのコールバックの関数ポインタ型
 * (bt_le_scan_cb_t は, ポインタではなく, 関数型の typedef なので, FFF の引数には使えない.
 *  関数の引数に書いた関数型は, 関数ポインタとして扱われるので, 同じ型になる)
 */
typedef bt_le_scan_cb_t *scan_cb_ptr_t;

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, bt_enable, bt_ready_cb_t)
FAKE_VALUE_FUNC(int, bt_conn_cb_register, struct bt_conn_cb *)
FAKE_VALUE_FUNC(int, bt_le_scan_start, const struct bt_le_scan_param *, scan_cb_ptr_t)
FAKE_VALUE_FUNC(int, bt_le_scan_stop)
FAKE_VALUE_FUNC(int, bt_conn_le_create, const bt_addr_le_t *,
                const struct bt_conn_le_create_param *, const struct bt_le_conn_param *,
                struct bt_conn **)
FAKE_VALUE_FUNC(struct bt_conn *, bt_conn_lookup_addr_le, uint8_t, const bt_addr_le_t *)
FAKE_VALUE_FUNC(struct bt_conn *, bt_conn_ref, struct bt_conn *)
FAKE_VOID_FUNC(bt_conn_unref, struct bt_conn *)
FAKE_VALUE_FUNC(int, bt_conn_disconnect, struct bt_conn *, uint8_t)
FAKE_VALUE_FUNC(const bt_addr_le_t *, bt_conn_get_dst, const struct bt_conn *)
FAKE_VALUE_FUNC(int, bt_gatt_discover, struct bt_conn *, struct bt_gatt_discover_params *)
FAKE_VALUE_FUNC(int, bt_gatt_subscribe, struct bt_conn *, struct bt_gatt_subscribe_params *)
FAKE_VALUE_FUNC(uint16_t, bt_gatt_attr_value_handle, const struct bt_gatt_attr *)
FAKE_VOID_FUNC(bt_data_parse, struct net_buf_simple *, data_cb_t, void *)

/** Thermo サービスの UUID (128 bit) */
static const uint8_t service_uuid[] = {THERMO_UUID_SERVICE_VAL};
/** 温度の特性の UUID (128 bit) */
static const uint8_t temperature_uuid[] = {THERMO_UUID_TEMPERATURE_VAL};

/** ノードのアドレス (A, B, C) */
static const bt_addr_le_t node_addr[] = {
    {.type = BT_ADDR_LE_RANDOM, .a = {.val = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}}},
    {.type = BT_ADDR_LE_RANDOM, .a = {.val = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16}}},
    {.type = BT_ADDR_LE_RANDOM, .a = {.val = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26}}},
};

/** ダミーの接続 (struct bt_conn の中身は, ここでは使わない. アドレスで見分ける) */
static char conn_storage[CONN_COUNT];

/** 次に bt_conn_le_create() が返す, ダミーの接続の番号 */
static unsigned int next_conn;

/** 接続のコールバック (ble_init() が登録したもの) */
static struct bt_conn_cb *conn_cb;

/** 温度のコールバックに渡された値 */
static struct {
    unsigned int count; /**< 呼ばれた回数 */
    bt_addr_le_t addr;  /**< アドレス */
    uint16_t raw;       /**< 温度 */
} received;

/** bt_le_scan_start() に渡されたスキャンパラメータの写し (引数は呼び出しの後は, 無効になる) */
static struct bt_le_scan_param captured_scan_param;

/** スキャンのコールバックに渡す, アドバタイズデータ (スキャン応答) */
static uint8_t adv_bytes[40];
/** adv_bytes を指す, アドバタイズデータ */
static struct net_buf_simple adv;

/**
 * ダミーの接続を返す
 *
 * @param[in] index 番号 (0 .. CONN_COUNT - 1)
 * @return ダミーの接続
 */
static struct bt_conn *conn_of(unsigned int index)
{
    return (struct bt_conn *)(void *)&conn_storage[index];
}

/**
 * bt_data_parse() のモック動作 (本物と同じに要素ごとにコールバックを呼ぶ)
 *
 * @param[in] ad アドバタイズデータ (長さ, 種別, データ の繰り返し)
 * @param[in] func 要素ごとに呼ぶコールバック (false を返したら, 終了する)
 * @param[in,out] user_data コールバックに渡す
 */
static void fake_data_parse(struct net_buf_simple *ad, data_cb_t func, void *user_data)
{
    size_t pos = 0U; /* 書き込み位置 */

    while (pos < ad->len) {
        struct bt_data data;
        uint8_t len = ad->data[pos];

        if ((len == 0U) || ((pos + len + 1U) > ad->len)) {
            return;
        }
        data.type = ad->data[pos + 1U];
        data.data_len = (uint8_t)(len - 1U);
        data.data = &ad->data[pos + 2U];
        if (!func(&data, user_data)) {
            return;
        }
        pos += (size_t)len + 1U;
    }
}

/**
 * bt_conn_le_create() のモック動作 (ダミーの接続を順に返す)
 *
 * @param[in] peer 使用しない
 * @param[in] create_param 使用しない
 * @param[in] conn_param 使用しない
 * @param[out] conn 接続
 * @return 0
 */
static int fake_conn_le_create(const bt_addr_le_t *peer,
                               const struct bt_conn_le_create_param *create_param,
                               const struct bt_le_conn_param *conn_param, struct bt_conn **conn)
{
    ARG_UNUSED(peer);
    ARG_UNUSED(create_param);
    ARG_UNUSED(conn_param);

    *conn = conn_of(next_conn);
    next_conn++;

    return 0;
}

/**
 * bt_conn_ref() のモック動作 (引数の接続をそのまま返す)
 *
 * @param[in] conn ダミーの接続
 * @return conn
 */
static struct bt_conn *fake_conn_ref(struct bt_conn *conn)
{
    return conn;
}

/**
 * bt_conn_le_create() のモック動作 (bt_conn_le_create() が戻る前に接続が完了する場合)
 *
 * @param[in] peer 使用しない
 * @param[in] create_param 使用しない
 * @param[in] conn_param 使用しない
 * @param[out] conn 接続
 * @return 0
 */
static int fake_conn_le_create_connected_early(const bt_addr_le_t *peer,
                                               const struct bt_conn_le_create_param *create_param,
                                               const struct bt_le_conn_param *conn_param,
                                               struct bt_conn **conn)
{
    struct bt_conn *created = conn_of(next_conn); /* 作成した接続 */

    conn_cb->connected(created, 0U);

    return fake_conn_le_create(peer, create_param, conn_param, conn);
}

/**
 * bt_le_scan_start() のモック動作 (スキャンパラメータの写しを残す)
 *
 * @param[in] param スキャンパラメータ
 * @param[in] cb 使用しない
 * @return 0
 */
static int capture_scan_start(const struct bt_le_scan_param *param, scan_cb_ptr_t cb)
{
    ARG_UNUSED(cb);

    captured_scan_param = *param;

    return 0;
}

/**
 * bt_conn_get_dst() のモック動作 (接続の番号に対応する, ノードのアドレスを返す)
 *
 * @param[in] conn ダミーの接続
 * @return ノードのアドレス
 */
static const bt_addr_le_t *fake_get_dst(const struct bt_conn *conn)
{
    return &node_addr[(const char *)conn - conn_storage];
}

/**
 * 温度のコールバック (受け取った値を残す)
 *
 * @param[in] addr ノードのアドレス
 * @param[in] raw 温度
 */
static void on_temperature(const bt_addr_le_t *addr, uint16_t raw)
{
    received.count++;
    received.addr = *addr;
    received.raw = raw;
}

/**
 * Thermo サービスの UUID を持つ, アドバタイズデータにする
 */
static void set_adv_with_service(void)
{
    adv_bytes[0] = (uint8_t)(1U + sizeof(service_uuid));
    adv_bytes[1] = BT_DATA_UUID128_ALL;
    (void)memcpy(&adv_bytes[2], service_uuid, sizeof(service_uuid));
    adv.data = adv_bytes;
    adv.len = (uint16_t)(2U + sizeof(service_uuid));
}

/** SwitchBot のコールバックに渡された値の記録 */
static struct {
    unsigned int count;     /**< 呼ばれた回数 */
    bt_addr_le_t addr;      /**< 機器のアドレス */
    struct switchbot_ad ad; /**< 解析した結果 */
} switchbot_received;

/**
 * SwitchBot のコールバック (受け取った値を残す)
 *
 * @param[in] addr 機器のアドレス
 * @param[in] ad 解析した結果
 */
static void on_switchbot(const bt_addr_le_t *addr, const struct switchbot_ad *ad)
{
    switchbot_received.count++;
    switchbot_received.addr = *addr;
    switchbot_received.ad = *ad;
}

/**
 * アドバタイズデータを 1 要素 (長さ, 種類, 中身) のデータにする
 *
 * @param[in] type AD の種類
 * @param[in] payload AD の中身
 * @param[in] len payload の長さ
 */
static void set_adv_element(uint8_t type, const uint8_t *payload, size_t len)
{
    adv_bytes[0] = (uint8_t)(1U + len);
    adv_bytes[1] = type;
    (void)memcpy(&adv_bytes[2], payload, len);
    adv.data = adv_bytes;
    adv.len = (uint16_t)(2U + len);
}

/**
 * Thermo サービスの UUID を持たない (名前だけの), アドバタイズデータにする
 */
static void set_adv_without_service(void)
{
    adv_bytes[0] = 5U;
    adv_bytes[1] = BT_DATA_NAME_COMPLETE;
    (void)memcpy(&adv_bytes[2], "Abcd", 4U);
    adv.data = adv_bytes;
    adv.len = 6U;
}

/**
 * ble_init() と ble_scan() を呼んでスキャンを始めた状態にする
 *
 * @return スキャンのコールバック
 */
static scan_cb_ptr_t start_scanning(void)
{
    zassert_equal(ble_init(), EXIT_SUCCESS);
    conn_cb = bt_conn_cb_register_fake.arg0_val;
    ble_set_temperature_callback(on_temperature);
    zassert_equal(ble_scan(), EXIT_SUCCESS);

    return bt_le_scan_start_fake.arg1_val;
}

/**
 * ノード (アドレス index) を見つけて, 接続を始めさせる
 *
 * @param[in] scan_cb スキャンのコールバック
 * @param[in] index ノードのアドレスの番号
 */
static void find_node(scan_cb_ptr_t scan_cb, unsigned int index)
{
    set_adv_with_service();
    scan_cb(&node_addr[index], TEST_RSSI, BT_GAP_ADV_TYPE_SCAN_RSP, &adv);
    k_msleep(WORK_WAIT_MS); /* 接続はワークキューで始まる */
}

/**
 * 各テストの前にモックと状態を, 初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    RESET_FAKE(bt_enable);
    RESET_FAKE(bt_conn_cb_register);
    RESET_FAKE(bt_le_scan_start);
    RESET_FAKE(bt_le_scan_stop);
    RESET_FAKE(bt_conn_le_create);
    RESET_FAKE(bt_conn_lookup_addr_le);
    RESET_FAKE(bt_conn_ref);
    RESET_FAKE(bt_conn_unref);
    RESET_FAKE(bt_conn_disconnect);
    RESET_FAKE(bt_conn_get_dst);
    RESET_FAKE(bt_gatt_discover);
    RESET_FAKE(bt_gatt_subscribe);
    RESET_FAKE(bt_gatt_attr_value_handle);
    RESET_FAKE(bt_data_parse);
    FFF_RESET_HISTORY();
    bt_data_parse_fake.custom_fake = fake_data_parse;
    bt_conn_le_create_fake.custom_fake = fake_conn_le_create;
    bt_conn_get_dst_fake.custom_fake = fake_get_dst;
    bt_conn_ref_fake.custom_fake = fake_conn_ref;
    next_conn = 0U;
    (void)memset(&received, 0, sizeof(received));
    (void)memset(&switchbot_received, 0, sizeof(switchbot_received));
    ble_set_temperature_callback(NULL);
    ble_set_switchbot_callback(NULL);
}

/**
 * 各テストの後に ble.c の static な状態 (接続しているノード) を空に戻す
 *
 * ble.c はノードの状態を static 変数に持つので, 全てのダミーの接続を切断して, 解放する.
 *
 * @param[in] fixture 使用しない
 */
static void after(void *fixture)
{
    unsigned int i = 0U; /* ループ用の添字 */

    ARG_UNUSED(fixture);

    if (conn_cb != NULL) {
        for (i = 0U; i < CONN_COUNT; i++) {
            conn_cb->disconnected(conn_of(i), 0U);
        }
    }
    conn_cb = NULL;
}

/** ble_init() は bt_enable(NULL) を呼んで接続のコールバックを登録する */
ZTEST(ble_gateway, test_init_success)
{
    /* 期待: bt_enable() を同期 (NULL) で呼んで接続のコールバックを登録する */
    zassert_equal(ble_init(), EXIT_SUCCESS);
    conn_cb = bt_conn_cb_register_fake.arg0_val;

    zassert_equal(bt_enable_fake.call_count, 1U);
    zassert_is_null(bt_enable_fake.arg0_val);
    zassert_equal(bt_conn_cb_register_fake.call_count, 1U);
    zassert_not_null(conn_cb->connected);
    zassert_not_null(conn_cb->disconnected);
}

/** ble_init() は bt_enable() のエラーを返して, コールバックは登録しない */
ZTEST(ble_gateway, test_init_enable_failure)
{
    bt_enable_fake.return_val = -EIO;

    /* 期待: Bluetooth を有効にできなければ, そのエラーを返して, コールバックは登録しない */
    zassert_equal(ble_init(), -EIO);
    zassert_equal(bt_conn_cb_register_fake.call_count, 0U);
}

/** ble_init() は接続のコールバックの登録のエラーを返す */
ZTEST(ble_gateway, test_init_callback_failure)
{
    bt_conn_cb_register_fake.return_val = -EALREADY;

    /* 期待: コールバックの登録に失敗したら, そのエラーを返す */
    zassert_equal(ble_init(), -EALREADY);
    zassert_equal(bt_conn_cb_register_fake.call_count, 1U);
}

/** ble_scan() はアクティブスキャンを, 決められた間隔とウィンドウで始める */
ZTEST(ble_gateway, test_scan_success)
{
    bt_le_scan_start_fake.custom_fake = capture_scan_start;

    zassert_equal(ble_scan(), EXIT_SUCCESS);
    zassert_equal(bt_le_scan_start_fake.call_count, 1U);

    /* アクティブスキャン (スキャン応答を受け取る). ウィンドウは間隔を超えない */
    zassert_equal(captured_scan_param.type, BT_LE_SCAN_TYPE_ACTIVE);
    zassert_equal(captured_scan_param.options, BT_LE_SCAN_OPT_NONE);
    zassert_equal(captured_scan_param.interval, BT_GAP_MS_TO_SCAN_INTERVAL(EXPECTED_INTERVAL_MS));
    zassert_equal(captured_scan_param.window, BT_GAP_MS_TO_SCAN_WINDOW(EXPECTED_WINDOW_MS));
    zassert_true(captured_scan_param.window <= captured_scan_param.interval);
    zassert_not_null(bt_le_scan_start_fake.arg1_val);
}

/** ble_scan() はすでにスキャンしていれば (EALREADY), 成功を返す */
ZTEST(ble_gateway, test_scan_already_started)
{
    bt_le_scan_start_fake.return_val = -EALREADY;

    /* 期待: すでにスキャン中 (-EALREADY) は, エラーにしない */
    zassert_equal(ble_scan(), EXIT_SUCCESS);
}

/** ble_scan() は bt_le_scan_start() のそのほかのエラーをそのまま返す */
ZTEST(ble_gateway, test_scan_failure)
{
    bt_le_scan_start_fake.return_val = -ENOMEM;

    /* 期待: スキャンを始められなければ, そのエラーを返す */
    zassert_equal(ble_scan(), -ENOMEM);
    zassert_equal(bt_le_scan_start_fake.call_count, 1U);
}

/** Thermo サービスを持たないデバイスには, 接続しない */
ZTEST(ble_gateway, test_scan_ignores_other_devices)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    set_adv_without_service();
    scan_cb(&node_addr[0], TEST_RSSI, BT_GAP_ADV_TYPE_ADV_IND, &adv);

    /* 期待: Thermo の UUID がないデバイスは, 既知の確認もせずに無視する */
    zassert_equal(bt_conn_lookup_addr_le_fake.call_count, 0U);
    zassert_equal(bt_le_scan_stop_fake.call_count, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 0U);
}

/** Thermo サービスを持つノードを見つけたら, スキャンを止めて, そのアドレスへ接続する */
ZTEST(ble_gateway, test_scan_connects_to_node)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    find_node(scan_cb, 0U);

    /* 期待: ノードを見つけたら, スキャンを止めて, そのアドレスに接続を始める */
    zassert_equal(bt_conn_lookup_addr_le_fake.call_count, 1U);
    zassert_equal(bt_le_scan_stop_fake.call_count, 1U);
    zassert_equal(bt_conn_le_create_fake.call_count, 1U);
    zassert_true(bt_addr_le_eq(bt_conn_le_create_fake.arg0_val, &node_addr[0]));
}

/** すでに接続している (または接続中の) ノードは, 無視する (参照は解放する) */
ZTEST(ble_gateway, test_scan_ignores_known_node)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    bt_conn_lookup_addr_le_fake.return_val = conn_of(0U);
    find_node(scan_cb, 0U);

    /* 期待: すでに接続済みのノードは, 参照を解放して, 接続し直さない */
    zassert_equal(bt_conn_unref_fake.call_count, 1U);
    zassert_equal(bt_conn_unref_fake.arg0_val, conn_of(0U));
    zassert_equal(bt_le_scan_stop_fake.call_count, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 0U);
}

/** 接続できる台数 (CONFIG_BT_MAX_CONN) に達したら, それ以上は接続しない */
ZTEST(ble_gateway, test_scan_respects_max_nodes)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */
    unsigned int i = 0U;                      /* ループ用の添字 */

    for (i = 0U; i < MAX_NODES; i++) {
        find_node(scan_cb, i);
    }
    zassert_equal(bt_conn_le_create_fake.call_count, (unsigned int)MAX_NODES);

    /* 次のノードには, 接続しない */
    find_node(scan_cb, MAX_NODES);
    zassert_equal(bt_conn_le_create_fake.call_count, (unsigned int)MAX_NODES);
}

/** スキャンを止められなかったら (EBUSY 以外), 接続しない. 次に見つけたときにやり直す */
ZTEST(ble_gateway, test_scan_stop_failure)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    bt_le_scan_stop_fake.return_val = -EIO;
    find_node(scan_cb, 0U);

    /* 期待: スキャンを止められなければ, 接続しない (次に見つけたときにやり直す) */
    zassert_equal(bt_le_scan_stop_fake.call_count, 1U);
    zassert_equal(bt_conn_le_create_fake.call_count, 0U);

    bt_le_scan_stop_fake.return_val = 0;
    find_node(scan_cb, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 1U);
}

/** スキャンを止められなくても (EBUSY), 少し待ってやり直して, 止められたら接続する */
ZTEST(ble_gateway, test_scan_stop_busy_retries)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */
    int results[] = {-EBUSY, -EBUSY, 0};      /* モックが順に返す戻り値 */

    SET_RETURN_SEQ(bt_le_scan_stop, results, ARRAY_SIZE(results));
    find_node(scan_cb, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 0U); /* まだ, 待っている */

    k_msleep(CONNECT_RETRY_MS * 3);
    zassert_equal(bt_le_scan_stop_fake.call_count, 3U);
    zassert_equal(bt_conn_le_create_fake.call_count, 1U);
    zassert_true(bt_addr_le_eq(bt_conn_le_create_fake.arg0_val, &node_addr[0]));
}

/** スキャンを止められない (EBUSY) まま, やり直しの回数を超えたら, あきらめる */
ZTEST(ble_gateway, test_scan_stop_busy_gives_up)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    bt_le_scan_stop_fake.return_val = -EBUSY;
    find_node(scan_cb, 0U);

    k_msleep(CONNECT_RETRY_MS * (CONNECT_RETRY_MAX + 2U));
    zassert_equal(bt_le_scan_stop_fake.call_count, CONNECT_RETRY_MAX + 1U);
    zassert_equal(bt_conn_le_create_fake.call_count, 0U);

    /* あきらめたあとも次に見つけたときに, 接続できる */
    bt_le_scan_stop_fake.return_val = 0;
    find_node(scan_cb, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 1U);
}

/** bt_conn_le_create() が戻る前に接続が完了しても, その接続を使って, 探索を始める */
ZTEST(ble_gateway, test_connected_before_create_returns)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    bt_conn_le_create_fake.custom_fake = fake_conn_le_create_connected_early;
    find_node(scan_cb, 0U);

    zassert_equal(bt_conn_ref_fake.call_count, 1U);
    zassert_equal(bt_gatt_discover_fake.call_count, 1U);
    zassert_equal(bt_gatt_discover_fake.arg0_val, conn_of(0U));

    /* 参照は接続のコールバックと, bt_conn_le_create() の 2 つ. 1 つはすぐに解放する */
    zassert_equal(bt_conn_unref_fake.call_count, 1U);

    /* 切断すると, 残りの参照を解放する */
    conn_cb->disconnected(conn_of(0U), 0x08U);
    zassert_equal(bt_conn_unref_fake.call_count, 2U);
}

/** bt_conn_le_create() が戻る前の接続でも接続を始めたノードと違うアドレスなら, 無視する */
ZTEST(ble_gateway, test_connected_other_address_before_create_returns)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    next_conn = 1U; /* ノード 0 に接続する間に, ノード 1 の接続が完了する */
    bt_conn_le_create_fake.custom_fake = fake_conn_le_create_connected_early;
    find_node(scan_cb, 0U);

    zassert_equal(bt_conn_ref_fake.call_count, 0U);
    zassert_equal(bt_gatt_discover_fake.call_count, 0U);
}

/** 接続を始められなかったら, 状態を戻して, スキャンを再開する */
ZTEST(ble_gateway, test_scan_connect_failure_restarts_scan)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    bt_conn_le_create_fake.custom_fake = NULL;
    bt_conn_le_create_fake.return_val = -ENOMEM;
    find_node(scan_cb, 0U);

    zassert_equal(bt_conn_le_create_fake.call_count, 1U);
    zassert_equal(bt_le_scan_start_fake.call_count, 2U); /* 最初と, 再開 */

    /* 状態が戻っているので同じノードに, もう一度接続できる */
    bt_conn_le_create_fake.custom_fake = fake_conn_le_create;
    find_node(scan_cb, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 2U);
}

/** 接続できたら, Thermo サービスの探索を始めて, スキャンを再開する */
ZTEST(ble_gateway, test_connected_starts_discovery)
{
    scan_cb_ptr_t scan_cb = start_scanning();      /* スキャン結果のコールバック */
    struct bt_gatt_discover_params *params = NULL; /* ディスカバリのパラメータ */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);

    zassert_equal(bt_gatt_discover_fake.call_count, 1U);
    zassert_equal(bt_gatt_discover_fake.arg0_val, conn_of(0U));
    params = bt_gatt_discover_fake.arg1_val;
    zassert_equal(params->type, BT_GATT_DISCOVER_PRIMARY);
    zassert_true(memcmp(BT_UUID_128(params->uuid)->val, service_uuid, sizeof(service_uuid)) == 0);
    zassert_equal(params->start_handle, BT_ATT_FIRST_ATTRIBUTE_HANDLE);
    zassert_equal(params->end_handle, BT_ATT_LAST_ATTRIBUTE_HANDLE);
    zassert_not_null(params->func);

    /* ほかのノードを探すためにスキャンを再開する */
    zassert_equal(bt_le_scan_start_fake.call_count, 2U);
}

/** 接続に失敗したら, 状態を戻して (参照を解放して), スキャンを再開する */
ZTEST(ble_gateway, test_connected_failure)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0x3EU);

    /* 期待: 接続の失敗では, 探索せずに参照を解放して, スキャンを再開する */
    zassert_equal(bt_gatt_discover_fake.call_count, 0U);
    zassert_equal(bt_conn_unref_fake.call_count, 1U);
    zassert_equal(bt_le_scan_start_fake.call_count, 2U);
}

/** このゲートウェイが始めたものではない接続は, 無視する */
ZTEST(ble_gateway, test_connected_unknown_connection)
{
    (void)start_scanning();

    conn_cb->connected(conn_of(3U), 0U);

    /* 期待: 自分が始めた接続でなければ, 何もしない */
    zassert_equal(bt_gatt_discover_fake.call_count, 0U);
    zassert_equal(bt_le_scan_start_fake.call_count, 1U);
}

/** 探索を始められなかったら, 切断する */
ZTEST(ble_gateway, test_connected_discovery_failure_disconnects)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    find_node(scan_cb, 0U);
    bt_gatt_discover_fake.return_val = -ENOMEM;
    conn_cb->connected(conn_of(0U), 0U);

    /* 期待: 探索を始められなければ, その接続を切断する */
    zassert_equal(bt_conn_disconnect_fake.call_count, 1U);
    zassert_equal(bt_conn_disconnect_fake.arg0_val, conn_of(0U));
}

/**
 * サービス, 温度の特性, CCC を順に探索して, 最後に通知を購読する
 * (探索のコールバックを見つかった属性を渡して, 呼ぶ)
 */
ZTEST(ble_gateway, test_discovery_flow_subscribes)
{
    scan_cb_ptr_t scan_cb = start_scanning();      /* スキャン結果のコールバック */
    struct bt_gatt_discover_params *params = NULL; /* ディスカバリのパラメータ */
    struct bt_gatt_service_val service_val = {.end_handle = 0x0030U}; /* GATT サービスの値 */
    struct bt_gatt_attr service_attr = {.handle = 0x0010U,
                                        .user_data = &service_val}; /* サービスの属性 */
    struct bt_gatt_attr chrc_attr = {.handle = 0x0012U}; /* キャラクタリスティックの属性 */
    struct bt_gatt_attr ccc_attr = {.handle = 0x0014U};  /* CCC ディスクリプタの属性 */
    struct bt_gatt_subscribe_params *sub = NULL;         /* 購読のパラメータ */
    uint8_t ret = 0U;                                    /* 戻り値 */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);
    params = bt_gatt_discover_fake.arg1_val;

    /* サービスが見つかった: 次は, その中から, 温度の特性を探す */
    ret = params->func(conn_of(0U), &service_attr, params);
    zassert_equal(ret, BT_GATT_ITER_STOP);
    zassert_equal(bt_gatt_discover_fake.call_count, 2U);
    zassert_equal(params->type, BT_GATT_DISCOVER_CHARACTERISTIC);
    zassert_true(memcmp(BT_UUID_128(params->uuid)->val, temperature_uuid,
                        sizeof(temperature_uuid)) == 0);
    zassert_equal(params->start_handle, 0x0011U);
    zassert_equal(params->end_handle, 0x0030U);

    /* 温度の特性が見つかった: 次は, その CCC (通知の設定) を探す */
    bt_gatt_attr_value_handle_fake.return_val = 0x0013U;
    ret = params->func(conn_of(0U), &chrc_attr, params);
    zassert_equal(ret, BT_GATT_ITER_STOP);
    zassert_equal(bt_gatt_discover_fake.call_count, 3U);
    zassert_equal(params->type, BT_GATT_DISCOVER_DESCRIPTOR);
    zassert_equal(BT_UUID_16(params->uuid)->val, BT_UUID_GATT_CCC_VAL);
    zassert_equal(params->start_handle, 0x0014U);

    /* CCC が見つかった: 通知を購読する */
    ret = params->func(conn_of(0U), &ccc_attr, params);
    zassert_equal(ret, BT_GATT_ITER_STOP);
    zassert_equal(bt_gatt_subscribe_fake.call_count, 1U);
    zassert_equal(bt_gatt_subscribe_fake.arg0_val, conn_of(0U));
    sub = bt_gatt_subscribe_fake.arg1_val;
    zassert_equal(sub->value_handle, 0x0013U);
    zassert_equal(sub->ccc_handle, 0x0014U);
    zassert_equal(sub->value, BT_GATT_CCC_NOTIFY);
    zassert_not_null(sub->notify);
    zassert_equal(bt_conn_disconnect_fake.call_count, 0U);
}

/** サービスが見つからなかったら (属性が NULL), 切断する */
ZTEST(ble_gateway, test_discovery_not_found_disconnects)
{
    scan_cb_ptr_t scan_cb = start_scanning();      /* スキャン結果のコールバック */
    struct bt_gatt_discover_params *params = NULL; /* ディスカバリのパラメータ */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);
    params = bt_gatt_discover_fake.arg1_val;

    /* 期待: サービスが見つからなければ (属性が NULL), 探索を止めて, 切断する */
    zassert_equal(params->func(conn_of(0U), NULL, params), BT_GATT_ITER_STOP);
    zassert_equal(bt_conn_disconnect_fake.call_count, 1U);
}

/** 探索や購読のいずれかが失敗したら, 切断する (購読がすでに済んでいる EALREADY は, 成功) */
ZTEST(ble_gateway, test_discovery_failure_disconnects)
{
    scan_cb_ptr_t scan_cb = start_scanning();      /* スキャン結果のコールバック */
    struct bt_gatt_discover_params *params = NULL; /* ディスカバリのパラメータ */
    struct bt_gatt_service_val service_val = {.end_handle = 0x0030U}; /* GATT サービスの値 */
    struct bt_gatt_attr service_attr = {.handle = 0x0010U,
                                        .user_data = &service_val}; /* サービスの属性 */
    struct bt_gatt_attr ccc_attr = {.handle = 0x0014U};             /* CCC ディスクリプタの属性 */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);
    params = bt_gatt_discover_fake.arg1_val;

    /* 次の探索を始められない */
    bt_gatt_discover_fake.return_val = -ENOMEM;
    (void)params->func(conn_of(0U), &service_attr, params);
    zassert_equal(bt_conn_disconnect_fake.call_count, 1U);

    /* 購読できない */
    params->type = BT_GATT_DISCOVER_DESCRIPTOR;
    bt_gatt_subscribe_fake.return_val = -EIO;
    (void)params->func(conn_of(0U), &ccc_attr, params);
    zassert_equal(bt_conn_disconnect_fake.call_count, 2U);

    /* すでに購読している (EALREADY) のは, 成功として扱う */
    params->type = BT_GATT_DISCOVER_DESCRIPTOR;
    bt_gatt_subscribe_fake.return_val = -EALREADY;
    (void)params->func(conn_of(0U), &ccc_attr, params);
    zassert_equal(bt_conn_disconnect_fake.call_count, 2U);
}

/** 通知された温度 (リトルエンディアン) をノードのアドレスとともに, コールバックへ渡す */
ZTEST(ble_gateway, test_notification_delivers_temperature)
{
    scan_cb_ptr_t scan_cb = start_scanning();           /* スキャン結果のコールバック */
    struct bt_gatt_discover_params *params = NULL;      /* ディスカバリのパラメータ */
    struct bt_gatt_attr ccc_attr = {.handle = 0x0014U}; /* CCC ディスクリプタの属性 */
    struct bt_gatt_subscribe_params *sub = NULL;        /* 購読のパラメータ */
    const uint8_t data[THERMO_TEMPERATURE_SIZE] = {(uint8_t)(TEST_RAW & 0xFFU),
                                                   (uint8_t)(TEST_RAW >> 8)};
    uint8_t ret = 0U; /* 戻り値 */

    find_node(scan_cb, 1U); /* 2 台目 (アドレス B) のノード */
    next_conn = 0U;
    conn_cb->connected(conn_of(0U), 0U);
    params = bt_gatt_discover_fake.arg1_val;
    params->type = BT_GATT_DISCOVER_DESCRIPTOR;
    (void)params->func(conn_of(0U), &ccc_attr, params);
    sub = bt_gatt_subscribe_fake.arg1_val;

    ret = sub->notify(conn_of(0U), sub, data, sizeof(data));
    zassert_equal(ret, BT_GATT_ITER_CONTINUE);
    zassert_equal(received.count, 1U);
    zassert_equal(received.raw, TEST_RAW);
    zassert_mem_equal(received.addr.a.val, node_addr[0].a.val, sizeof(received.addr.a.val));
}

/** 大きさが違う通知は無視する (購読は続ける) */
ZTEST(ble_gateway, test_notification_wrong_length_ignored)
{
    scan_cb_ptr_t scan_cb = start_scanning();           /* スキャン結果のコールバック */
    struct bt_gatt_discover_params *params = NULL;      /* ディスカバリのパラメータ */
    struct bt_gatt_attr ccc_attr = {.handle = 0x0014U}; /* CCC ディスクリプタの属性 */
    struct bt_gatt_subscribe_params *sub = NULL;        /* 購読のパラメータ */
    const uint8_t data[3] = {1U, 2U, 3U};               /* 入力データ */

    /* ノードに接続して, 購読まで進める */
    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);
    params = bt_gatt_discover_fake.arg1_val;
    params->type = BT_GATT_DISCOVER_DESCRIPTOR;
    (void)params->func(conn_of(0U), &ccc_attr, params);
    sub = bt_gatt_subscribe_fake.arg1_val;

    /* 温度は 2 byte. 3 byte の通知は, 捨てて, コールバックを呼ばない (購読は続ける) */
    zassert_equal(sub->notify(conn_of(0U), sub, data, sizeof(data)), BT_GATT_ITER_CONTINUE);
    zassert_equal(received.count, 0U);
}

/** 購読が解除された通知 (データが NULL) は, 購読を終える. コールバックが未設定でも落ちない */
ZTEST(ble_gateway, test_notification_unsubscribed_and_no_callback)
{
    scan_cb_ptr_t scan_cb = start_scanning();               /* スキャン結果のコールバック */
    struct bt_gatt_discover_params *params = NULL;          /* ディスカバリのパラメータ */
    struct bt_gatt_attr ccc_attr = {.handle = 0x0014U};     /* CCC ディスクリプタの属性 */
    struct bt_gatt_subscribe_params *sub = NULL;            /* 購読のパラメータ */
    const uint8_t data[THERMO_TEMPERATURE_SIZE] = {1U, 0U}; /* 入力データ */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);
    params = bt_gatt_discover_fake.arg1_val;
    params->type = BT_GATT_DISCOVER_DESCRIPTOR;
    (void)params->func(conn_of(0U), &ccc_attr, params);
    sub = bt_gatt_subscribe_fake.arg1_val;

    /* コールバックを解除しても (NULL), 通知を受け取って, 落ちない */
    ble_set_temperature_callback(NULL);
    zassert_equal(sub->notify(conn_of(0U), sub, data, sizeof(data)), BT_GATT_ITER_CONTINUE);
    zassert_equal(received.count, 0U);

    /* 購読の解除 */
    sub->value_handle = 0x0013U;
    zassert_equal(sub->notify(conn_of(0U), sub, NULL, 0U), BT_GATT_ITER_STOP);
    zassert_equal(sub->value_handle, 0U);
}

/** 切断されたら, 状態を戻して (参照を解放して), スキャンを再開する. 同じノードへ再び接続できる */
ZTEST(ble_gateway, test_disconnected_releases_and_restarts_scan)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */
    unsigned int scan_starts = 0U;            /* スキャン開始の回数 */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);
    scan_starts = bt_le_scan_start_fake.call_count;
    zassert_equal(bt_conn_unref_fake.call_count, 0U);

    conn_cb->disconnected(conn_of(0U), 0x08U);
    zassert_equal(bt_conn_unref_fake.call_count, 1U);
    zassert_equal(bt_conn_unref_fake.arg0_val, conn_of(0U));
    zassert_equal(bt_le_scan_start_fake.call_count, scan_starts + 1U);

    /* 空いたのでもう一度, 接続できる */
    next_conn = 0U;
    find_node(scan_cb, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 2U);
}

/** このゲートウェイが始めたものではない接続の切断は, 無視する */
ZTEST(ble_gateway, test_disconnected_unknown_connection)
{
    (void)start_scanning();

    conn_cb->disconnected(conn_of(3U), 0x08U);

    /* 期待: 自分が始めた接続でなければ, 参照の解放もスキャンの再開もしない */
    zassert_equal(bt_conn_unref_fake.call_count, 0U);
    zassert_equal(bt_le_scan_start_fake.call_count, 1U);
}

/** UUID (128 bit) が Thermo サービスと違うデバイスには, 接続しない */
ZTEST(ble_gateway, test_scan_ignores_other_uuid)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    set_adv_with_service();
    adv_bytes[2] ^= 0xFFU; /* UUID の先頭の 1 byte を変える */
    scan_cb(&node_addr[0], TEST_RSSI, BT_GAP_ADV_TYPE_SCAN_RSP, &adv);
    k_msleep(WORK_WAIT_MS);

    zassert_equal(bt_conn_le_create_fake.call_count, 0U);
}

/** UUID (128 bit) の項目の長さが違うデバイス (壊れたデータ) には, 接続しない */
ZTEST(ble_gateway, test_scan_ignores_short_uuid)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    set_adv_with_service();
    adv_bytes[0] = 5U;
    adv.len = 6U;
    scan_cb(&node_addr[0], TEST_RSSI, BT_GAP_ADV_TYPE_SCAN_RSP, &adv);
    k_msleep(WORK_WAIT_MS);

    /* 期待: UUID の項目の長さが違う壊れたデータには, 接続しない */
    zassert_equal(bt_conn_le_create_fake.call_count, 0U);
}

/** 接続を始める依頼を処理している間 (スキャンを止められず, 待っている間) は次の依頼を受けない */
ZTEST(ble_gateway, test_scan_ignores_while_pending)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    bt_le_scan_stop_fake.return_val = -EBUSY;
    find_node(scan_cb, 0U);
    find_node(scan_cb, 1U); /* 待っている間に別のノードを見つけた */
    zassert_equal(bt_conn_lookup_addr_le_fake.call_count, 1U);

    /* 待ったあとに止められれば, 最初のノードに接続する */
    bt_le_scan_stop_fake.return_val = 0;
    k_msleep(CONNECT_RETRY_MS * 2);
    zassert_equal(bt_conn_le_create_fake.call_count, 1U);
    zassert_true(bt_addr_le_eq(bt_conn_le_create_fake.arg0_val, &node_addr[0]));
}

/** 接続を始められず, スキャンの再開にも失敗しても次に見つけたときに, 接続できる */
ZTEST(ble_gateway, test_scan_connect_failure_and_scan_failure)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    /* 接続を始められず, スキャンの再開にも失敗する */
    bt_conn_le_create_fake.custom_fake = NULL;
    bt_conn_le_create_fake.return_val = -ENOMEM;
    bt_le_scan_start_fake.return_val = -EIO;
    find_node(scan_cb, 0U);
    zassert_equal(bt_le_scan_start_fake.call_count, 2U);

    /* 状態が戻っているので次に見つけたときに, 同じノードへ接続できる */
    bt_conn_le_create_fake.custom_fake = fake_conn_le_create;
    find_node(scan_cb, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 2U);
}

/** 接続できたあと, スキャンを再開できなくても探索は始める */
ZTEST(ble_gateway, test_connected_scan_restart_failure)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    find_node(scan_cb, 0U);
    bt_le_scan_start_fake.return_val = -EIO;
    conn_cb->connected(conn_of(0U), 0U);

    /* 期待: スキャンの再開に失敗しても, 探索はすでに始まっている */
    zassert_equal(bt_gatt_discover_fake.call_count, 1U);
    zassert_equal(bt_le_scan_start_fake.call_count, 2U);
}

/** 探索を始められず, 切断にも失敗しても処理を続ける (スキャンを再開する) */
ZTEST(ble_gateway, test_connected_discovery_and_disconnect_failure)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    find_node(scan_cb, 0U);
    bt_gatt_discover_fake.return_val = -ENOMEM;
    bt_conn_disconnect_fake.return_val = -EIO;
    conn_cb->connected(conn_of(0U), 0U);

    /* 期待: 切断にも失敗してもエラーにせず, スキャンを再開する */
    zassert_equal(bt_conn_disconnect_fake.call_count, 1U);
    zassert_equal(bt_le_scan_start_fake.call_count, 2U);
}

/** サービスが見つからず, 切断にも失敗しても探索を止める */
ZTEST(ble_gateway, test_discovery_not_found_and_disconnect_failure)
{
    scan_cb_ptr_t scan_cb = start_scanning();      /* スキャン結果のコールバック */
    struct bt_gatt_discover_params *params = NULL; /* ディスカバリのパラメータ */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);
    params = bt_gatt_discover_fake.arg1_val;

    bt_conn_disconnect_fake.return_val = -EIO;
    /* 期待: 切断に失敗しても探索は止める */
    zassert_equal(params->func(conn_of(0U), NULL, params), BT_GATT_ITER_STOP);
    zassert_equal(bt_conn_disconnect_fake.call_count, 1U);
}

/** 次の探索を始められず, 切断にも失敗しても探索を止める */
ZTEST(ble_gateway, test_discovery_next_failure_and_disconnect_failure)
{
    scan_cb_ptr_t scan_cb = start_scanning();      /* スキャン結果のコールバック */
    struct bt_gatt_discover_params *params = NULL; /* ディスカバリのパラメータ */
    struct bt_gatt_service_val service_val = {.end_handle = 0x0030U}; /* GATT サービスの値 */
    struct bt_gatt_attr service_attr = {.handle = 0x0010U,
                                        .user_data = &service_val}; /* サービスの属性 */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);
    params = bt_gatt_discover_fake.arg1_val;

    /* サービスは見つかったが次の探索を始められず, 切断にも失敗する */
    bt_gatt_discover_fake.return_val = -ENOMEM;
    bt_conn_disconnect_fake.return_val = -EIO;
    zassert_equal(params->func(conn_of(0U), &service_attr, params), BT_GATT_ITER_STOP);
    zassert_equal(bt_conn_disconnect_fake.call_count, 1U);
}

/** 切断されたあと, スキャンを再開できなくても状態は戻す */
ZTEST(ble_gateway, test_disconnected_scan_restart_failure)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    find_node(scan_cb, 0U);
    conn_cb->connected(conn_of(0U), 0U);
    bt_le_scan_start_fake.return_val = -EIO;
    conn_cb->disconnected(conn_of(0U), 0x08U);

    /* 期待: スキャンの再開に失敗しても, 状態 (参照) は戻している */
    zassert_equal(bt_conn_unref_fake.call_count, 1U);
}

/** SwitchBot のサービスデータ (機種, 電池残量) は接続せずにコールバックに渡す */
ZTEST(ble_gateway, test_scan_switchbot_service_data)
{
    scan_cb_ptr_t scan_cb = start_scanning();                 /* スキャン結果のコールバック */
    const uint8_t service[] = {0x3D, 0xFD, 0x77, 0x00, 0x64}; /* サービス */

    ble_set_switchbot_callback(on_switchbot);
    set_adv_element(BT_DATA_SVC_DATA16, service, sizeof(service));
    scan_cb(&node_addr[0], TEST_RSSI, BT_GAP_ADV_TYPE_ADV_IND, &adv);
    k_msleep(WORK_WAIT_MS);

    /* 期待: コールバックにアドレスと, 機種 'w' と電池残量 100 % を渡す. 接続はしない */
    zassert_equal(switchbot_received.count, 1U);
    zassert_true(bt_addr_le_eq(&switchbot_received.addr, &node_addr[0]));
    zassert_equal(switchbot_received.ad.kind, SWITCHBOT_INFO);
    zassert_equal(switchbot_received.ad.model, SWITCHBOT_MODEL_OUTDOOR);
    zassert_equal(switchbot_received.ad.battery, 100U);
    zassert_equal(bt_conn_lookup_addr_le_fake.call_count, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 0U);
}

/** SwitchBot の製造者データ (温度, 湿度) も接続せずにコールバックに渡す */
ZTEST(ble_gateway, test_scan_switchbot_manufacturer_data)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */
    const uint8_t mfr[] = {0x69, 0x09, 0xB0, 0xE9, 0xFE, 0x12, 0x34,
                           0x56, 0x00, 0x00, 0x05, 0x97, 0x37};

    ble_set_switchbot_callback(on_switchbot);
    set_adv_element(BT_DATA_MANUFACTURER_DATA, mfr, sizeof(mfr));
    scan_cb(&node_addr[1], TEST_RSSI, BT_GAP_ADV_TYPE_ADV_IND, &adv);

    /* 期待: 23.5 ℃, 湿度 55 % */
    zassert_equal(switchbot_received.count, 1U);
    zassert_equal(switchbot_received.ad.kind, SWITCHBOT_ENV);
    zassert_equal(switchbot_received.ad.temp_x10, 235);
    zassert_equal(switchbot_received.ad.humidity, 55U);
    zassert_equal(bt_conn_le_create_fake.call_count, 0U);
}

/** SwitchBot のコールバックを設定していなければ, SwitchBot のデータは, 捨てる (接続もしない) */
ZTEST(ble_gateway, test_scan_switchbot_without_callback)
{
    scan_cb_ptr_t scan_cb = start_scanning();                 /* スキャン結果のコールバック */
    const uint8_t service[] = {0x3D, 0xFD, 0x77, 0x00, 0x64}; /* サービス */

    set_adv_element(BT_DATA_SVC_DATA16, service, sizeof(service));
    scan_cb(&node_addr[0], TEST_RSSI, BT_GAP_ADV_TYPE_ADV_IND, &adv);
    k_msleep(WORK_WAIT_MS);

    zassert_equal(switchbot_received.count, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 0U);
}

/** Thermo のノードを見つけても, SwitchBot のコールバックは呼ばない (従来どおり, 接続する) */
ZTEST(ble_gateway, test_scan_thermo_node_is_not_switchbot)
{
    scan_cb_ptr_t scan_cb = start_scanning(); /* スキャン結果のコールバック */

    ble_set_switchbot_callback(on_switchbot);
    find_node(scan_cb, 0U);

    zassert_equal(switchbot_received.count, 0U);
    zassert_equal(bt_conn_le_create_fake.call_count, 1U);
}

ZTEST_SUITE(ble_gateway, NULL, NULL, before, after, NULL);
