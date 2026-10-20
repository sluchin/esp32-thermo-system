# cloud.c の解析

対象: `app/common/cloud.c` (743 行) と `cloud.h` (55 行)

AWS IoT Core へ、WiFi と MQTT over TLS で値を送るコードです。ゲートウェイと、ゲートウェイにつながらないときのノード (`direct.c`) が、共通で使います。

呼び出し側は、値を **キューに入れるだけ** (待たずに戻る)。WiFi の接続、時刻の同期、TLS と MQTT の接続、publish、再接続は、専用のスレッド (`cloud`) が、全て受け持ちます。

## 公開する関数 (cloud.h)

| 関数 | 役割 |
|:---|:---|
| `cloud_init(start_enabled)` | 送信のキューを初期化し、設定を読み込み、WiFi を準備して、送信のスレッドを作る |
| `cloud_publish_temperature()` | Thermo ノードの温度と湿度と気圧をキューに入れる (時刻は、受信したとき) |
| `cloud_publish_temperature_at()` | 測った UNIX 時刻を指定して、キューに入れる (SD カードの記録用) |
| `cloud_publish_switchbot()` | SwitchBot の値をキューに入れる |
| `cloud_set_enabled()` | 送信する状態にする、または止めて WiFi を切る |
| `cloud_set_attempt_limit()` | 接続の失敗が続いたとき、あきらめる回数 |
| `cloud_has_given_up()` | あきらめたか |
| `cloud_is_connected()` | MQTT でつながっているか |
| `cloud_pending()` | キューに溜まっている数 |
| `cloud_sent_count()` | publish に成功した回数 (起動からの合計) |
| `cloud_reconnect()` | 設定を変えたあとに、接続をやり直す |
| `cloud_step()` | 接続と送信を 1 回分行う (スレッドが繰り返し呼ぶ。単体テストが直接呼ぶ) |
| `cloud_stop()` | スレッドを止める (単体テスト用) |

## 定数

| 定数 | 値 | 意味 |
|:---|:---|:---|
| `BROKER_PORT` | 8883 | AWS IoT Core の MQTT (TLS、クライアント証明書による相互認証) |
| `KEEPALIVE_S` | 60 | MQTT の keep alive (AWS IoT Core の許す範囲は 30 から 1200) |
| `WAIT_CONFIG_S` | 5 | 設定が揃うのを待つ間隔 |
| `WIFI_TIMEOUT_S` | 30 | WiFi の接続を待つ時間 |
| `NTP_TIMEOUT_MS` | 5000 | NTP の応答を待つ時間 |
| `CONNACK_TIMEOUT_MS` | 10000 | MQTT の CONNACK を待つ時間 |
| `SESSION_TICK_MS` | 1000 | 送るものがないときに、受信と keep alive を確認する間隔 |
| `RETRY_MIN_S`、`RETRY_MAX_S` | 5、60 | 接続の再試行の間隔 (失敗のたびに 2 倍。上限 60 秒) |
| `QUEUE_LEN` | 16 | 送信を待てる値の数 |
| `TOPIC_SIZE`、`PAYLOAD_SIZE` | 128、176 | トピックとペイロードのバッファ。`PAYLOAD_SIZE` は 164 以上 (`BUILD_ASSERT`。Thermo ノードの最も長いペイロードが 163 文字 + NUL) |
| `MQTT_BUF_SIZE` | 512 | MQTT の送受信バッファ |
| `THREAD_STACK_SIZE`、`THREAD_PRIORITY` | 6144、7 | スレッド (TLS のハンドシェイクを含むため、大きい) |

## 送信を待つ値 (struct sample)

| メンバー | 内容 |
|:---|:---|
| `addr` | 送ってきた機器のアドレス |
| `kind` | `SAMPLE_THERMO` か `SAMPLE_SWITCHBOT` (`uint8_t`) |
| `uptime_ms` | 受信したときの稼働時間 |
| `temp_x10`、`humidity_x10`、`pressure_x10` | Thermo の値 |
| `switchbot` | SwitchBot の値 |
| `unix_s` | 測った UNIX 時刻。`UNIX_FROM_UPTIME` (-1) なら、publish のときに求める |

