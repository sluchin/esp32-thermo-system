/*
 * Copyright (c) 2026 Tetsuya Higashi
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief AWS IoT Core への、MQTT (TLS) による温度の送信
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "cloud.h"
#include "ntp.h"
#include "payload.h"
#include "wifi_link.h"

LOG_MODULE_REGISTER(thermo_cloud);

/** AWS IoT Core の MQTT (TLS、クライアント証明書による相互認証) のポート */
#define BROKER_PORT        "8883"
/** MQTT の keep alive の間隔 [s] (AWS IoT Core は 30 .. 1200) */
#define KEEPALIVE_S        60u
/** 設定が揃うのを待つ間隔 [s] */
#define WAIT_CONFIG_S      5
/** WiFi の接続を待つ時間 [s] */
#define WIFI_TIMEOUT_S     30
/** NTP の応答を待つ時間 [ms] */
#define NTP_TIMEOUT_MS     5000
/** MQTT の CONNACK を待つ時間 [ms] */
#define CONNACK_TIMEOUT_MS 10000
/** 送信するものがないときに、受信と keep alive を確認する間隔 [ms] */
#define SESSION_TICK_MS    1000
/** 接続の再試行の間隔の、最小値 [s] */
#define RETRY_MIN_S        5u
/** 接続の再試行の間隔の、最大値 [s] */
#define RETRY_MAX_S        60u
/** 送信を待つ温度の最大数 */
#define QUEUE_LEN          16
/** トピックのバッファのサイズ */
#define TOPIC_SIZE         128u
/** ペイロードのバッファのサイズ */
#define PAYLOAD_SIZE       160u
/*
 * ペイロードの最大長が、入ること (SwitchBot の、機器のアドレス、-3276.7 ℃、湿度 100 %、
 * 電池 100 %、稼働時間が最大で、UNIX 時刻が 19 桁の場合は、155 文字 + NUL)
 */
BUILD_ASSERT(PAYLOAD_SIZE >= 156u, "The payload buffer is too small");
/** MQTT の送受信バッファのサイズ */
#define MQTT_BUF_SIZE     512u
/** スレッドのスタックサイズ (TLS のハンドシェイクを含む) */
#define THREAD_STACK_SIZE 6144
/** スレッドの優先度 */
#define THREAD_PRIORITY   7

/** 送信を待つ値の種類 */
enum sample_kind {
    SAMPLE_THERMO,   /**< Thermo ノードの温度 (ADC の生値) */
    SAMPLE_SWITCHBOT /**< SwitchBot の温湿度計の値 */
};

/** 送信を待つ値 1 件 */
struct sample {
    enum sample_kind kind;             /**< 値の種類 */
    bt_addr_le_t addr;                 /**< 送ってきた機器のアドレス */
    uint32_t uptime_ms;                /**< 受信したときの、ゲートウェイの稼働時間 [ms] */
    uint16_t raw;                      /**< 温度 (ADC の生値。SAMPLE_THERMO のとき) */
    struct switchbot_sample switchbot; /**< SwitchBot の値 (SAMPLE_SWITCHBOT のとき) */
};

/** 送信を待つ温度のキュー */
K_MSGQ_DEFINE(cloud_sample_q, sizeof(struct sample), QUEUE_LEN, 4);

/** MQTT クライアント */
static struct mqtt_client client;
/** ブローカーのアドレス (DNS で解決した結果) */
static struct sockaddr_storage broker;
/** MQTT の受信バッファ */
static uint8_t rx_buf[MQTT_BUF_SIZE];
/** MQTT の送信バッファ */
static uint8_t tx_buf[MQTT_BUF_SIZE];
/** TLS の証明書のセキュリティタグ */
static const sec_tag_t sec_tags[] = {CFG_TLS_SEC_TAG};

/** MQTT で接続している状態か (CONNACK を受け取ってから、DISCONNECT を受け取るまで) */
static volatile bool mqtt_up;
/** 設定が変わったので、接続をやり直す依頼があるか */
static volatile bool reconnect_requested;
/** 次に失敗したときの、再試行までの間隔 [s] */
static unsigned int retry_s = RETRY_MIN_S;
/** 次に publish するメッセージの ID (1 .. 65535) */
static uint16_t message_id;

