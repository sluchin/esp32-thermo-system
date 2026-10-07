/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief thermo-gateway の BLE 実装 (ノードのスキャンと接続, GATT での温度の受信)
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>   /* EALREADY EBUSY */
#include <stdbool.h> /* bool true false */
#include <stdint.h>  /* uint8_t uint16_t int8_t */
#include <stdlib.h>  /* EXIT_SUCCESS */
#include <string.h>  /* memset memcmp */

#include "ble.h"
#include "thermo_ble_uuid.h"
#include "thermo_log.h"

LOG_MODULE_REGISTER(ble_thermo_gateway, THERMO_LOG_LEVEL);

/** スキャン間隔 [ms] */
#define SCAN_INTERVAL_MS  100
/** スキャンウィンドウ [ms] */
#define SCAN_WINDOW_MS    50
/** 接続できるノードの数 (接続の数と同じ) */
#define MAX_NODES         CONFIG_BT_MAX_CONN
/** スキャンを止められなかったとき (EBUSY) に接続を始め直すまでの間隔 [ms] */
#define CONNECT_RETRY_MS  100
/** スキャンを止められなかったときに, 接続を始め直す最大の回数 */
#define CONNECT_RETRY_MAX 10U

/** 接続しているノード 1 台ぶんの状態 */
struct node {
    struct bt_conn *conn;                      /**< 接続 (使っていなければ NULL) */
    struct bt_gatt_discover_params discover;   /**< サービスの探索のパラメータ */
    struct bt_gatt_subscribe_params subscribe; /**< 通知の購読のパラメータ */
};

/** 接続しているノード */
static struct node nodes[MAX_NODES];

/** Thermo サービスの UUID */
static const struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(THERMO_UUID_SERVICE_VAL);
/** 温度の特性の UUID */
static const struct bt_uuid_128 temperature_uuid = BT_UUID_INIT_128(THERMO_UUID_TEMPERATURE_VAL);
/** CCC (通知の設定) の UUID */
static const struct bt_uuid_16 ccc_uuid = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);

/** スキャン応答の UUID (128 bit) と比べるための Thermo サービスの UUID の値 */
static const uint8_t service_uuid_val[] = {THERMO_UUID_SERVICE_VAL};

/** 温度を受信したときのコールバック */
static ble_temperature_cb_t temperature_cb;
/** SwitchBot のアドバタイズを受信したときのコールバック */
static ble_switchbot_cb_t switchbot_cb;

/** 接続を始めるノードのアドレス (スキャンのコールバックが記録する) */
static bt_addr_le_t pending_addr;
/** 接続を始める依頼で確保したノードの状態 (依頼から, 接続を始めるまでの間. 依頼がなければ NULL)
 */
static struct node *pending_node;
/** 接続を始め直した回数 */
static unsigned int pending_retries;

static struct node *find_node(const struct bt_conn *conn);
static struct node *find_free_node(void);
static void release_node(struct node *node);
static bool parse_ad(struct bt_data *data, void *user_data);
static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                         const void *data, uint16_t length);
static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params);
static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);
static void connect_work_handler(struct k_work *work);
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *adv_data);

/** 接続を始める処理 (システムのワークキューで実行する) */
static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_handler);

/** アドバタイズデータ (スキャン応答を含む) を解析した結果 */
struct scan_result {
    bool thermo;                   /**< Thermo サービスの UUID があった */
    bool has_switchbot;            /**< SwitchBot の温湿度計のデータがあった */
    struct switchbot_ad switchbot; /**< SwitchBot の温湿度計のデータ (has_switchbot のとき) */
};

#if (THERMO_LOG_LEVEL >= 4) /* 4: LOG_LEVEL_DBG (#if では, 列挙子を使えない) */
/** アドバタイズデータの 16 進ダンプを出す間隔 (要素の数. この回数に 1 回だけ出す) */
#define ADV_HEXDUMP_EVERY 50U