`struct sample` は、キューに `QUEUE_LEN` (16) 件を持つので、詰め物が少なくなる順に並べて、32 バイトにしています (`BUILD_ASSERT`)。`kind` を `enum` (4 バイト) にすると、40 バイトになり、キューのバッファが 128 バイト増えて、ノードの RAM (`dram0_0_seg`) に入りません。

キューは `static struct k_msgq cloud_sample_q` (`cloud.c` の中だけで使う) で、先入れ先出しです。バッファ `cloud_sample_buf` と合わせて `static` で宣言し、`cloud_init()` の最初に `k_msgq_init()` で初期化します (ほかの `cloud_*` の関数より先に、`cloud_init()` を呼びます)。

## スレッドの動き (cloud_step)

`cloud_thread_entry()` が `cloud_step()` を、永久に繰り返します。1 回の `cloud_step()` は、次の順です。

```text
enabled でない        → WiFi が上がっていれば切る。1 秒待って -EAGAIN
設定が揃っていない     → 5 秒待って -EAGAIN          (cfg_is_complete())
reconnect_requested = false
WiFi に接続 (30 秒)
    成功なら:
        NTP で時刻を同期 (5 秒)。失敗は警告だけ
        同期したことがある → connect_broker()
        一度も同期していない → -ETIME           ← TLS の証明書の有効期限の確認に、時計が要る
失敗なら:
    attempts++
    上限 (max_attempts) に達した → あきらめる: gave_up = true、enabled = false、WiFi を切って返る
    それ以外 → retry_s 秒待つ。retry_s を 2 倍 (上限 60)
成功なら:
    retry_s = 5、attempts = 0
    run_session()                        ← 接続が切れるまで送る
    止められた (enabled = false) → WiFi を切る
    エラーで戻った → 5 秒待つ (すぐに失敗を繰り返さない)
```

- **時計の同期が必須**です。mbedTLS が、サーバの証明書の有効期限をシステム時計で確認するので、時計が合っていないと、TLS は必ず失敗します。そのため、`ntp_sync()` に一度も成功していなければ、接続せずに `-ETIME` でやり直します。同期したことがあれば、今回の同期に失敗しても (時計は進んでいるので) 接続を続けます。
- **あきらめる**のは、`max_attempts` が 0 でなく、WiFi、NTP、TLS、MQTT のどこかの失敗が、その回数続いたときです。ゲートウェイは 0 (あきらめない)。ノードは `DIRECT_ATTEMPT_LIMIT` (5) に設定します。
- 失敗したときの待ち時間 (`retry_s`) は、失敗のたびに 2 倍になり (5、10、20、40、60 秒)、成功で 5 秒に戻ります。

## cloud_init

1. `k_msgq_init()` で、送信のキュー (`cloud_sample_q`) を初期化します。ほかの処理より先にするので、このあとで失敗して戻っても、キューは使えます。
2. `cfg_init()` で、フラッシュから設定を読み込みます。
3. `enabled = start_enabled`、`gave_up = false`、`attempts = 0`。
4. `wifi_link_init()` で、WiFi のイベントのコールバックを登録します。
5. スレッドを作ります (名前は `cloud`)。

ほかの `cloud_*` の関数より先に、1 回だけ呼びます (キューを初期化するため)。

`start_enabled` は、ゲートウェイでは `true` (すぐに接続)、ノードでは `false` (`cloud_set_enabled(true)` まで待つ) です。

## cloud_set_enabled

| 引数 | 動き |
|:---|:---|
| `true` | `attempts` と `gave_up` を戻す。**キューの古い値を、全て捨てる** (`k_msgq_purge`)。`enabled = true` |
| `false` | `enabled = false`、`reconnect_requested = true`。実行中のセッションは、1 秒以内に終わり、MQTT を閉じて、WiFi を切る |

## MQTT の接続 (connect_broker)

```text
resolve_broker(endpoint)        ← zsock_getaddrinfo (IPv4、TCP、ポート 8883)
mqtt_client_init() で作り直す
    MQTT 3.1.1、clean_session = 1、keepalive = 60 秒
    クライアント ID = 設定の client_id
    トランスポート: MQTT_TRANSPORT_SECURE
    TLS: peer_verify = REQUIRED、hostname = endpoint、sec_tag = CFG_TLS_SEC_TAG (1)
mqtt_connect()                  ← TCP、TLS のハンドシェイク、CONNECT の送信まで
poll_input(10 秒)               ← CONNACK を待つ。mqtt_up が true になれば成功
    失敗 → mqtt_abort() して -ECONNREFUSED など
```