/** 送信のスレッド */
static struct k_thread cloud_thread;
/** 送信のスレッドのスタック */
static K_THREAD_STACK_DEFINE(cloud_stack, THREAD_STACK_SIZE)

        /**
         * MQTT のイベントのコールバック
         *
         * @param[in] c   MQTT クライアント (使用しない)
         * @param[in] evt イベント
         */
        static void mqtt_evt_handler(struct mqtt_client *c, const struct mqtt_evt *evt)
{
    ARG_UNUSED(c);

    /* 使うのは、この 3 つだけ (購読は、しない) */
    if (evt->type == MQTT_EVT_CONNACK) {
        if (evt->result == 0) {
            mqtt_up = true;
        } else {
            LOG_ERR("MQTT connection refused (result %d)", evt->result);
        }
    } else if (evt->type == MQTT_EVT_DISCONNECT) {
        LOG_WRN("MQTT disconnected");
        mqtt_up = false;
    } else if (evt->type == MQTT_EVT_PUBACK) {
        LOG_DBG("Publish acknowledged (id %u)", evt->param.puback.message_id);
    } else {
        LOG_DBG("Ignoring the MQTT event %d", (int)evt->type);
    }
}

/**
 * ブローカー (AWS IoT Core のエンドポイント) の名前を解決する
 *
 * @param[in] host エンドポイント
 * @retval EXIT_SUCCESS 成功 (broker に、アドレスを保存する)
 * @retval -EHOSTUNREACH 名前を解決できなかった
 */
static int resolve_broker(const char *host)
{
    struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct zsock_addrinfo *res = NULL;
    int err = zsock_getaddrinfo(host, BROKER_PORT, &hints, &res);

    if (err != 0) {
        LOG_ERR("Resolving '%s' failed (err %d)", host, err);
        return -EHOSTUNREACH;
    }

    (void)memcpy(&broker, res->ai_addr, res->ai_addrlen);
    zsock_freeaddrinfo(res);
    return EXIT_SUCCESS;
}

/**
 * ソケットを待って、受信したデータがあれば、MQTT の処理をする
 *
 * @param[in] timeout_ms 待つ時間 [ms] (0 なら、待たない)
 * @retval EXIT_SUCCESS データがなかった、または、処理した
 * @retval -ECONNRESET  ソケットにエラーがある
 * @retval negative     poll() または mqtt_input() の失敗 (負の errno)
 */
static int poll_input(int timeout_ms)
{
    struct zsock_pollfd fds = {.fd = client.transport.tls.sock, .events = ZSOCK_POLLIN};
    int ret = zsock_poll(&fds, 1, timeout_ms);

    if (ret < 0) {
        return -errno;
    }
    /* 時間内に、受信したデータがなかった */
    if (ret == 0) {
        return EXIT_SUCCESS;
    }
    /* 切断、エラー: mqtt_input() を呼ぶ前に、検出する */
    if ((fds.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) != 0) {
        return -ECONNRESET;
    }
    return mqtt_input(&client);
}

/**
 * MQTT のブローカーに接続して、CONNACK を待つ
 *
 * @retval EXIT_SUCCESS 接続した
 * @retval -ECONNREFUSED CONNACK が、拒否、または、時間内に来なかった
 * @retval negative     名前の解決、接続、受信の失敗 (負の errno)
 */
static int connect_broker(void)
{
    const char *endpoint = cfg_get(CFG_KEY_ENDPOINT);
    const char *client_id = cfg_get(CFG_KEY_CLIENT_ID);
    struct mqtt_sec_config *tls = &client.transport.tls.config;
    int err = resolve_broker(endpoint);

    if (err != 0) {
        return err;
    }

    /* MQTT クライアントを、設定から作り直す (切断のたびに、初期化する) */
    mqtt_client_init(&client);
    client.broker = &broker;
    client.evt_cb = mqtt_evt_handler;
    client.client_id.utf8 = (const uint8_t *)client_id;
    client.client_id.size = (uint32_t)strlen(client_id);
    client.protocol_version = MQTT_VERSION_3_1_1;
    client.rx_buf = rx_buf;
    client.rx_buf_size = sizeof(rx_buf);
    client.tx_buf = tx_buf;
    client.tx_buf_size = sizeof(tx_buf);
    client.keepalive = KEEPALIVE_S;
    client.clean_session = 1u;

    /* サーバの証明書を確認して、クライアント証明書で、認証される (相互認証) */
    client.transport.type = MQTT_TRANSPORT_SECURE;
    tls->peer_verify = TLS_PEER_VERIFY_REQUIRED;
    tls->cipher_list = NULL;
    tls->sec_tag_list = sec_tags;
    tls->sec_tag_count = ARRAY_SIZE(sec_tags);
    tls->hostname = endpoint;

    /* TCP の接続と、TLS のハンドシェイクと、MQTT の CONNECT の送信まで、ここで行う */
    mqtt_up = false;
    err = mqtt_connect(&client);
    if (err != 0) {
        LOG_ERR("MQTT connect failed (err %d)", err);
        return err;
    }

    /* CONNACK を受信して、MQTT のイベント (mqtt_evt_handler) で、mqtt_up が true になるのを確認する
     */
    err = poll_input(CONNACK_TIMEOUT_MS);
    if ((err != 0) || !mqtt_up) {
        LOG_ERR("MQTT CONNACK not received (err %d)", err);
        (void)mqtt_abort(&client);
        return (err != 0) ? err : -ECONNREFUSED;
    }
    return EXIT_SUCCESS;
}