/**
 * アドバタイズデータの 1 要素を, 16 進数でログに出す (ADV_HEXDUMP_EVERY 回に 1 回だけ)
 *
 * スキャンでは, まわりの機器の広告が大量に届くので, 回数で間引く.
 *
 * @param[in] data アドバタイズデータの 1 要素
 */
static void adv_hexdump(const struct bt_data *data)
{
    static uint32_t count; /* 受け取った要素の数 (static なので 0 から始まる) */

    if ((count % ADV_HEXDUMP_EVERY) == 0U) {
        LOG_HEXDUMP_DBG(data->data, data->data_len, "Advertising data");
    }
    count++;
}
#else
/** アドバタイズデータの 16 進ダンプ (デバッグログが無効のときは, 何もしない) */
#define adv_hexdump(data)                                                                          \
    do {                                                                                           \
    } while (0)
#endif

/** 接続のコールバック */
static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

/**
 * @brief Bluetooth スタックを初期化して, 接続のコールバックを登録する
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_init(void)
{
    int err = EXIT_SUCCESS; /* エラーコード */

    /* Bluetooth スタックを, 同期で有効にする (戻ったときには, 使える) */
    err = bt_enable(NULL);
    if (err != 0) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
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
 * @brief 温度を受信したときのコールバックを設定する
 *
 * ble_scan() の前に設定しておくこと. NULL で解除する.
 *
 * @param[in] cb コールバック
 */
void ble_set_temperature_callback(ble_temperature_cb_t cb)
{
    temperature_cb = cb;
}

/**
 * @brief SwitchBot 屋外用温湿度計のアドバタイズを受信したときのコールバックを設定する
 *
 * ble_scan() の前に設定しておくこと. NULL で解除する (SwitchBot のデータは, 捨てる).
 *
 * @param[in] cb コールバック
 */
void ble_set_switchbot_callback(ble_switchbot_cb_t cb)
{
    switchbot_cb = cb;
}

/**
 * @brief 周辺ノードのアクティブスキャンを開始する
 *
 * スキャン応答に Thermo サービスの UUID を持つノードを見つけたら, 接続して, 温度の特性の
 * 通知 (notify) を購読する. 温度を受信するたびに ble_set_temperature_callback() で設定した
 * コールバックを呼ぶ. 接続できる台数は CONFIG_BT_MAX_CONN まで. 接続が切れたら,
 * スキャンを再開して, 再び接続する.
 *
 * SwitchBot のアドバタイズ (接続しない) を受信したら, ble_set_switchbot_callback() で設定した
 * コールバックを呼ぶ.
 *
 * 事前に ble_init() を呼び出しておくこと.
 *
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
int ble_scan(void)
{
    int err = EXIT_SUCCESS; /* エラーコード */
    /* アクティブスキャン: ノードの UUID は, スキャン応答に入っているので要求を出して受け取る */
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_MS_TO_SCAN_INTERVAL(SCAN_INTERVAL_MS),
        .window = BT_GAP_MS_TO_SCAN_WINDOW(SCAN_WINDOW_MS),
    };

    err = bt_le_scan_start(&scan_param, scan_cb);
    if (err == -EALREADY) {
        return EXIT_SUCCESS; /* すでにスキャンしている */
    }
    if (err != 0) {
        LOG_ERR("Starting scan failed (err %d)", err);
        return err;
    }
    LOG_INF("BLE scan started");

    return EXIT_SUCCESS;
}

/**
 * 接続から, ノードの状態を探す
 *
 * @param[in] conn 接続 (NULL にしてはいけない)
 * @return ノードの状態 (見つからなければ NULL)
 */
static struct node *find_node(const struct bt_conn *conn)
{
    size_t i = 0U; /* ループ用の添字 */

    for (i = 0U; i < ARRAY_SIZE(nodes); i++) {
        if (nodes[i].conn == conn) {
            return &nodes[i];
        }
    }

    return NULL;
}

/**
 * 使っていないノードの状態を探す
 *
 * @return ノードの状態 (全て使っていれば NULL)
 */
static struct node *find_free_node(void)
{
    size_t i = 0U; /* ループ用の添字 */

