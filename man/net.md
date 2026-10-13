# ネットワーク

ネットワーク (`zephyr/net/*.h`) の、使っている API。WiFi の接続、名前解決とソケット、SNTP、TLS の認証情報、MQTT。AWS IoT Core には、TLS の MQTT で発行する。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

## WiFi とネットワーク管理

### net_if_get_default

```c
struct net_if *net_if_get_default(void);
```

既定のネットワークインターフェースを返す。

- 戻り値: 既定のインターフェース。インターフェースが 1 つもなければ `NULL`。

使用箇所: `app/common/wifi_link.c:73` (ほか 1 行)

### net_mgmt

```c
#define net_mgmt(_mgmt_request, _iface, _data, _len)
```

ネットワーク管理の要求を出す (イベントを生成する)。WiFi の接続や切断は、これに `NET_REQUEST_WIFI_CONNECT` `NET_REQUEST_WIFI_DISCONNECT` を渡して要求する。

- `_mgmt_request`: 要求の識別子。`_iface`: ネットワークインターフェース。`_data` / `_len`: 追加のデータと、その長さ (接続は `struct wifi_connect_req_params`。切断は `NULL` と 0)。
- 戻り値: 成功は 0、失敗は負の errno。

使用箇所: `app/common/wifi_link.c:14` (ほか 2 行)

### net_mgmt_init_event_callback

```c
void net_mgmt_init_event_callback(struct net_mgmt_event_callback *cb, net_mgmt_event_handler_t handler, uint64_t mgmt_event_mask);
```

`net_mgmt_event_callback` を、正しく初期化する補助関数。

- `cb`: アプリのコールバックの構造体。`handler`: ハンドラ関数。`mgmt_event_mask`: ハンドラが受けるイベントのマスク (`NET_EVENT_WIFI_CONNECT_RESULT` `NET_EVENT_WIFI_DISCONNECT_RESULT` など)。

使用箇所: `app/common/wifi_link.c:47` (ほか 1 行)

### net_mgmt_add_event_callback

```c
void net_mgmt_add_event_callback(struct net_mgmt_event_callback *cb);
```

ユーザーのコールバックを加える。

- `cb`: 加えるコールバックへのポインタ (有効なままにしておく)。

使用箇所: `app/common/wifi_link.c:49` (ほか 1 行)

### wifi_connect_req_params

```c
struct wifi_connect_req_params
```

WiFi の接続の要求に渡すパラメータ (SSID、パスワード、セキュリティの種類、バンドなど)。`NET_REQUEST_WIFI_CONNECT` の `_data` に渡す。

使用箇所: `app/common/wifi_link.c:74`

### NET_REQUEST_WIFI_CONNECT

```c
NET_REQUEST_WIFI_CONNECT
```

WiFi の接続の要求。`net_mgmt()` に渡す。結果は、`NET_EVENT_WIFI_CONNECT_RESULT` で届く。

使用箇所: `app/common/wifi_link.c:101`

### NET_REQUEST_WIFI_DISCONNECT

```c
NET_REQUEST_WIFI_DISCONNECT
```

WiFi の切断の要求。`net_mgmt()` に渡す。結果は、`NET_EVENT_WIFI_DISCONNECT_RESULT` で届く。

使用箇所: `app/common/wifi_link.c:144`

## ソケット

### zsock_getaddrinfo

```c
int zsock_getaddrinfo(const char *host, const char *service, const struct zsock_addrinfo *hints, struct zsock_addrinfo **res);
```

ホスト名を、1 つ以上のネットワークアドレスにする (名前解決)。POSIX.1-2017 の `getaddrinfo()` と同じ。`CONFIG_POSIX_API` が有効なら、`getaddrinfo()` の名前でも使える。

- 戻り値: 成功は 0。失敗は 0 以外 (`EAI_*`)。成功したら、`res` を `zsock_freeaddrinfo()` で解放する。

使用箇所: `app/common/cloud.c:451`, `app/common/ntp.c:139`, `app/thermo-node/src/wifi_time.c:142`

