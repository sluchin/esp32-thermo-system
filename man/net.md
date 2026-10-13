# ネットワーク

ネットワーク (`zephyr/net/*.h`) の、使っている API。WiFi の接続、名前解決とソケット、SNTP、TLS の認証情報、MQTT。AWS IoT Core には、TLS の MQTT で発行する。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## WiFi とネットワーク管理

### net_if_get_default

```c
struct net_if *net_if_get_default(void);
```

既定のネットワークインターフェースを返す。

戻り値: 既定のインターフェース。インターフェースが 1 つもなければ `NULL`。

使用箇所: [`app/common/wifi_link.c:73`](../app/common/wifi_link.c#L73) (ほか 1 行)

### net_mgmt

```c
#define net_mgmt(_mgmt_request, _iface, _data, _len)
```

ネットワーク管理の要求を出す (イベントを生成する)。WiFi の接続や切断は、これに `NET_REQUEST_WIFI_CONNECT` `NET_REQUEST_WIFI_DISCONNECT` を渡して要求する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_mgmt_request` | in | 要求の識別子 |
| `_iface` | in | ネットワークインターフェース |
| `_data` | in,out | 追加のデータ (接続は `struct wifi_connect_req_params`。切断は `NULL`) |
| `_len` | in | `_data` の長さ (切断は 0) |

戻り値: 成功は 0、失敗は負の errno。

使用箇所: [`app/common/wifi_link.c:101`](../app/common/wifi_link.c#L101) (ほか 1 行)

### net_mgmt_init_event_callback

```c
void net_mgmt_init_event_callback(struct net_mgmt_event_callback *cb, net_mgmt_event_handler_t handler, uint64_t mgmt_event_mask);
```

`net_mgmt_event_callback` を、正しく初期化する補助関数。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `cb` | out | アプリのコールバックの構造体 |
| `handler` | in | ハンドラ関数 |
| `mgmt_event_mask` | in | ハンドラが受けるイベントのマスク (`NET_EVENT_WIFI_CONNECT_RESULT` `NET_EVENT_WIFI_DISCONNECT_RESULT` など) |

使用箇所: [`app/common/wifi_link.c:47`](../app/common/wifi_link.c#L47) (ほか 1 行)

### net_mgmt_add_event_callback

```c
void net_mgmt_add_event_callback(struct net_mgmt_event_callback *cb);
```

ユーザーのコールバックを加える。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `cb` | in,out | 加えるコールバック。有効なままにしておく |

使用箇所: [`app/common/wifi_link.c:49`](../app/common/wifi_link.c#L49) (ほか 1 行)

### wifi_connect_req_params

```c
struct wifi_connect_req_params
```

WiFi の接続の要求に渡すパラメータ (SSID、パスワード、セキュリティの種類、バンドなど)。`NET_REQUEST_WIFI_CONNECT` の `_data` に渡す。

使用箇所: [`app/common/wifi_link.c:74`](../app/common/wifi_link.c#L74)

### NET_REQUEST_WIFI_CONNECT

```c
NET_REQUEST_WIFI_CONNECT
```

WiFi の接続の要求。`net_mgmt()` に渡す。結果は、`NET_EVENT_WIFI_CONNECT_RESULT` で届く。

使用箇所: [`app/common/wifi_link.c:101`](../app/common/wifi_link.c#L101)

### NET_REQUEST_WIFI_DISCONNECT

```c
NET_REQUEST_WIFI_DISCONNECT
```

WiFi の切断の要求。`net_mgmt()` に渡す。結果は、`NET_EVENT_WIFI_DISCONNECT_RESULT` で届く。

使用箇所: [`app/common/wifi_link.c:144`](../app/common/wifi_link.c#L144)

## ソケット

### zsock_getaddrinfo

```c
int zsock_getaddrinfo(const char *host, const char *service, const struct zsock_addrinfo *hints, struct zsock_addrinfo **res);
```

ホスト名を、1 つ以上のネットワークアドレスにする (名前解決)。POSIX.1-2017 の `getaddrinfo()` と同じ。`CONFIG_POSIX_API` が有効なら、`getaddrinfo()` の名前でも使える。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `host` | in | ホスト名 |
| `service` | in | サービス名かポート番号の文字列 |
| `hints` | in | 結果の絞り込み (`ai_family`、`ai_socktype` など) |
| `res` | out | 結果の一覧の先頭 |

戻り値:

- 成功は 0。
- 失敗は 0 以外 (`EAI_*`)。

注意:

- 成功したら、`res` を `zsock_freeaddrinfo()` で解放する。

使用箇所: [`app/common/cloud.c:451`](../app/common/cloud.c#L451), [`app/common/ntp.c:139`](../app/common/ntp.c#L139), [`app/thermo-node/src/wifi_time.c:142`](../app/thermo-node/src/wifi_time.c#L142)

### zsock_freeaddrinfo

```c
void zsock_freeaddrinfo(struct zsock_addrinfo *ai);
```

`zsock_getaddrinfo()` が返した結果を解放する。POSIX.1-2017 の `freeaddrinfo()` と同じ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `ai` | in,out | 解放する結果の一覧の先頭 |

使用箇所: [`app/common/cloud.c:459`](../app/common/cloud.c#L459), [`app/common/ntp.c:148`](../app/common/ntp.c#L148), [`app/thermo-node/src/wifi_time.c:151`](../app/thermo-node/src/wifi_time.c#L151)

### zsock_poll

```c
static inline int zsock_poll(struct zsock_pollfd *fds, int nfds, int timeout);
```

複数のソケットのイベントを、まとめて待つ。POSIX.1-2017 の `poll()` と同じ。`CONFIG_POSIX_API` が有効なら、`poll()` の名前でも使える。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `fds` | in,out | 調べるソケットと、待つイベント (`events`)。戻ったとき、起きたイベントが `revents` に入る |
| `nfds` | in | `fds` の要素数 |
| `timeout` | in | 待つ時間 [ms]。負の値は、無期限に待つ |

戻り値:

- イベントが起きたソケットの数。
- 時間切れは 0。
- 失敗は -1 (`errno`)。

使用箇所: [`app/common/cloud.c:476`](../app/common/cloud.c#L476)

## SNTP (時刻の取得)

### sntp_init

```c
int sntp_init(struct sntp_ctx *ctx, struct sockaddr *addr, socklen_t addr_len);
```

SNTP のコンテキストを初期化する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `ctx` | out | SNTP のコンテキスト |
| `addr` | in | NTP / SNTP サーバの IP アドレス |
| `addr_len` | in | `addr` の長さ |

戻り値: 成功は 0、失敗は負の値。

使用箇所: [`app/common/ntp.c:68`](../app/common/ntp.c#L68), [`app/thermo-node/src/wifi_time.c:109`](../app/thermo-node/src/wifi_time.c#L109)

### sntp_query

```c
int sntp_query(struct sntp_ctx *ctx, uint32_t timeout, struct sntp_time *ts);
```

SNTP の問い合わせをして、時刻を得る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `ctx` | in,out | SNTP のコンテキスト |
| `timeout` | in | 応答を待つ時間 [ms] |
| `ts` | out | 1970-01-01 からの秒 (整数部と小数部) |

戻り値: 成功は 0、失敗は負の値 (時間切れは `-ETIMEDOUT`)。

使用箇所: [`app/common/ntp.c:74`](../app/common/ntp.c#L74), [`app/thermo-node/src/wifi_time.c:115`](../app/thermo-node/src/wifi_time.c#L115)

### sntp_close

```c
void sntp_close(struct sntp_ctx *ctx);
```

SNTP のコンテキストを解放する (ソケットを閉じる)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `ctx` | in,out | SNTP のコンテキスト |

使用箇所: [`app/common/ntp.c:75`](../app/common/ntp.c#L75), [`app/thermo-node/src/wifi_time.c:116`](../app/thermo-node/src/wifi_time.c#L116)

## TLS の認証情報

### tls_credential_add

```c
int tls_credential_add(sec_tag_t tag, enum tls_credential_type type, const void *cred, size_t credlen);
```

TLS / DTLS の認証に使う、認証情報を加える。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `tag` | in | 認証情報を参照するための、セキュリティタグ |
| `type` | in | 認証情報の種類 (`TLS_CREDENTIAL_CA_CERTIFICATE` `TLS_CREDENTIAL_PUBLIC_CERTIFICATE` `TLS_CREDENTIAL_PRIVATE_KEY`) |
| `cred` | in | 認証情報 |
| `credlen` | in | `cred` の長さ |

戻り値:

- 成功は 0。
- `-EACCES`: アクセスが拒否された。
- `-ENOMEM`: メモリが足りない。
- `-EEXIST`: そのタグと種類の認証情報が、すでにある。

使用箇所: [`app/common/cfg.c:591`](../app/common/cfg.c#L591)

### tls_credential_get

```c
int tls_credential_get(sec_tag_t tag, enum tls_credential_type type, void *cred, size_t *credlen);
```

登録済みの認証情報を取得する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `tag` | in | 取得する認証情報のセキュリティタグ |
| `type` | in | 認証情報の種類 |
| `cred` | out | 認証情報を入れるバッファ |
| `credlen` | in,out | 入力はバッファの大きさ。出力は認証情報の長さ |

戻り値:

- 成功は 0。
- `-EACCES`: アクセスが拒否された。
- `-ENOENT`: 見つからない。
- `-EFBIG`: バッファに入らない (必要な大きさは `*credlen` に入る)。

使用箇所: [`app/common/cfg.c:207`](../app/common/cfg.c#L207) (ほか 1 行)

### tls_credential_delete

```c
int tls_credential_delete(sec_tag_t tag, enum tls_credential_type type);
```

登録済みの認証情報を削除する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `tag` | in | 削除する認証情報のセキュリティタグ |
| `type` | in | 認証情報の種類 |

戻り値:

- 成功は 0。
- `-EACCES`: アクセスが拒否された。
- `-ENOENT`: 見つからない。

使用箇所: [`app/common/cfg.c:302`](../app/common/cfg.c#L302) (ほか 1 行)

### TLS_PEER_VERIFY_REQUIRED

```c
TLS_PEER_VERIFY_REQUIRED  /* TLS_PEER_VERIFY ソケットオプションの値 */
```

相手の証明書の検証を、必ずする。検証に失敗したら、接続を拒否する。

使用箇所: [`app/common/cloud.c:527`](../app/common/cloud.c#L527)

## MQTT (AWS IoT Core への送信)

### mqtt_client_init

```c
void mqtt_client_init(struct mqtt_client *client);
```

MQTT のクライアントを初期化する。パラメータの設定と、ブローカへの接続の前に、必ず呼ぶ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `client` | out | 初期化する MQTT のクライアント。`NULL` にしない |

使用箇所: [`app/common/cloud.c:512`](../app/common/cloud.c#L512)

### mqtt_connect

```c
int mqtt_connect(struct mqtt_client *client);
```

MQTT の接続を要求する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `client` | in,out | MQTT のクライアント。`NULL` にしない |

戻り値: 成功は 0、失敗は負の errno。

注意:

- `client` のメモリは、`mqtt_disconnect()` を呼ぶまで、有効にしておく。
- 接続したあとに、ブローカのアドレス、ユーザー名、機器 ID などを変えても、反映されない。
- 既定のプロトコルは 3.1.1。3.1.0 を使うには、`client.protocol_version = MQTT_VERSION_3_1_0` にする。
- キープアライブの既定は 1 分。変えるには `CONFIG_MQTT_KEEPALIVE` を変える。

使用箇所: [`app/common/cloud.c:535`](../app/common/cloud.c#L535)

### mqtt_publish

```c
int mqtt_publish(struct mqtt_client *client, const struct mqtt_publish_param *param);
```

トピックにメッセージを発行する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `client` | in,out | MQTT のクライアント。`NULL` にしない |
| `param` | in | 発行のパラメータ。`NULL` にしない |

戻り値: 成功は 0、失敗は負の errno。

使用箇所: [`app/common/cloud.c:623`](../app/common/cloud.c#L623)

### mqtt_input

```c
int mqtt_input(struct mqtt_client *client);
```

届いた MQTT のパケットを受け取る。登録したコールバックが、パケットの内容つきで呼ばれる (`MQTT_EVT_CONNACK` `MQTT_EVT_PUBACK` `MQTT_EVT_DISCONNECT` など)。ブロックしない呼び出し。ソケットに読めるデータがあるときに呼ぶ (`zsock_poll()` と組み合わせる)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `client` | in,out | MQTT のクライアント。`NULL` にしない |

戻り値: 成功は 0、失敗は負の errno。

注意:

- PUBLISH メッセージの本文は、`mqtt_read_publish_payload()` で、別に読む。

使用箇所: [`app/common/cloud.c:490`](../app/common/cloud.c#L490)

### mqtt_live

```c
int mqtt_live(struct mqtt_client *client);
```

接続を保つため、必要なら Ping 要求を送る。定期的に呼ぶ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `client` | in,out | MQTT のクライアント。`NULL` にしない |

戻り値: 成功は 0、失敗は負の errno。

注意:

- 呼ぶ間隔は、接続のときにブローカと決めたキープアライブの時間を守れるようにする。

使用箇所: [`app/common/cloud.c:662`](../app/common/cloud.c#L662)

### mqtt_disconnect

```c
int mqtt_disconnect(struct mqtt_client *client, const struct mqtt_disconnect_param *param);
```

MQTT の接続を切る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `client` | in,out | MQTT のクライアント。`NULL` にしない |
| `param` | in | 切断のパラメータ。`NULL` でよい (MQTT 3.1.1 では無視される) |

戻り値: 成功は 0、失敗は負の errno。

使用箇所: [`app/common/cloud.c:674`](../app/common/cloud.c#L674)

### mqtt_abort

```c
int mqtt_abort(struct mqtt_client *client);
```

MQTT の接続を中止する。MQTT の切断メッセージは送らずに、下のトランスポート (ソケット) だけを閉じる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `client` | in,out | MQTT のクライアント。`NULL` にしない |

戻り値: 成功は 0、失敗は負の errno。

使用箇所: [`app/common/cloud.c:546`](../app/common/cloud.c#L546) (ほか 1 行)

### MQTT_TRANSPORT_SECURE

```c
MQTT_TRANSPORT_SECURE  /* enum mqtt_transport_type */
```

`client->transport.type` に指定して、TLS で接続する。TLS の設定は、`client->transport.tls.config` に書く。

使用箇所: [`app/common/cloud.c:526`](../app/common/cloud.c#L526)