    for (i = 0U; i < ARRAY_SIZE(nodes); i++) {
        if (nodes[i].conn == NULL) {
            return &nodes[i];
        }
    }

    return NULL;
}

/**
 * ノードの状態を使っていない状態に戻す (接続の参照も解放する)
 *
 * @param[in,out] node ノードの状態
 */
static void release_node(struct node *node)
{
    bt_conn_unref(node->conn);
    (void)memset(node, 0, sizeof(*node));
}

/**
 * アドバタイズデータの中から, Thermo サービスの UUID か, SwitchBot の温湿度計のデータを探す
 *
 * @param[in] data アドバタイズデータの 1 要素
 * @param[in,out] user_data 見つかったものを書き込む (struct scan_result *)
 * @return 探索を続けるなら true (見つかったら false)
 */
static bool parse_ad(struct bt_data *data, void *user_data)
{
    struct scan_result *result = (struct scan_result *)user_data; /* スキャンの結果 */

    adv_hexdump(data);

    if ((data->type == BT_DATA_UUID128_ALL) && (data->data_len == sizeof(service_uuid_val)) &&
        (memcmp(data->data, service_uuid_val, sizeof(service_uuid_val)) == 0)) {
        result->thermo = true;
        return false;
    }

    if (switchbot_parse(data->type, data->data, data->data_len, &result->switchbot)) {
        LOG_DBG("SwitchBot advertising data (AD type 0x%02X)", data->type);
        LOG_HEXDUMP_DBG(data->data, data->data_len, "SwitchBot advertising data");
        result->has_switchbot = true;
        return false;
    }

    return true;
}

/**
 * 通知のコールバック (ノードが送った温度と湿度を受信する)
 *
 * @param[in] conn 接続
 * @param[in,out] params 購読のパラメータ
 * @param[in] data 通知されたデータ (NULL なら, 購読が解除された)
 * @param[in] length データの大きさ
 * @return BT_GATT_ITER_CONTINUE (購読を続ける), または BT_GATT_ITER_STOP (購読を解除する)
 */
static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                         const void *data, uint16_t length)
{
    int16_t temp_x10 = 0;      /* 温度 [℃ の 10 倍] */
    uint16_t humidity_x10 = 0; /* 湿度 [% の 10 倍] */

    if (data == NULL) {
        LOG_INF("Unsubscribed");
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    LOG_HEXDUMP_DBG(data, length, "Notification");
    if (length != THERMO_TEMPERATURE_SIZE) {
        LOG_WRN("Unexpected notification length: %u", length);
        return BT_GATT_ITER_CONTINUE;
    }

    /* 温度 (int16) と湿度 (uint16) が, この順に, 2 byte ずつ, リトルエンディアンで並んでいる */
    temp_x10 = (int16_t)sys_get_le16((const uint8_t *)data);
    humidity_x10 = sys_get_le16((const uint8_t *)data + sizeof(uint16_t));
    if (temperature_cb != NULL) {
        temperature_cb(bt_conn_get_dst(conn), temp_x10, humidity_x10);
    }

    return BT_GATT_ITER_CONTINUE;
}

/**
 * サービスの探索のコールバック
 *
 * Thermo サービス, 温度の特性, CCC の順に探索して, 最後に通知を購読する.
 *
 * @param[in] conn 接続
 * @param[in] attr 見つかった属性 (NULL なら, 探索が終わった)
 * @param[in,out] params 探索のパラメータ
 * @return BT_GATT_ITER_STOP (探索を続けない)
 */
static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params)
{
    struct node *node = CONTAINER_OF(params, struct node, discover); /* 対象のノード */
    const struct bt_gatt_service_val *service = NULL;                /* GATT サービス */
    int err = EXIT_SUCCESS;                                          /* エラーコード */

    if (attr == NULL) {
        LOG_ERR("Thermo service not found");
        err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (err != 0) {
            LOG_ERR("Disconnect failed (err %d)", err);
        }
        return BT_GATT_ITER_STOP;
    }

    if (params->type == BT_GATT_DISCOVER_PRIMARY) {
        /* 次はサービスの中から, 温度の特性を探す */
        service = (const struct bt_gatt_service_val *)attr->user_data;
        params->uuid = &temperature_uuid.uuid;
        params->start_handle = (uint16_t)(attr->handle + 1U);
        params->end_handle = service->end_handle;
        params->type = BT_GATT_DISCOVER_CHARACTERISTIC;
        err = bt_gatt_discover(conn, params);
    } else if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
        /* 次は特性の CCC (通知の設定) を探す */
        node->subscribe.value_handle = bt_gatt_attr_value_handle(attr);
        params->uuid = &ccc_uuid.uuid;
        params->start_handle = (uint16_t)(attr->handle + 2U);
        params->type = BT_GATT_DISCOVER_DESCRIPTOR;
        err = bt_gatt_discover(conn, params);
    } else {
        /* CCC が見つかったので通知を購読する */
        node->subscribe.ccc_handle = attr->handle;
        node->subscribe.value = BT_GATT_CCC_NOTIFY;
        node->subscribe.notify = notify_cb;
        err = bt_gatt_subscribe(conn, &node->subscribe);
        if (err == -EALREADY) {
            err = EXIT_SUCCESS;
        }
        if (err == EXIT_SUCCESS) {
            LOG_INF("Subscribed to the temperature");
        }
    }

    if (err != 0) {
        LOG_ERR("GATT discovery or subscription failed (err %d)", err);
        err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (err != 0) {
            LOG_ERR("Disconnect failed (err %d)", err);
        }
    }

    return BT_GATT_ITER_STOP;
}

