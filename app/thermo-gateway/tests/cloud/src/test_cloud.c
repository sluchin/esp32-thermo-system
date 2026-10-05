/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief cloud.c の単体テスト
 *
 * cfg, wifi_link, MQTT ライブラリ, ソケットの関数を, FFF のモックに置き換えて (payload.c は
 * 本物を使う), 接続から送信までの流れを, cloud_step() を直接呼んで確認する.
 *  - 接続の手順 (WiFi, 名前の解決, TLS の設定, MQTT の接続, CONNACK) と, 各段階の失敗
 *  - 接続中の, 送信 (トピック, ペイロード, QoS, メッセージ ID), 受信, keep alive
 *  - 失敗したときの, 再試行の間隔 (倍々に増やして, 上限で止める)
 *  - 設定の変更 (cloud_reconnect()) による, 接続のやり直し
 *  - 送信のキューと, スレッドの開始
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <errno.h>  /* errno EAGAIN ENOMSG EIO ENOSPC ECONNRESET etc... */
#include <stdlib.h> /* EXIT_SUCCESS strtoll */
#include <string.h> /* memcpy memset strlen strstr */

#include "cfg.h"
#include "cloud.h"
#include "ntp.h"
#include "wifi_link.h"

DEFINE_FFF_GLOBALS

/* FAKE_*: FFF のモック (実物の代わりの関数. 呼ばれた回数と引数を記録する) */
FAKE_VALUE_FUNC(int, cfg_init)
FAKE_VALUE_FUNC(const char *, cfg_get, enum cfg_key)
FAKE_VALUE_FUNC(bool, cfg_is_complete)
FAKE_VALUE_FUNC(int, wifi_link_init)
FAKE_VALUE_FUNC(int, wifi_link_connect, const char *, const char *, k_timeout_t)
FAKE_VOID_FUNC(wifi_link_disconnect)
FAKE_VALUE_FUNC(int, ntp_sync, const char *, k_timeout_t)
FAKE_VALUE_FUNC(bool, ntp_is_synced)
FAKE_VALUE_FUNC(int, ntp_unix_time, int64_t *)
FAKE_VOID_FUNC(mqtt_client_init, struct mqtt_client *)
FAKE_VALUE_FUNC(int, mqtt_connect, struct mqtt_client *)
FAKE_VALUE_FUNC(int, mqtt_publish, struct mqtt_client *, const struct mqtt_publish_param *)
FAKE_VALUE_FUNC(int, mqtt_input, struct mqtt_client *)
FAKE_VALUE_FUNC(int, mqtt_live, struct mqtt_client *)
FAKE_VALUE_FUNC(int, mqtt_abort, struct mqtt_client *)
FAKE_VALUE_FUNC(int, mqtt_disconnect, struct mqtt_client *, const struct mqtt_disconnect_param *)
FAKE_VALUE_FUNC(int, zsock_getaddrinfo, const char *, const char *, const struct zsock_addrinfo *,
                struct zsock_addrinfo **)
FAKE_VOID_FUNC(zsock_freeaddrinfo, struct zsock_addrinfo *)
FAKE_VALUE_FUNC(int, z_impl_zvfs_poll, struct zvfs_pollfd *, int, int)

/** ソケットの代わりの番号 (mqtt_connect() が, 設定する) */
#define FAKE_SOCK      7
/** 送信の記録の最大数 */
#define MAX_PUBLISHED  24u
/** 送信のキューの長さ (cloud.c の QUEUE_LEN) */
#define QUEUE_LEN      16u
/** 再試行の間隔の最小値 [s] (cloud.c の RETRY_MIN_S) */
#define RETRY_MIN_S    5u
/** 時間の判定の許容範囲 [ms] */
#define TIME_MARGIN_MS 200u

/** テストのエンドポイント */
#define ENDPOINT "example-ats.iot.ap-northeast-1.amazonaws.com"

/** mqtt_publish() に渡された内容の記録 */
static struct {
    char topic[160];     /**< トピック */
    char payload[160];   /**< ペイロード */
    uint8_t qos;         /**< QoS */
    uint16_t message_id; /**< メッセージ ID */
} published[MAX_PUBLISHED];

/** テストのノードのアドレス */
static const bt_addr_le_t node = {
    .type = BT_ADDR_LE_PUBLIC,
    .a = {.val = {0x42, 0x00, 0x00, 0x01, 0xAA, 0x00}},
};

/** cfg_get() が返す, クライアント ID (テストが, 長いものに変える) */
static const char *client_id;
/** 名前の解決の結果の代わり */
static struct sockaddr_in broker_addr = {.sin_family = AF_INET};
/** 名前の解決の結果の代わり (struct zsock_addrinfo) */
static struct zsock_addrinfo addrinfo;

/** MQTT のイベントのコールバック (mqtt_connect() が受け取ったもの) */
static mqtt_evt_cb_t evt_cb;
/** mqtt_input() が配るイベントの代わりの, MQTT クライアント */
static struct mqtt_client *evt_client;

/** zsock_getaddrinfo() に渡された hints の ai_family (引数のポインタは, 呼び出しの後は, 無効になる)
 */
static int resolved_family;
/** zsock_poll() に渡されたソケットの番号 (同上) */
static int polled_fd;