CA、クライアント証明書、秘密鍵は、`cfg.c` が TLS の認証情報 (セキュリティタグ 1) に登録したものを使います。

## セッション (run_session)

```text
while (reconnect_requested でない):
    キューから 1 件取る (最大 1 秒待つ)
        あれば publish_sample()。失敗 → ループを抜ける
        成功 → sent_count++
    poll_input(0)                 ← PUBACK、サーバの切断を処理。切断 → -ECONNRESET
    mqtt_live()                   ← keep alive。-EAGAIN は正常 (まだ時間が来ていない)
終了: mqtt_up なら mqtt_disconnect()、mqtt_abort()、mqtt_up = false
```

### publish_sample

1. UNIX 時刻を決める。`unix_s` が 0 以上ならそれ、`UNIX_FROM_UPTIME` なら `received_unix_time()` (今の UNIX 時刻 - 受信してからの経過)。
2. 値の種類で、`payload.c` の関数を選び、トピックとペイロードを作る ([payload.md](payload.md))。
3. トピックが作れなければ (`topic_len < 0`)、`-ENOSPC`。
4. メッセージ ID を 1 から 65535 の順に進めます (QoS 1 では 0 を使えない)。
5. QoS 1、retain なしで、`mqtt_publish()`。

`received_unix_time()` は、受信したときではなく、publish するときに計算します。受信のときには、時計が合っていないことがあるためです (接続の前に必ず同期するので、publish のときは、合っています)。経過は 32 ビットの符号なしの引き算なので、稼働時間が一周 (約 49 日) しても正しく求まります。

### sent_count

`mqtt_publish()` が成功するたびに増えます。ノードの `direct.c` は、この増え方で、SD カードの記録を「送り終えた」ことにします。**PUBACK の受信ではなく、publish の送信に成功した時点で数えます** (PUBACK のイベントは、デバッグのログに出すだけです)。

## キューへの追加 (enqueue_sample)

- `enabled` でなければ、`-ENOTCONN`。
- キューが満杯なら、新しい値を捨てて、`-ENOMSG` (警告 `Send queue is full, the value was dropped`)。
- それ以外は、成功。

`cloud_publish_*()` は、Bluetooth のスレッドからも呼ばれるので、`K_NO_WAIT` です。

## cloud_reconnect

設定を変えたあとに呼びます (`thermo apply`)。`reconnect_requested = true` にして、`wifi_link_disconnect()` で WiFi を切ります。実行中のセッションは、WiFi が切れて終わります。次の `cloud_step()` が、新しい設定でつなぎ直します。

## 気づいた点

- 送信の確認は、PUBACK を待たない。QoS 1 でも、`sent_count` は publish の送信時点で増えます。直後に接続が切れると、ブローカーに届いていない値が、送信済みとして数えられ、ノードの SD カードで「送信済み」にされます。
- 1 件の publish が失敗すると、その値は、すでにキューから取り出されているので、失われます。セッションは終わり、再接続します。
- `cloud_set_enabled(true)` は、キューを空にします。ノードが WiFi を始めるとき、それ以前にキューに入れた値は、捨てられます。
- `mqtt_up` と `reconnect_requested` は `volatile bool` で、アトミック操作や排他制御はありません。Bluetooth のスレッド、メインスレッド、シェルのスレッド、`cloud` スレッドから触ります。1 バイトの読み書きなので、値が壊れることはありません。
- トピックには、クライアント ID をそのまま入れます。`+`、`#`、`/` が含まれると、トピックの階層が変わります (AWS IoT Core のポリシーも、それに合わせる必要があります)。

## 関連

- [cfg.md](cfg.md): 設定と証明書
- [wifi_link.md](wifi_link.md)、[ntp.md](ntp.md): WiFi と時刻
- [payload.md](payload.md): トピックとペイロード
- [index.md](index.md): このディレクトリの文書の一覧