/**
 * 接続のコールバック
 *
 * 接続できたら, Thermo サービスの探索を始める. 接続できなかったら, 状態を戻す.
 * どちらの場合もほかのノードを探すために, スキャンを再開する.
 *
 * @param[in] conn 接続
 * @param[in] err 接続の結果 (0 なら成功)
 */
static void connected(struct bt_conn *conn, uint8_t err)
{
    struct node *node = find_node(conn); /* 対象のノード */
    const bt_addr_le_t *dst = NULL;      /* 接続先のアドレス */
    int ret = EXIT_SUCCESS;              /* 戻り値 */

    if ((node == NULL) && (pending_node != NULL)) {
        /*
         * bt_conn_le_create() が戻る前に接続が完了することがある (node->conn がまだ NULL).
         * 接続を始めたノードのアドレスと同じなら, この接続を使っていない状態に割り当てる.
         */
        dst = bt_conn_get_dst(conn);
        if (bt_addr_le_eq(dst, &pending_addr)) {
            node = pending_node;
            node->conn = bt_conn_ref(conn);
        }
    }

    if (node == NULL) {
        return; /* このゲートウェイが接続を始めたものではない */
    }

    if (err != 0U) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        release_node(node);
    } else {
        LOG_INF("Connected");
        node->discover.uuid = &service_uuid.uuid;
        node->discover.func = discover_cb;
        node->discover.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
        node->discover.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
        node->discover.type = BT_GATT_DISCOVER_PRIMARY;
        ret = bt_gatt_discover(conn, &node->discover);
        if (ret != 0) {
            LOG_ERR("Discovery failed to start (err %d)", ret);
            ret = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            if (ret != 0) {
                LOG_ERR("Disconnect failed (err %d)", ret);
            }
        }
    }

    ret = ble_scan();
    if (ret != EXIT_SUCCESS) {
        LOG_ERR("Failed to restart scanning (err %d)", ret);
    }
}

/**
 * 切断のコールバック (状態を戻して, スキャンを再開する)
 *
 * @param[in] conn 接続
 * @param[in] reason 切断の理由
 */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    struct node *node = find_node(conn); /* 対象のノード */
    int err = EXIT_SUCCESS;              /* エラーコード */

    if (node == NULL) {
        return;
    }

    LOG_INF("Disconnected (reason 0x%02x)", reason);
    release_node(node);

    err = ble_scan();
    if (err != EXIT_SUCCESS) {
        LOG_ERR("Failed to restart scanning (err %d)", err);
    }
}