/** mqtt_input() の 1 回ぶんの動作 */
struct input_step {
    const struct mqtt_evt *evt; /**< コールバックに渡すイベント (NULL なら, 渡さない) */
    int ret;                    /**< 戻り値 */
};

/** mqtt_input() の動作の台本 (呼ばれた順に使う. 使い切ったら, 何もせず 0 を返す) */
static struct input_step script[4];
/** 台本の長さ */
static size_t script_len;
/** 台本の, 次に使う位置 */
static size_t script_pos;

/** CONNACK (成功) */
static const struct mqtt_evt connack_ok = {.type = MQTT_EVT_CONNACK, .result = 0};
/** CONNACK (拒否) */
static const struct mqtt_evt connack_refused = {.type = MQTT_EVT_CONNACK, .result = 5};
/** DISCONNECT */
static const struct mqtt_evt disconnect_evt = {.type = MQTT_EVT_DISCONNECT, .result = 0};
/** PUBACK */
static const struct mqtt_evt puback_evt = {.type = MQTT_EVT_PUBACK, .result = 0};
/** 使わないイベント (PINGRESP) */
static const struct mqtt_evt pingresp_evt = {.type = MQTT_EVT_PINGRESP, .result = 0};

/** zsock_poll() の, 接続を待つとき (待ち時間が正) の結果 */
static struct {
    int ret;       /**< 戻り値 */
    int err;       /**< ret が負のときの errno */
    short revents; /**< ret が正のときの revents */
} poll_wait;
/** zsock_poll() の, 接続中の確認 (待ち時間が 0) の結果 */
static struct {
    int ret;       /**< 戻り値 */
    int err;       /**< ret が負のときの errno */
    short revents; /**< ret が正のときの revents */
} poll_tick;

/** mqtt_live() が呼ばれた回数 */
static unsigned int live_calls;
/** この回数目の mqtt_live() で, cloud_reconnect() を呼んで, 接続中の処理を終わらせる */
static unsigned int end_after_live_calls;
/** mqtt_live() の戻り値 (接続を終わらせる回以外) */
static int live_return;

/**
 * cfg_get() のモック動作
 *
 * @param[in] key 項目
 * @return テストの設定の値
 */
static const char *fake_cfg_get(enum cfg_key key)
{
    if (key == CFG_KEY_SSID) {
        return "home-ap";
    }
    if (key == CFG_KEY_PSK) {
        return "secret-pass";
    }
    if (key == CFG_KEY_ENDPOINT) {
        return ENDPOINT;
    }
    return client_id;
}

/**
 * zsock_getaddrinfo() のモック動作 (固定の結果を返す)
 *
 * @param[in] host 使用しない
 * @param[in] service 使用しない
 * @param[in] hints 使用しない
 * @param[out] res 結果
 * @return 0
 */
static int fake_getaddrinfo(const char *host, const char *service,
                            const struct zsock_addrinfo *hints, struct zsock_addrinfo **res)
{
    ARG_UNUSED(host);
    ARG_UNUSED(service);
    resolved_family = hints->ai_family;
    *res = &addrinfo;
    return 0;
}

/**
 * mqtt_connect() のモック動作 (イベントのコールバックとクライアントを記録して, ソケットを設定する)
 *
 * @param[in,out] client MQTT クライアント
 * @return 0
 */
static int fake_mqtt_connect(struct mqtt_client *client)
{
    evt_cb = client->evt_cb;
    evt_client = client;
    client->transport.tls.sock = FAKE_SOCK;
    return 0;
}

/**
 * zvfs_poll() (zsock_poll() の実体) のモック動作 (待ち時間が正か 0 かで, 結果を変える)
 *
 * @param[in,out] fds 監視するソケット
 * @param[in] nfds 使用しない
 * @param[in] timeout 待ち時間 [ms]
 * @return poll_wait または poll_tick の戻り値
 */
static int fake_poll(struct zvfs_pollfd *fds, int nfds, int timeout)
{
    ARG_UNUSED(nfds);

    polled_fd = fds->fd;
    if (timeout > 0) {
        fds->revents = poll_wait.revents;
        errno = poll_wait.err;
        return poll_wait.ret;
    }
    fds->revents = poll_tick.revents;
    errno = poll_tick.err;
    return poll_tick.ret;
}

/**
 * mqtt_input() のモック動作 (台本の次の動作を行う)
 *
 * @param[in] client 使用しない
 * @return 台本の戻り値. 台本を使い切っていたら 0
 */
static int fake_mqtt_input(struct mqtt_client *client)
{
    struct input_step step = {0}; /* 入力の手順 */

    ARG_UNUSED(client);
    if (script_pos >= script_len) {
        return 0;
    }
    step = script[script_pos];
    script_pos++;
    if (step.evt != NULL) {
        evt_cb(evt_client, step.evt);
    }
    return step.ret;
}

/**
 * mqtt_input() の台本を設定する
 *
 * @param[in] steps 動作 (呼ばれた順)
 * @param[in] count 動作の数
 */
static void set_script(const struct input_step *steps, size_t count)
{
    zassert_true(count <= ARRAY_SIZE(script));
    (void)memcpy(script, steps, count * sizeof(steps[0]));
    script_len = count;
    script_pos = 0u;
}

/**
 * mqtt_publish() のモック動作 (内容を記録する)
 *
 * @param[in] client 使用しない
 * @param[in] param 送信するメッセージ
 * @return 0
 */