/**
 * 値を受信したときの UNIX 時刻を求める (今の UNIX 時刻から、受信してからの経過を引く)
 *
 * 受信した時点では、時計が合っていないことがあるので、publish するときに求める
 * (接続の前に、必ず同期している)。
 *
 * @param[in] uptime_ms 受信したときの、ゲートウェイの稼働時間 [ms]
 * @return UNIX 時刻 [s]。時計が合っていなければ、-1
 */
static int64_t received_unix_time(uint32_t uptime_ms)
{
    int64_t now = 0;
    /* 32 bit の稼働時間は、約 49 日で戻るので、符号なしの引き算で、経過を求める */
    uint32_t elapsed_ms = k_uptime_get_32() - uptime_ms;

    if (ntp_unix_time(&now) != EXIT_SUCCESS) {
        return -1;
    }
    return now - (int64_t)(elapsed_ms / 1000u);
}

/**
 * 温度 1 件を、MQTT で publish する (QoS 1)
 *
 * @param[in] s 温度
 * @retval EXIT_SUCCESS 成功
 * @retval negative     失敗 (負の errno)
 */
static int publish_sample(const struct sample *s)
{
    char topic[TOPIC_SIZE] = {0};
    char payload[PAYLOAD_SIZE] = {0};
    struct mqtt_publish_param param = {0};
    const char *client_id = cfg_get(CFG_KEY_CLIENT_ID);
    int64_t unix_s = received_unix_time(s->uptime_ms);
    int topic_len = 0;
    int payload_len = 0;

    /* 値の種類で、トピックとペイロードの形式が違う */
    if (s->kind == SAMPLE_SWITCHBOT) {
        topic_len = payload_format_switchbot_topic(topic, sizeof(topic), client_id, &s->addr);
        payload_len = payload_format_switchbot(payload, sizeof(payload), &s->addr, &s->switchbot,
                                               s->uptime_ms, unix_s);
    } else {
        topic_len = payload_format_topic(topic, sizeof(topic), client_id, &s->addr);
        payload_len = payload_format_temperature(payload, sizeof(payload), &s->addr, s->raw,
                                                 s->uptime_ms, unix_s);
    }

    /* ペイロードは、必ずバッファに入る. トピックは、長いクライアント ID で、入らないことがある */
    if (topic_len < 0) {
        return -ENOSPC;
    }

    /* メッセージ ID は、1 から 65535 を、順に使う (QoS 1 では、0 は使えない) */
    message_id = (uint16_t)((message_id % UINT16_MAX) + 1u);
    param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    param.message.topic.topic.utf8 = (const uint8_t *)topic;
    param.message.topic.topic.size = (uint32_t)topic_len;
    param.message.payload.data = (uint8_t *)payload;
    param.message.payload.len = (uint32_t)payload_len;
    param.message_id = message_id;
    param.dup_flag = 0u;
    param.retain_flag = 0u;
    return mqtt_publish(&client, &param);
}

/**
 * 接続している間、キューの温度を publish して、受信と keep alive を処理する
 *
 * 接続が切れるか、エラーになるか、cloud_reconnect() が呼ばれたら、MQTT を閉じて戻る.
 *
 * @retval EXIT_SUCCESS cloud_reconnect() が呼ばれた
 * @retval negative     接続が切れた、または、エラー (負の errno)
 */
static int run_session(void)
{
    struct sample s = {.kind = SAMPLE_THERMO};
    int err = EXIT_SUCCESS;

    while (!reconnect_requested) {
        /* 送るものがあれば、すぐ送る。なければ、最大 SESSION_TICK_MS 待って、受信などの確認に進む
         */
        if (k_msgq_get(&cloud_sample_q, &s, K_MSEC(SESSION_TICK_MS)) == 0) {
            err = publish_sample(&s);
            if (err != 0) {
                LOG_ERR("Publish failed (err %d)", err);
                break;
            }
        }

        /* 受信 (PUBACK、サーバからの切断など) を処理する。待たない */
        err = poll_input(0);
        if ((err == 0) && !mqtt_up) {
            err = -ECONNRESET;
        }
        if (err != 0) {
            LOG_ERR("MQTT connection lost (err %d)", err);
            break;
        }

        /* keep alive の時間が来ていなければ、-EAGAIN */
        err = mqtt_live(&client);
        if (err == -EAGAIN) {
            err = EXIT_SUCCESS;
        }
        if (err != 0) {
            LOG_ERR("MQTT keep alive failed (err %d)", err);
            break;
        }
    }

    /* サーバが切断していなければ、DISCONNECT を送ってから、ソケットを閉じる */
    if (mqtt_up) {
        (void)mqtt_disconnect(&client, NULL);
    }
    (void)mqtt_abort(&client);
    mqtt_up = false;
    return err;
}