/**
 * 接続を始める処理 (システムのワークキューで実行する)
 *
 * スキャンのコールバックの中でスキャンを止めたり, 接続を始めたりすると, スキャンの開始と
 * 重なって, スキャンを止められない (EBUSY) ことがある. そのため, コールバックでは, 依頼を
 * 記録するだけにして, スキャンの停止と, 接続はここで行う. EBUSY のときは, 少し待って, やり直す.
 *
 * @param[in] work 使用しない
 */
static void connect_work_handler(struct k_work *work)
{
    struct node *node = pending_node; /* 対象のノード */
    struct bt_conn *created = NULL;   /* 作成した接続 */
    int err = EXIT_SUCCESS;           /* エラーコード */

    ARG_UNUSED(work);

    /* 接続を始める前にスキャンを止める */
    err = bt_le_scan_stop();
    if ((err == -EBUSY) && (pending_retries < CONNECT_RETRY_MAX)) {
        pending_retries++;
        (void)k_work_reschedule(&connect_work, K_MSEC(CONNECT_RETRY_MS));
        return;
    }
    if (err != 0) {
        LOG_ERR("Stopping scan failed (err %d)", err);
        pending_node = NULL; /* 次にノードが見つかったときに, やり直す */
        return;
    }

    /* 接続を始める (結果は connected() / disconnected() で受け取る) */
    err = bt_conn_le_create(&pending_addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT,
                            &created);
    if (err == 0) {
        if (node->conn == NULL) {
            node->conn = created;
        } else {
            bt_conn_unref(created); /* 接続のコールバックが先に参照を取っている */
        }
    } else {
        LOG_ERR("Connection failed to start (err %d)", err);
        err = ble_scan();
        if (err != EXIT_SUCCESS) {
            LOG_ERR("Failed to restart scanning (err %d)", err);
        }
    }
    pending_node = NULL;
}

/**
 * スキャンのコールバック (ノードを見つけたら, 接続を始める依頼を記録する)
 *
 * SwitchBot の温湿度計のアドバタイズは, 接続せずに設定されたコールバックに渡す.
 *
 * Bluetooth のスレッドから呼ばれるので, 接続は connect_work_handler() が始める.
 *
 * @param[in] addr 見つかったデバイスのアドレス
 * @param[in] rssi 受信信号強度 [dBm]
 * @param[in] adv_type アドバタイズの種別 (使用しない)
 * @param[in] adv_data アドバタイズデータ (スキャン応答を含む)
 */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *adv_data)
{
    char addr_str[BT_ADDR_LE_STR_LEN] = {0}; /* アドレスの文字列 */
    struct bt_conn *known = NULL;            /* 既知の接続 */
    struct node *node = NULL;                /* 対象のノード */
    struct scan_result result = {0};         /* スキャンの結果 */

    ARG_UNUSED(adv_type);

    bt_data_parse(adv_data, parse_ad, &result);

    /* SwitchBot の温湿度計: 接続せずに, アドバタイズの値を渡す */
    if (result.has_switchbot) {
        if (switchbot_cb != NULL) {
            switchbot_cb(addr, &result.switchbot);
        }
        return;
    }

    if (!result.thermo) {
        return; /* Thermo サービスを持たないデバイス */
    }

    if (pending_node != NULL) {
        return; /* 接続を始める依頼を処理中 */
    }

    /* すでに接続している (または接続中の) ノードは, 無視する */
    known = bt_conn_lookup_addr_le(BT_ID_DEFAULT, addr);
    if (known != NULL) {
        bt_conn_unref(known);
        return;
    }

    node = find_free_node();
    if (node == NULL) {
        return; /* 接続できる台数に達している */
    }

    (void)bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    LOG_INF("Thermo node found: %s (RSSI %d)", addr_str, rssi);

    bt_addr_le_copy(&pending_addr, addr);
    pending_node = node;
    pending_retries = 0U;
    /* 戻り値は 0 以上 (ワークキューが停止しているときだけ, 負). 実行中の依頼は重ねない */
    (void)k_work_schedule(&connect_work, K_NO_WAIT);
}