static int fake_mqtt_publish(struct mqtt_client *client, const struct mqtt_publish_param *param)
{
    unsigned int i = mqtt_publish_fake.call_count - 1u; /* ループ用の添字 */

    ARG_UNUSED(client);
    zassert_true(i < MAX_PUBLISHED);
    (void)memcpy(published[i].topic, param->message.topic.topic.utf8,
                 param->message.topic.topic.size);
    published[i].topic[param->message.topic.topic.size] = '\0';
    (void)memcpy(published[i].payload, param->message.payload.data, param->message.payload.len);
    published[i].payload[param->message.payload.len] = '\0';
    published[i].qos = param->message.topic.qos;
    published[i].message_id = param->message_id;
    return 0;
}

/**
 * mqtt_live() のモック動作 (指定の回数目に, 接続をやり直す依頼を出して, 接続中の処理を終わらせる)
 *
 * @param[in] client 使用しない
 * @return live_return
 */
static int fake_mqtt_live(struct mqtt_client *client)
{
    ARG_UNUSED(client);
    live_calls++;
    if (live_calls >= end_after_live_calls) {
        cloud_reconnect();
    }
    return live_return;
}

/**
 * モックの動作を, 既定 (全て成功. 接続したら, 1 回の確認で終わる) にする
 */
static void setup_defaults(void)
{
    RESET_FAKE(cfg_init);
    RESET_FAKE(cfg_get);
    RESET_FAKE(cfg_is_complete);
    RESET_FAKE(wifi_link_init);
    RESET_FAKE(wifi_link_connect);
    RESET_FAKE(wifi_link_disconnect);
    RESET_FAKE(ntp_sync);
    RESET_FAKE(ntp_is_synced);
    RESET_FAKE(ntp_unix_time);
    RESET_FAKE(mqtt_client_init);
    RESET_FAKE(mqtt_connect);
    RESET_FAKE(mqtt_publish);
    RESET_FAKE(mqtt_input);
    RESET_FAKE(mqtt_live);
    RESET_FAKE(mqtt_abort);
    RESET_FAKE(mqtt_disconnect);
    RESET_FAKE(zsock_getaddrinfo);
    RESET_FAKE(zsock_freeaddrinfo);
    RESET_FAKE(z_impl_zvfs_poll);
    FFF_RESET_HISTORY();

    cfg_get_fake.custom_fake = fake_cfg_get;
    cfg_is_complete_fake.return_val = true;
    ntp_is_synced_fake.return_val = true;
    ntp_unix_time_fake.return_val = -EAGAIN;
    zsock_getaddrinfo_fake.custom_fake = fake_getaddrinfo;
    mqtt_connect_fake.custom_fake = fake_mqtt_connect;
    z_impl_zvfs_poll_fake.custom_fake = fake_poll;
    mqtt_input_fake.custom_fake = fake_mqtt_input;
    mqtt_publish_fake.custom_fake = fake_mqtt_publish;
    mqtt_live_fake.custom_fake = fake_mqtt_live;

    client_id = "gateway-01";
    addrinfo.ai_family = AF_INET;
    addrinfo.ai_addr = (struct sockaddr *)&broker_addr;
    addrinfo.ai_addrlen = sizeof(broker_addr);
    set_script((const struct input_step[]){{&connack_ok, 0}}, 1u);
    poll_wait.ret = 1;
    poll_wait.err = 0;
    poll_wait.revents = ZSOCK_POLLIN;
    poll_tick.ret = 0;
    poll_tick.err = 0;
    poll_tick.revents = 0;
    live_calls = 0u;
    end_after_live_calls = 1u;
    live_return = -EAGAIN;
    (void)memset(published, 0, sizeof(published));
}

/**
 * 接続中の処理を, 指定の回数の確認 (mqtt_live()) で終わらせる準備をして, 次の接続に備える
 *
 * @param[in] ticks 確認の回数
 */
static void connect_and_run_for(unsigned int ticks)
{
    set_script((const struct input_step[]){{&connack_ok, 0}}, 1u);
    live_calls = 0u;
    end_after_live_calls = ticks;
}

/**
 * 各テストの前に, モックを既定にして, 再試行の間隔を最小に戻す
 *
 * 再試行の間隔は, cloud.c の static な状態なので, 接続に成功して, 戻す.
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    setup_defaults();
    zassert_equal(cloud_step(), EXIT_SUCCESS);
    setup_defaults();
}

/**
 * 失敗した接続を再試行するまでに, cloud_step() が待った時間を測る
 *
 * @return 待った時間 [ms]
 */
static uint32_t step_failure_duration(void)
{
    uint32_t start = k_uptime_get_32(); /* 開始時刻 [ms] */

    zassert_not_equal(cloud_step(), EXIT_SUCCESS);
    return k_uptime_get_32() - start;
}

/**
 * 送信のキューを, 空にする (空になるまで, 接続中の処理を回して, publish させる)
 */
static void drain_queue(void)
{
    connect_and_run_for(QUEUE_LEN + 1u);
    zassert_equal(cloud_step(), EXIT_SUCCESS);
}