### zsock_freeaddrinfo

```c
void zsock_freeaddrinfo(struct zsock_addrinfo *ai);
```

`zsock_getaddrinfo()` が返した結果を解放する。POSIX.1-2017 の `freeaddrinfo()` と同じ。

使用箇所: `app/common/cloud.c:459`, `app/common/ntp.c:148`, `app/thermo-node/src/wifi_time.c:151`

### zsock_poll

```c
static inline int zsock_poll(struct zsock_pollfd *fds, int nfds, int timeout);
```

複数のソケットのイベントを、まとめて待つ。POSIX.1-2017 の `poll()` と同じ。`CONFIG_POSIX_API` が有効なら、`poll()` の名前でも使える。

- `timeout`: ミリ秒。負の値は、無期限に待つ。
- 戻り値: イベントが起きたソケットの数。時間切れは 0。失敗は -1 (`errno`)。

使用箇所: `app/common/cloud.c:476`

## SNTP (時刻の取得)

### sntp_init

```c
int sntp_init(struct sntp_ctx *ctx, struct sockaddr *addr, socklen_t addr_len);
```

SNTP のコンテキストを初期化する。

- `ctx`: SNTP のコンテキスト。`addr` / `addr_len`: NTP / SNTP サーバの IP アドレスと、その長さ。
- 戻り値: 成功は 0、失敗は負の値。

使用箇所: `app/common/ntp.c:68`, `app/thermo-node/src/wifi_time.c:109`

### sntp_query

```c
int sntp_query(struct sntp_ctx *ctx, uint32_t timeout, struct sntp_time *ts);
```

SNTP の問い合わせをして、時刻を得る。

- `timeout`: 応答を待つ時間 [ミリ秒]。`ts` (出力): 1970-01-01 からの秒 (整数部と小数部)。
- 戻り値: 成功は 0、失敗は負の値 (時間切れは `-ETIMEDOUT`)。

使用箇所: `app/common/ntp.c:74`, `app/thermo-node/src/wifi_time.c:115`

### sntp_close

```c
void sntp_close(struct sntp_ctx *ctx);
```

SNTP のコンテキストを解放する (ソケットを閉じる)。

使用箇所: `app/common/ntp.c:75`, `app/thermo-node/src/wifi_time.c:116`

## TLS の認証情報

### tls_credential_add

```c
int tls_credential_add(sec_tag_t tag, enum tls_credential_type type, const void *cred, size_t credlen);
```

TLS / DTLS の認証に使う、認証情報を加える。

- `tag`: 認証情報を参照するための、セキュリティタグ。`type`: 認証情報の種類 (`TLS_CREDENTIAL_CA_CERTIFICATE` `TLS_CREDENTIAL_PUBLIC_CERTIFICATE` `TLS_CREDENTIAL_PRIVATE_KEY`)。`cred` / `credlen`: 認証情報と、その長さ。
- 戻り値: 成功は 0。`-EACCES`: アクセスが拒否された。`-ENOMEM`: メモリが足りない。`-EEXIST`: そのタグと種類の認証情報が、すでにある。

使用箇所: `app/common/cfg.c:591`

### tls_credential_get

```c
int tls_credential_get(sec_tag_t tag, enum tls_credential_type type, void *cred, size_t *credlen);
```

登録済みの認証情報を取得する。

- `cred`: 認証情報を入れるバッファ。`credlen`: 入力はバッファの大きさ。出力は認証情報の長さ。
- 戻り値: 成功は 0。`-EACCES`: アクセスが拒否された。`-ENOENT`: 見つからない。`-EFBIG`: バッファに入らない (必要な大きさは `*credlen` に入る)。

使用箇所: `app/common/cfg.c:207` (ほか 1 行)

### tls_credential_delete

```c
int tls_credential_delete(sec_tag_t tag, enum tls_credential_type type);
```

登録済みの認証情報を削除する。

