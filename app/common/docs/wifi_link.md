# wifi_link.c の解析

対象: `app/common/wifi_link.c` (181 行) と `wifi_link.h` (28 行)

WiFi (ステーションモード) の接続です。アクセスポイントに接続して、IPv4 アドレスを取得するまで待ちます。接続の結果は、Zephyr の `net_mgmt` のイベントで届くので、セマフォで待ちます。

## 公開する関数

| 関数 | 役割 |
|:---|:---|
| `wifi_link_init()` | イベントのコールバックを登録する |
| `wifi_link_connect(ssid, psk, timeout)` | 接続して、IPv4 アドレスの取得まで待つ |
| `wifi_link_is_up()` | IPv4 アドレスを持っているか |
| `wifi_link_disconnect()` | 切断を要求する (結果は待たない) |

## 状態

| 変数 | 内容 |
|:---|:---|
| `ip_ready` | IPv4 アドレスを取得した状態か |
| `connect_status` | 接続の結果 (0 なら成功) |
| `result_sem` | 結果が確定したときに与えるセマフォ (最大 1) |

## wifi_link_init

2 つのコールバックを登録します。WiFi のイベント (層が違う) と、IPv4 のイベントは、別々に登録します。

| コールバック | イベント |
|:---|:---|
| `wifi_cb` | `NET_EVENT_WIFI_CONNECT_RESULT`、`NET_EVENT_WIFI_DISCONNECT_RESULT` |
| `ipv4_cb` | `NET_EVENT_IPV4_ADDR_ADD` |

## wifi_link_connect

```text
iface == NULL         → -ENODEV
ip_ready              → すぐ成功 (つなぎ直さない)
パスワードがあれば WPA/WPA2 の PSK、なければ暗号化なし。チャンネルは自動
connect_status = 0、セマフォを空にする       ← 要求の直後にイベントが来ることがあるので、先に
net_mgmt(NET_REQUEST_WIFI_CONNECT)            失敗 → その値
k_sem_take(timeout)                           時間切れ → -ETIMEDOUT
connect_status != 0                           → -ECONNREFUSED
```

### イベントの処理

| イベント | 処理 |
|:---|:---|
| 接続の結果 (失敗) | `connect_status` に状態を入れて、セマフォを与える |
| 接続の結果 (成功) | 何もしない (IPv4 アドレスを待つ) |
| 切断 | `ip_ready = false` |
| IPv4 アドレスの追加 | `connect_status = 0`、`ip_ready = true`、セマフォを与える |

接続の「成功」は、IPv4 アドレスを取得したときです。無線の接続だけでは、成功としません。

## wifi_link_disconnect

`ip_ready = false` にして、すぐに接続していない状態にします (再接続を、すぐ始められるようにするため)。そのあと、`NET_REQUEST_WIFI_DISCONNECT` を要求します。結果は待ちません。

## 気づいた点

- 切断を要求した直後に、すぐ接続し直すと、前の接続の切断のイベント (`NET_EVENT_WIFI_DISCONNECT_RESULT`) が、あとから届くことがあります。そのイベントは `ip_ready = false` にするので、新しい接続の IPv4 アドレスの取得の前なら問題ありませんが、あとに届くと、つながっているのに `wifi_link_is_up()` が false になります。ドライバのイベントの順序に依存します。
- `ip_ready` と `connect_status` は `volatile` ですが、排他制御はありません。`cloud` スレッド、メインスレッド、シェルのスレッドから使います。
- 1 つのインターフェース (`net_if_get_default()`) だけを扱います。
- 接続のタイムアウトは `params.timeout = SYS_FOREVER_MS` で、ドライバ側では待ち続けます。呼び出し側の `timeout` が、実際の待ち時間の上限です。時間切れのあとも、接続の要求は残ります (呼び出し側が `wifi_link_disconnect()` で切る)。

## 関連

- [cloud.md](cloud.md)、`app/thermo-node/docs/wifi_time.md`: 呼び出し側
- [index.md](index.md): このディレクトリの文書の一覧