/** 設定が揃っていなければ, 接続せずに, 待って, -EAGAIN */
ZTEST(cloud, test_step_waits_for_config)
{
    uint32_t start = k_uptime_get_32(); /* 開始時刻 [ms] */

    cfg_is_complete_fake.return_val = false;

    /* 期待: 設定が揃うまで, 5 秒待って -EAGAIN (WiFi には, つながない) */
    zassert_equal(cloud_step(), -EAGAIN);
    zassert_true((k_uptime_get_32() - start) >= 5000u);
    zassert_equal(wifi_link_connect_fake.call_count, 0u);
}

/** 接続の手順: WiFi, 名前の解決 (ポート 8883), TLS の設定, MQTT の接続 */
ZTEST(cloud, test_step_connects)
{
    const struct mqtt_client *c = NULL; /* MQTT クライアント */

    zassert_equal(cloud_step(), EXIT_SUCCESS);

    /* 1. WiFi に, 設定の SSID とパスワードで接続する */
    zassert_equal(wifi_link_connect_fake.call_count, 1u);
    zassert_str_equal(wifi_link_connect_fake.arg0_val, "home-ap");
    zassert_str_equal(wifi_link_connect_fake.arg1_val, "secret-pass");
    /* 2. エンドポイントの名前を, IPv4 で, ポート 8883 として解決する */
    zassert_equal(zsock_getaddrinfo_fake.call_count, 1u);
    zassert_str_equal(zsock_getaddrinfo_fake.arg0_val, ENDPOINT);
    zassert_str_equal(zsock_getaddrinfo_fake.arg1_val, "8883");
    zassert_equal(resolved_family, AF_INET);
    zassert_equal(zsock_freeaddrinfo_fake.call_count, 1u);
    zassert_equal(mqtt_client_init_fake.call_count, 1u);
    zassert_equal(mqtt_connect_fake.call_count, 1u);

    /* 3. MQTT クライアントの設定: クライアント ID, keep alive, TLS (サーバの証明書を必ず確認する)
     */
    c = mqtt_connect_fake.arg0_val;
    zassert_equal(c->protocol_version, MQTT_VERSION_3_1_1);
    zassert_equal(c->client_id.size, strlen("gateway-01"));
    zassert_mem_equal(c->client_id.utf8, "gateway-01", strlen("gateway-01"));
    zassert_equal(c->keepalive, 60u);
    zassert_equal(c->clean_session, 1u);
    zassert_not_null(c->rx_buf);
    zassert_not_null(c->tx_buf);
    zassert_equal(c->transport.type, MQTT_TRANSPORT_SECURE);
    zassert_equal(c->transport.tls.config.peer_verify, TLS_PEER_VERIFY_REQUIRED);
    zassert_equal(c->transport.tls.config.sec_tag_count, 1u);
    zassert_equal(c->transport.tls.config.sec_tag_list[0], CFG_TLS_SEC_TAG);
    zassert_str_equal(c->transport.tls.config.hostname, ENDPOINT);
}

/** 接続したら, CONNACK を待って (待ち時間のあるソケットの確認), 接続中の処理に進む */
ZTEST(cloud, test_step_waits_for_connack)
{
    /* 期待: CONNACK を待つ poll (待ち時間あり) のあと, 接続中の確認 (mqtt_live) に進む */
    zassert_equal(cloud_step(), EXIT_SUCCESS);

    zassert_equal(polled_fd, FAKE_SOCK);
    zassert_true(z_impl_zvfs_poll_fake.arg2_history[0] > 0);
    zassert_equal(mqtt_input_fake.call_count, 1u);
    zassert_equal(mqtt_live_fake.call_count, 1u);
}

/** 接続中は, キューの温度を, トピックとペイロードにして, QoS 1 で publish する */
ZTEST(cloud, test_session_publishes_sample)
{
    zassert_equal(cloud_publish_temperature(&node, 2568u), EXIT_SUCCESS);

    zassert_equal(cloud_step(), EXIT_SUCCESS);

    /* トピックは, クライアント ID とノードのアドレス. ペイロードは, アドレス, 生値, 稼働時間 */
    zassert_equal(mqtt_publish_fake.call_count, 1u);
    zassert_str_equal(published[0].topic, "thermo/gateway-01/00:AA:01:00:00:42/temperature");
    zassert_not_null(strstr(published[0].payload, "\"node\":\"00:AA:01:00:00:42\""));
    zassert_not_null(strstr(published[0].payload, "\"raw\":2568,"));
    zassert_not_null(strstr(published[0].payload, "\"uptime_ms\":"));
    zassert_equal(published[0].qos, MQTT_QOS_1_AT_LEAST_ONCE);
    zassert_not_equal(published[0].message_id, 0u);
}

/**
 * ntp_unix_time() のモック動作 (固定の UNIX 時刻を返す)
 *
 * @param[out] sec UNIX 時刻 [s]
 * @return 0
 */
static int fake_unix_time(int64_t *sec)
{
    *sec = 1790000000;
    return 0;
}