/* 接続と送信を 1 回分行う (スレッドが、繰り返し呼ぶ) */
int cloud_step(void)
{
    int err = EXIT_SUCCESS;

    /* 設定 (WiFi、エンドポイント、証明書) が揃うまで、接続しないで待つ */
    if (!cfg_is_complete()) {
        (void)k_sleep(K_SECONDS(WAIT_CONFIG_S));
        return -EAGAIN;
    }

    /* 接続をやり直す依頼は、これから始める接続で、反映される */
    reconnect_requested = false;
    err = wifi_link_connect(cfg_get(CFG_KEY_SSID), cfg_get(CFG_KEY_PSK), K_SECONDS(WIFI_TIMEOUT_S));
    if (err == 0) {
        /*
         * サーバ証明書の有効期限を確認する (mbedTLS が、システム時計を使う) ので、時計が合って
         * いないと、TLS の接続は、必ず失敗する。1 度も同期していなければ、接続しないで、やり直す。
         * 同期したことがあれば、今回の同期に失敗しても、時計は進んでいるので、接続を続ける。
         */
        if (ntp_sync(CONFIG_THERMO_NTP_SERVER, K_MSEC(NTP_TIMEOUT_MS)) != 0) {
            LOG_WRN("Time synchronization failed");
        }
        if (ntp_is_synced()) {
            err = connect_broker();
        } else {
            err = -ETIME;
        }
    }
    /* 失敗したら、間隔をあけてから、やり直す (間隔は、失敗のたびに倍にして、上限で止める) */
    if (err != 0) {
        LOG_WRN("Connection failed (err %d), retrying in %u s", err, retry_s);
        (void)k_sleep(K_SECONDS(retry_s));
        retry_s = MIN(retry_s * 2u, RETRY_MAX_S);
        return err;
    }

    retry_s = RETRY_MIN_S;
    LOG_INF("Connected to AWS IoT Core");
    err = run_session();
    if (err != 0) {
        (void)k_sleep(K_SECONDS(RETRY_MIN_S)); /* すぐに失敗を繰り返さない */
    }
    return err;
}

/**
 * 送信のスレッドの入口 (cloud_step() を繰り返す)
 *
 * @param[in] p1 使用しない
 * @param[in] p2 使用しない
 * @param[in] p3 使用しない
 */
static void cloud_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        (void)cloud_step();
    }
}

/* 設定を読み込んで、送信のスレッドを開始する */
int cloud_init(void)
{
    int err = cfg_init();

    if (err != 0) {
        return err;
    }

    err = wifi_link_init();
    if (err != 0) {
        return err;
    }

    (void)k_thread_create(&cloud_thread, cloud_stack, K_THREAD_STACK_SIZEOF(cloud_stack),
                          cloud_thread_entry, NULL, NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
    (void)k_thread_name_set(&cloud_thread, "cloud");
    return EXIT_SUCCESS;
}

/* 送信のスレッドを止める */
void cloud_stop(void)
{
    k_thread_abort(&cloud_thread);
}

/**
 * 値を、送信のキューに入れる (待たない)
 *
 * @param[in] s 値
 * @retval EXIT_SUCCESS 成功
 * @retval -ENOMSG      キューが満杯で、値を捨てた
 */
static int enqueue_sample(const struct sample *s)
{
    if (k_msgq_put(&cloud_sample_q, s, K_NO_WAIT) != 0) {
        LOG_WRN("Send queue is full, the value was dropped");
        return -ENOMSG;
    }
    return EXIT_SUCCESS;
}

/* 温度を、送信のキューに入れる (待たない) */
int cloud_publish_temperature(const bt_addr_le_t *addr, uint16_t raw)
{
    struct sample s = {
            .kind = SAMPLE_THERMO, .addr = *addr, .raw = raw, .uptime_ms = k_uptime_get_32()};

    return enqueue_sample(&s);
}

/* SwitchBot の温湿度計の値を、送信のキューに入れる (待たない) */
int cloud_publish_switchbot(const bt_addr_le_t *addr, const struct switchbot_sample *sample)
{
    struct sample s = {.kind = SAMPLE_SWITCHBOT,
                       .addr = *addr,
                       .switchbot = *sample,
                       .uptime_ms = k_uptime_get_32()};

    return enqueue_sample(&s);
}

/* 接続の切断を依頼して、新しい設定で、つなぎ直させる */
void cloud_reconnect(void)
{
    reconnect_requested = true;
    wifi_link_disconnect();
}