- 戻り値: 成功は 0。`-EACCES`: アクセスが拒否された。`-ENOENT`: 見つからない。

使用箇所: `app/common/cfg.c:302` (ほか 1 行)

### TLS_PEER_VERIFY_REQUIRED

```c
TLS_PEER_VERIFY_REQUIRED  /* TLS_PEER_VERIFY ソケットオプションの値 */
```

相手の証明書の検証を、必ずする。検証に失敗したら、接続を拒否する。

使用箇所: `app/common/cloud.c:527`

## MQTT (AWS IoT Core への送信)

### mqtt_client_init

```c
void mqtt_client_init(struct mqtt_client *client);
```

MQTT のクライアントを初期化する。パラメータの設定と、ブローカへの接続の前に、必ず呼ぶ。

- `client`: クライアント。`NULL` にしない。

使用箇所: `app/common/cloud.c:512`

### mqtt_connect

```c
int mqtt_connect(struct mqtt_client *client);
```

MQTT の接続を要求する。

- `client`: クライアント。`NULL` にしない。このメモリは、`mqtt_disconnect()` を呼ぶまで、有効にしておく。接続したあとに、ブローカのアドレス、ユーザー名、機器 ID などを変えても、反映されない。
- 既定のプロトコルは 3.1.1。3.1.0 を使うには、`client.protocol_version = MQTT_VERSION_3_1_0` にする。
- キープアライブの既定は 1 分。変えるには `CONFIG_MQTT_KEEPALIVE` を変える。
- 戻り値: 成功は 0、失敗は負の errno。

使用箇所: `app/common/cloud.c:535`

### mqtt_publish

```c
int mqtt_publish(struct mqtt_client *client, const struct mqtt_publish_param *param);
```

トピックにメッセージを発行する。

- `param`: 発行のパラメータ。`NULL` にしない。
- 戻り値: 成功は 0、失敗は負の errno。

使用箇所: `app/common/cloud.c:623`

### mqtt_input

```c
int mqtt_input(struct mqtt_client *client);
```

届いた MQTT のパケットを受け取る。登録したコールバックが、パケットの内容つきで呼ばれる (`MQTT_EVT_CONNACK` `MQTT_EVT_PUBACK` `MQTT_EVT_DISCONNECT` など)。

- PUBLISH メッセージの本文は、`mqtt_read_publish_payload()` で、別に読む。
- ブロックしない呼び出し。ソケットに読めるデータがあるときに呼ぶ (`zsock_poll()` と組み合わせる)。
- 戻り値: 成功は 0、失敗は負の errno。

使用箇所: `app/common/cloud.c:490`

### mqtt_live

```c
int mqtt_live(struct mqtt_client *client);
```

接続を保つため、必要なら Ping 要求を送る。定期的に呼ぶ。

- 呼ぶ間隔は、接続のときにブローカと決めたキープアライブの時間を守れるようにする。
- 戻り値: 成功は 0、失敗は負の errno。

使用箇所: `app/common/cloud.c:662`

### mqtt_disconnect

```c
int mqtt_disconnect(struct mqtt_client *client, const struct mqtt_disconnect_param *param);
```

MQTT の接続を切る。

- `param`: 切断のパラメータ。`NULL` でよい (MQTT 3.1.1 では無視される)。
- 戻り値: 成功は 0、失敗は負の errno。

使用箇所: `app/common/cloud.c:674`

### mqtt_abort

```c
int mqtt_abort(struct mqtt_client *client);
```

MQTT の接続を中止する。MQTT の切断メッセージは送らずに、下のトランスポート (ソケット) だけを閉じる。

- 戻り値: 成功は 0、失敗は負の errno。

使用箇所: `app/common/cloud.c:546` (ほか 1 行)

### MQTT_TRANSPORT_SECURE

```c
MQTT_TRANSPORT_SECURE  /* enum mqtt_transport_type */
```

`client->transport.type` に指定して、TLS で接続する。TLS の設定は、`client->transport.tls.config` に書く。

使用箇所: `app/common/cloud.c:526`