/** 時計が合っていれば, 受信した時刻 (今の UNIX 時刻から, 経過を引いた値) を, payload に入れる */
ZTEST(cloud, test_session_publishes_timestamp)
{
    const char *p = NULL; /* 見つかった位置 */
    long long ts = 0;

    ntp_unix_time_fake.custom_fake = fake_unix_time;
    zassert_equal(cloud_publish_temperature(&node, 2568u), EXIT_SUCCESS);

    zassert_equal(cloud_step(), EXIT_SUCCESS);

    zassert_equal(mqtt_publish_fake.call_count, 1u);
    p = strstr(published[0].payload, "\"timestamp\":");
    zassert_not_null(p);
    ts = strtoll(p + strlen("\"timestamp\":"), NULL, 10);
    /* 受信から publish までの経過は, テストの中では, 数秒以内 */
    zassert_true((ts <= 1790000000LL) && (ts > 1789999990LL), "timestamp %lld", ts);
}

/** 時計が合っていなければ, "timestamp" を, payload に入れない */
ZTEST(cloud, test_session_omits_timestamp_without_clock)
{
    zassert_equal(cloud_publish_temperature(&node, 2568u), EXIT_SUCCESS);

    zassert_equal(cloud_step(), EXIT_SUCCESS);

    zassert_equal(mqtt_publish_fake.call_count, 1u);
    zassert_is_null(strstr(published[0].payload, "timestamp"));
}

/** 複数の温度は, 順に publish して, メッセージ ID は, 1 ずつ増える */
ZTEST(cloud, test_session_publishes_in_order)
{
    zassert_equal(cloud_publish_temperature(&node, 100u), EXIT_SUCCESS);
    zassert_equal(cloud_publish_temperature(&node, 200u), EXIT_SUCCESS);
    zassert_equal(cloud_publish_temperature(&node, 300u), EXIT_SUCCESS);
    /* 3 回の確認 (mqtt_live) で, 接続中の処理が終わる (1 回の確認で, キューから 1 件送る) */
    connect_and_run_for(3u);

    zassert_equal(cloud_step(), EXIT_SUCCESS);

    /* キューに入れた順に, 送られる */
    zassert_equal(mqtt_publish_fake.call_count, 3u);
    zassert_not_null(strstr(published[0].payload, "\"raw\":100,"));
    zassert_not_null(strstr(published[1].payload, "\"raw\":200,"));
    zassert_not_null(strstr(published[2].payload, "\"raw\":300,"));
    /* メッセージ ID は, 1 ずつ増える */
    zassert_equal(published[1].message_id, published[0].message_id + 1u);
    zassert_equal(published[2].message_id, published[1].message_id + 1u);
}

/** キューが満杯のときは, 新しい温度を捨てて, -ENOMSG */
ZTEST(cloud, test_publish_queue_full)
{
    unsigned int i = 0u; /* ループ用の添字 */

    for (i = 0u; i < QUEUE_LEN; i++) {
        /* 期待: 満杯のキューは, 新しい温度を捨てて -ENOMSG. 残りの 16 件は, あとで送られる */
        zassert_equal(cloud_publish_temperature(&node, (uint16_t)i), EXIT_SUCCESS);
    }
    zassert_equal(cloud_publish_temperature(&node, 999u), -ENOMSG);

    drain_queue();
    zassert_equal(mqtt_publish_fake.call_count, QUEUE_LEN);
}

/** 接続中の処理が終わったら (設定の変更), MQTT に DISCONNECT を送って, 接続を閉じる */
ZTEST(cloud, test_session_closes_connection)
{
    zassert_equal(cloud_step(), EXIT_SUCCESS);

    zassert_equal(mqtt_disconnect_fake.call_count, 1u);
    zassert_equal(mqtt_abort_fake.call_count, 1u);
    zassert_equal(wifi_link_disconnect_fake.call_count, 1u); /* cloud_reconnect() */
}

/** 送信に失敗したら, そのエラーを返して, 接続を閉じる (失敗の直後は, 少し待つ) */
ZTEST(cloud, test_session_publish_failure)
{
    uint32_t start = k_uptime_get_32(); /* 開始時刻 [ms] */

    zassert_equal(cloud_publish_temperature(&node, 1u), EXIT_SUCCESS);
    mqtt_publish_fake.custom_fake = NULL;
    mqtt_publish_fake.return_val = -EIO;

    zassert_equal(cloud_step(), -EIO);

    /* 失敗の直後に, 最小の間隔だけ待つ. 接続は, 閉じる */
    zassert_true((k_uptime_get_32() - start) >= (RETRY_MIN_S * 1000u));
    zassert_equal(mqtt_disconnect_fake.call_count, 1u);
    zassert_equal(mqtt_abort_fake.call_count, 1u);
}

/** トピックやペイロードが, バッファに入らなければ, -ENOSPC (publish しない) */
ZTEST(cloud, test_session_format_failure)
{
    static char long_id[161]; /* 上限を超える長さのクライアント ID */

    (void)memset(long_id, 'x', sizeof(long_id) - 1u);
    client_id = long_id;
    /* 期待: トピックがバッファに入らなければ -ENOSPC で, publish しない */
    zassert_equal(cloud_publish_temperature(&node, 1u), EXIT_SUCCESS);

    zassert_equal(cloud_step(), -ENOSPC);
    zassert_equal(mqtt_publish_fake.call_count, 0u);
}

/** 接続中のソケットの確認 (poll) が失敗したら, その errno を返す */
ZTEST(cloud, test_session_poll_failure)
{
    poll_tick.ret = -1;
    poll_tick.err = EIO;

    /* 期待: 接続中の poll の失敗は, その errno を返して, 接続を閉じる */
    zassert_equal(cloud_step(), -EIO);
    zassert_equal(mqtt_abort_fake.call_count, 1u);
}

/** 接続中のソケットにエラーがあれば (POLLERR など), -ECONNRESET */
ZTEST(cloud, test_session_socket_error)
{
    /* POLLERR, POLLHUP, POLLNVAL は, どれも, 接続が切れたとして扱う */
    poll_tick.ret = 1;
    poll_tick.revents = ZSOCK_POLLERR;
    zassert_equal(cloud_step(), -ECONNRESET);

    connect_and_run_for(1u);
    poll_tick.revents = ZSOCK_POLLHUP;
    zassert_equal(cloud_step(), -ECONNRESET);

    connect_and_run_for(1u);
    poll_tick.revents = ZSOCK_POLLNVAL;
    zassert_equal(cloud_step(), -ECONNRESET);
}

/** 受信の処理 (mqtt_input) が失敗したら, そのエラーを返して, 接続を閉じる */
ZTEST(cloud, test_session_input_failure)
{
    poll_tick.ret = 1;
    poll_tick.revents = ZSOCK_POLLIN;
    set_script((const struct input_step[]){{&connack_ok, 0}, {NULL, -ENOTCONN}}, 2u);

    /* 期待: 受信の処理の失敗を返して, 接続を閉じる (DISCONNECT は, まだ送れる) */
    zassert_equal(cloud_step(), -ENOTCONN);
    zassert_equal(mqtt_disconnect_fake.call_count, 1u);
    zassert_equal(mqtt_abort_fake.call_count, 1u);
}

/** サーバから DISCONNECT を受け取ったら, 接続が切れたとして -ECONNRESET (DISCONNECT は, 送らない)
 */
ZTEST(cloud, test_session_disconnect_event)
{
    poll_tick.ret = 1;
    poll_tick.revents = ZSOCK_POLLIN;
    set_script((const struct input_step[]){{&connack_ok, 0}, {&disconnect_evt, 0}}, 2u);

    /* 期待: サーバの切断は, -ECONNRESET (すでに切れているので, DISCONNECT は送らない) */
    zassert_equal(cloud_step(), -ECONNRESET);
    zassert_equal(mqtt_disconnect_fake.call_count, 0u);
    zassert_equal(mqtt_abort_fake.call_count, 1u);
}

/** PUBACK と, 使わないイベントを受け取っても, 接続中の処理は, 続く */
ZTEST(cloud, test_session_ignores_other_events)
{
    poll_tick.ret = 1;
    poll_tick.revents = ZSOCK_POLLIN;
    set_script((const struct input_step[]){{&connack_ok, 0}, {&puback_evt, 0}, {&pingresp_evt, 0}},
               3u);
    end_after_live_calls = 3u;

    zassert_equal(cloud_step(), EXIT_SUCCESS);
    zassert_equal(mqtt_input_fake.call_count,
                  4u); /* CONNACK, PUBACK, PINGRESP, 台本の外 (3 回目の確認) */
}

/** keep alive が失敗したら (-EAGAIN 以外), そのエラーを返す */
ZTEST(cloud, test_session_keepalive_failure)
{
    live_return = -ENOTCONN;

    /* 期待: keep alive の失敗 (-EAGAIN 以外) は, そのエラーを返して, 接続を閉じる */
    zassert_equal(cloud_step(), -ENOTCONN);
    zassert_equal(mqtt_abort_fake.call_count, 1u);
}

/** WiFi に接続できなければ, そのエラーを返して, 名前の解決には進まない */
ZTEST(cloud, test_step_wifi_failure)
{
    wifi_link_connect_fake.return_val = -ETIMEDOUT;

    /* 期待: WiFi に接続できなければ, 名前の解決には進まない */
    zassert_equal(cloud_step(), -ETIMEDOUT);
    zassert_equal(zsock_getaddrinfo_fake.call_count, 0u);
}

/** 1 度も時刻を同期できていなければ, 接続しない (証明書の有効期限を確認できない) */
ZTEST(cloud, test_step_ntp_failure_never_synced)
{
    /* 1 度も同期していなければ, TLS (証明書の有効期限の確認) は, 必ず失敗するので, 接続しない */
    ntp_sync_fake.return_val = -ETIMEDOUT;
    ntp_is_synced_fake.return_val = false;

    zassert_equal(cloud_step(), -ETIME);
    zassert_equal(ntp_sync_fake.call_count, 1u);
    zassert_equal(mqtt_connect_fake.call_count, 0u);
}

/** 同期したことがあれば, 今回の時刻の同期に失敗しても, 接続する */
ZTEST(cloud, test_step_ntp_failure_already_synced)
{
    /* 同期したことがあれば, 今回の同期に失敗しても, 接続する */
    ntp_sync_fake.return_val = -ETIMEDOUT;

    zassert_equal(cloud_step(), 0);
    zassert_equal(mqtt_connect_fake.call_count, 1u);
}

/** エンドポイントの名前を解決できなければ, -EHOSTUNREACH (MQTT の接続には, 進まない) */
ZTEST(cloud, test_step_resolve_failure)
{
    zsock_getaddrinfo_fake.custom_fake = NULL;
    zsock_getaddrinfo_fake.return_val = -2;

    /* 期待: 名前を解決できなければ -EHOSTUNREACH (MQTT クライアントは, 作らない) */
    zassert_equal(cloud_step(), -EHOSTUNREACH);
    zassert_equal(mqtt_client_init_fake.call_count, 0u);
    zassert_equal(zsock_freeaddrinfo_fake.call_count, 0u);
}

/** MQTT の接続 (TLS のハンドシェイクを含む) に失敗したら, そのエラーを返す */
ZTEST(cloud, test_step_connect_failure)
{
    mqtt_connect_fake.custom_fake = NULL;
    mqtt_connect_fake.return_val = -ECONNREFUSED;

    /* 期待: MQTT の接続の失敗を返す (まだ接続していないので, 閉じる必要はない) */
    zassert_equal(cloud_step(), -ECONNREFUSED);
    zassert_equal(mqtt_input_fake.call_count, 0u);
    zassert_equal(mqtt_abort_fake.call_count, 0u);
}

/** CONNACK が拒否 (認証の失敗など) なら, -ECONNREFUSED で, 接続を閉じる */
ZTEST(cloud, test_step_connack_refused)
{
    set_script((const struct input_step[]){{&connack_refused, 0}}, 1u);

    /* 期待: 拒否の CONNACK は -ECONNREFUSED. 接続を閉じて, 接続中の処理には進まない */
    zassert_equal(cloud_step(), -ECONNREFUSED);
    zassert_equal(mqtt_abort_fake.call_count, 1u);
    zassert_equal(mqtt_live_fake.call_count, 0u);
}

/** CONNACK が時間内に来なければ, -ECONNREFUSED で, 接続を閉じる */
ZTEST(cloud, test_step_connack_timeout)
{
    poll_wait.ret = 0;

    /* 期待: CONNACK が来なければ (poll の時間切れ), -ECONNREFUSED で, 接続を閉じる */
    zassert_equal(cloud_step(), -ECONNREFUSED);
    zassert_equal(mqtt_abort_fake.call_count, 1u);
}

/** CONNACK を待つ間に, poll が失敗, または, ソケットにエラーがあれば, そのエラーを返す */
ZTEST(cloud, test_step_connack_socket_failure)
{
    poll_wait.ret = -1;
    poll_wait.err = ETIMEDOUT;
    /* 期待: poll の失敗 (errno) と, ソケットのエラー (POLLERR) は, どちらも接続を閉じる */
    zassert_equal(cloud_step(), -ETIMEDOUT);

    poll_wait.ret = 1;
    poll_wait.revents = ZSOCK_POLLERR;
    zassert_equal(cloud_step(), -ECONNRESET);
    zassert_equal(mqtt_abort_fake.call_count, 2u);
}

/** CONNACK を待つ間の, 受信の処理 (mqtt_input) が失敗したら, そのエラーを返す */
ZTEST(cloud, test_step_connack_input_failure)
{
    set_script((const struct input_step[]){{NULL, -EIO}}, 1u);

    /* 期待: CONNACK の受信処理の失敗を返して, 接続を閉じる */
    zassert_equal(cloud_step(), -EIO);
    zassert_equal(mqtt_abort_fake.call_count, 1u);
}

/** 接続に失敗するたびに, 再試行までの間隔を 2 倍にして (5, 10, 20, 40, 60 秒), 60 秒で止める */
ZTEST(cloud, test_retry_backoff)
{
    const uint32_t expected_s[] = {5u, 10u, 20u, 40u, 60u, 60u}; /* 期待する待ち時間 [s] */
    size_t i = 0u;                                               /* ループ用の添字 */
    uint32_t elapsed = 0u;                                       /* 経過時間 [ms] */

    wifi_link_connect_fake.return_val = -ETIMEDOUT;
    for (i = 0u; i < ARRAY_SIZE(expected_s); i++) {
        elapsed = step_failure_duration();
        /* 期待: 失敗のたびに, 5, 10, 20, 40, 60 秒と, 間隔が倍になって, 60 秒で止まる */
        zassert_true((elapsed >= (expected_s[i] * 1000u)) &&
                             (elapsed < ((expected_s[i] * 1000u) + TIME_MARGIN_MS)),
                     "retry %zu: %u ms", i, elapsed);
    }
}

/** 接続に成功したら, 再試行の間隔は, 最小に戻る */
ZTEST(cloud, test_retry_backoff_resets_on_success)
{
    uint32_t elapsed = 0u; /* 経過時間 [ms] */

    /* 2 回失敗して, 間隔を 20 秒まで伸ばす */
    wifi_link_connect_fake.return_val = -ETIMEDOUT;
    (void)step_failure_duration();
    (void)step_failure_duration();

    /* 接続に成功すると, 間隔が, 最小に戻る */
    wifi_link_connect_fake.return_val = 0;
    connect_and_run_for(1u);
    zassert_equal(cloud_step(), EXIT_SUCCESS);

    /* 次の失敗の間隔は, 20 秒ではなく, 最小の 5 秒 */
    wifi_link_connect_fake.return_val = -ETIMEDOUT;
    elapsed = step_failure_duration();
    zassert_true(elapsed < ((RETRY_MIN_S * 1000u) + TIME_MARGIN_MS), "%u ms", elapsed);
}

/** 設定を変えたあとの再接続の依頼 (cloud_reconnect) は, WiFi を切断して, 接続中の処理を終わらせる
 */
ZTEST(cloud, test_reconnect)
{
    cloud_reconnect();

    /* 期待: 再接続の依頼は, WiFi を切断する */
    zassert_equal(wifi_link_disconnect_fake.call_count, 1u);
}

/** 初期化: 設定の読み込みに失敗したら, そのエラーを返して, スレッドは開始しない */
ZTEST(cloud, test_init_cfg_failure)
{
    cfg_init_fake.return_val = -EIO;

    /* 期待: 設定の読み込みに失敗したら, WiFi の初期化もスレッドの開始もしない */
    zassert_equal(cloud_init(), -EIO);
    zassert_equal(wifi_link_init_fake.call_count, 0u);
}

/** 初期化: WiFi の初期化に失敗したら, そのエラーを返す */
ZTEST(cloud, test_init_wifi_failure)
{
    wifi_link_init_fake.return_val = -ENODEV;

    /* 期待: WiFi の初期化の失敗を返す */
    zassert_equal(cloud_init(), -ENODEV);
}

/** 初期化に成功したら, スレッドが, 接続の処理 (cloud_step) を繰り返す */
ZTEST(cloud, test_init_starts_thread)
{
    cfg_is_complete_fake.return_val = false; /* 設定が揃うまで, 待つ (接続しない) */

    zassert_equal(cloud_init(), EXIT_SUCCESS);
    k_msleep(100);
    cloud_stop();

    zassert_equal(cfg_init_fake.call_count, 1u);
    zassert_equal(wifi_link_init_fake.call_count, 1u);
    zassert_true(cfg_is_complete_fake.call_count >= 1u);
}

/** SwitchBot の値は, switchbot の階層のトピックに, 温度 (℃), 湿度, 電池残量の JSON で, publish する
 */
ZTEST(cloud, test_session_publishes_switchbot)
{
    /* テスト用のサンプル */
    const struct switchbot_sample sample = {.temp_x10 = -53, .humidity = 55u, .battery = 87};

    zassert_equal(cloud_publish_switchbot(&node, &sample), EXIT_SUCCESS);

    zassert_equal(cloud_step(), EXIT_SUCCESS);

    /* 期待: Thermo ノードの温度とは別のトピックと形式 (QoS 1) */
    zassert_equal(mqtt_publish_fake.call_count, 1u);
    zassert_str_equal(published[0].topic, "thermo/gateway-01/switchbot/00:AA:01:00:00:42");
    zassert_not_null(strstr(published[0].payload, "\"type\":\"switchbot\""));
    zassert_not_null(strstr(published[0].payload, "\"temperature_c\":-5.3,"));
    zassert_not_null(strstr(published[0].payload, "\"humidity\":55,\"battery\":87,"));
    zassert_equal(published[0].qos, MQTT_QOS_1_AT_LEAST_ONCE);
}

/** Thermo ノードの温度と, SwitchBot の値は, 同じキューで, 受け取った順に送る */
ZTEST(cloud, test_session_publishes_mixed_in_order)
{
    /* テスト用のサンプル */
    const struct switchbot_sample sample = {.temp_x10 = 235, .humidity = 55u, .battery = 87};

    zassert_equal(cloud_publish_temperature(&node, 100u), EXIT_SUCCESS);
    zassert_equal(cloud_publish_switchbot(&node, &sample), EXIT_SUCCESS);
    zassert_equal(cloud_publish_temperature(&node, 300u), EXIT_SUCCESS);
    connect_and_run_for(3u);

    zassert_equal(cloud_step(), EXIT_SUCCESS);

    /* 期待: 種類ごとのトピックで, 順番どおり */
    zassert_equal(mqtt_publish_fake.call_count, 3u);
    zassert_not_null(strstr(published[0].topic, "/temperature"));
    zassert_not_null(strstr(published[1].topic, "/switchbot/"));
    zassert_not_null(strstr(published[2].topic, "/temperature"));
}

/** SwitchBot のキューが満杯のときも, 新しい値を捨てて -ENOMSG */
ZTEST(cloud, test_publish_switchbot_queue_full)
{
    /* テスト用のサンプル */
    const struct switchbot_sample sample = {.temp_x10 = 235, .humidity = 55u, .battery = 87};
    unsigned int i = 0u; /* ループ用の添字 */

    for (i = 0u; i < QUEUE_LEN; i++) {
        zassert_equal(cloud_publish_switchbot(&node, &sample), EXIT_SUCCESS);
    }
    zassert_equal(cloud_publish_switchbot(&node, &sample), -ENOMSG);

    drain_queue();
}

/** SwitchBot のトピックが, バッファに入らなければ -ENOSPC で, publish しない */
ZTEST(cloud, test_session_switchbot_format_failure)
{
    static char long_id[161]; /* 上限を超える長さのクライアント ID */
    /* テスト用のサンプル */
    const struct switchbot_sample sample = {.temp_x10 = 235, .humidity = 55u, .battery = 87};

    (void)memset(long_id, 'x', sizeof(long_id) - 1u);
    client_id = long_id;
    zassert_equal(cloud_publish_switchbot(&node, &sample), EXIT_SUCCESS);

    zassert_equal(cloud_step(), -ENOSPC);
    zassert_equal(mqtt_publish_fake.call_count, 0u);
}

ZTEST_SUITE(cloud, NULL, NULL, before, NULL, NULL);
