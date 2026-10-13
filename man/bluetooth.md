# Bluetooth LE

Bluetooth LE のスタック (`zephyr/bluetooth/*.h`) の、使っている API。ノードは、アドバタイズと GATT サーバ (測定値の通知、時刻の書き込み) を、ゲートウェイは、スキャンと GATT クライアント (接続、探索、購読、書き込み) を使う。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## Bluetooth の初期化

### bt_enable

```c
int bt_enable(bt_ready_cb_t cb);
```

Bluetooth を有効にする。ローカルの Bluetooth ハードウェアと通信する API を呼ぶ前に、必ず呼ぶ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `cb` | in | 完了を通知するコールバック。`NULL` なら同期で有効にする。コールバックは、システムのワークキューから呼ばれる |

戻り値: 成功は 0、失敗は負のエラーコード。

注意:

- `CONFIG_BT_SETTINGS` が有効なときは、この API が成功したあとに、`settings_load()` か `settings_load_subtree("bt")` で、Bluetooth の設定を読み込む。呼ぶ前に読み込んでも足りない。

使用箇所: [`app/thermo-gateway/src/ble.c:167`](../app/thermo-gateway/src/ble.c#L167), [`app/thermo-node/src/ble.c:111`](../app/thermo-node/src/ble.c#L111)

### bt_id_get

```c
void bt_id_get(bt_addr_le_t *addrs, size_t *count);
```

設定されているアイデンティティアドレスを取得する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `addrs` | out | アドレスを入れる配列。`NULL` にすると、`count` に、取得できるアドレスの数だけが入る |
| `count` | in,out | 入力は配列の要素数 (`CONFIG_BT_ID_MAX` 以上にする)。出力は、返したアドレスの数 |

注意:

- 削除されたアドレスは、`BT_ADDR_LE_ANY` として返ることがある。

使用箇所: [`app/thermo-node/src/ble.c:158`](../app/thermo-node/src/ble.c#L158)

## アドバタイズ (ノード側)

### bt_le_adv_start

```c
int bt_le_adv_start(const struct bt_le_adv_param *param,
                    const struct bt_data *ad, size_t ad_len,
                    const struct bt_data *sd, size_t sd_len);
```

アドバタイズのデータ、スキャン応答のデータ、アドバタイズのパラメータを設定して、アドバタイズを始める。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `param` | in | アドバタイズのパラメータ。`param.peer` を設定すると、その相手への指向性アドバタイズになり、ほかの引数は無視される |
| `ad` | in | アドバタイズのパケットに入れるデータ |
| `ad_len` | in | `ad` の要素数 |
| `sd` | in | スキャン応答のパケットに入れるデータ |
| `sd_len` | in | `sd` の要素数 |

戻り値:

- 成功は 0。
- `-ENOMEM`: 接続可能なアドバタイズに使える接続オブジェクトがない。
- `-ECONNREFUSED`: 接続可能なアドバタイズを求めたが、コントローラの接続数が上限に達している (Zephyr のコントローラのときだけ確実。ほかでは `-EIO` になることがある)。
- ほかの失敗は負のエラーコード。

注意:

- 拡張アドバタイズ (`BT_LE_ADV_OPT_EXT_ADV`) には使えない。`bt_le_ext_adv_*` を使う。

使用箇所: [`app/thermo-node/src/ble.c:181`](../app/thermo-node/src/ble.c#L181)

## スキャン (ゲートウェイ側)

### bt_le_scan_start

```c
int bt_le_scan_start(const struct bt_le_scan_param *param, bt_le_scan_cb_t cb);
```

LE スキャンを、指定のパラメータで始めて、結果をコールバックで渡す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `param` | in | スキャンのパラメータ |
| `cb` | in | スキャン結果のコールバック。`bt_le_scan_cb_register()` で登録するなら `NULL` でよい |

戻り値:

- 成功は 0。
- 失敗は、プロトコルのエラーなら正の値、スタック内部のエラーなら負の値 (POSIX)。
- `-EBUSY`: 別のスレッドからスキャンを始めている最中。

注意:

- `CONFIG_BT_PRIVACY` が無効だと、アクティブスキャンは、ローカル機器のアイデンティティアドレスを使わない (スキャン要求のときに、アドレスを漏らさないため)。
- `CONFIG_BT_PRIVACY` が有効で、アクティブスキャンのとき、`param.timeout` に 0 以外を指定すると `-EINVAL` になる。
- `CONFIG_BT_EXT_ADV` が有効なら、拡張アドバタイズのパケットも自動でスキャンする。

使用箇所: [`app/thermo-gateway/src/ble.c:249`](../app/thermo-gateway/src/ble.c#L249)

### bt_le_scan_stop

```c
int bt_le_scan_stop(void);
```

実行中の LE スキャンを止める。

戻り値: 成功は 0。失敗は、プロトコルのエラーなら正の値、スタック内部のエラーなら負の値。

使用箇所: [`app/thermo-gateway/src/ble.c:688`](../app/thermo-gateway/src/ble.c#L688)

### bt_data_parse

```c
void bt_data_parse(struct net_buf_simple *ad,
                   bool (*func)(struct bt_data *data, void *user_data),
                   void *user_data);
```

アドバタイズ (または EIR、OOB) のデータを、要素ごとに分解する補助関数。多くは、`bt_le_scan_start()` のコールバックで受けたデータに使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `ad` | in,out | コールバックで受けたアドバタイズのデータ。解析の間に読み進めて消費する |
| `func` | in | 見つかった要素ごとに呼ばれるコールバック。続けるなら `true`、止めるなら `false` を返す |
| `user_data` | in | コールバックに渡す値 |

注意:

- `ad` は消費される。後で同じデータを使うなら、`net_buf_simple_save()` で状態を保存して、あとで `net_buf_simple_restore()` で戻す。

使用箇所: [`app/thermo-gateway/src/ble.c:749`](../app/thermo-gateway/src/ble.c#L749)

## 接続

### bt_conn_cb_register

```c
int bt_conn_cb_register(struct bt_conn_cb *cb);
```

接続の状態を監視するコールバックを登録する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `cb` | in | コールバックの構造体。呼び出し後も有効なメモリに置く (`static` など) |

戻り値:

- 成功は 0。
- `-EEXIST`: 同じ `cb` を登録済み。

使用箇所: [`app/thermo-gateway/src/ble.c:173`](../app/thermo-gateway/src/ble.c#L173), [`app/thermo-node/src/ble.c:123`](../app/thermo-node/src/ble.c#L123)

### bt_conn_le_create

```c
int bt_conn_le_create(const bt_addr_le_t *peer,
                      const struct bt_conn_le_create_param *create_param,
                      const struct bt_le_conn_param *conn_param,
                      struct bt_conn **conn);
```

相手のアドレスを指定して、LE 接続を始める (General Connection Establishment)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `peer` | in | 相手のアドレス |
| `create_param` | in | 接続の作成パラメータ (`BT_CONN_LE_CREATE_CONN` など) |
| `conn_param` | in | 接続パラメータの初期値 (`BT_LE_CONN_PARAM_DEFAULT` など) |
| `conn` | out | 成功したときの接続オブジェクト。新しい参照を得るので、使い終わったら `bt_conn_unref()` で解放する |

戻り値: 成功は 0、失敗は負のエラーコード。

注意:

- `CONFIG_BT_CONN_CHECK_NULL_BEFORE_CREATE` が有効で、`*conn` が `NULL` でなければ `-EINVAL`。
- `CONFIG_BT_SCAN_AND_INITIATE_IN_PARALLEL` が無効なら、接続を始める前に、明示的なスキャンを止める (このプロジェクトでは、ゲートウェイが接続の前に `bt_le_scan_stop()` を呼ぶ)。

使用箇所: [`app/thermo-gateway/src/ble.c:709`](../app/thermo-gateway/src/ble.c#L709)

### bt_conn_disconnect

```c
int bt_conn_disconnect(struct bt_conn *conn, uint8_t reason);
```

接続を切る。または、接続の途中なら取り消す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `conn` | in | 切る接続 |
| `reason` | in | 切る理由。通常の切断は `BT_HCI_ERR_REMOTE_USER_TERM_CONN` |

戻り値: 成功は 0、失敗は負のエラーコード。

注意:

- 指定できる理由は、`BT_HCI_ERR_AUTH_FAIL`、`BT_HCI_ERR_REMOTE_USER_TERM_CONN`、`BT_HCI_ERR_REMOTE_LOW_RESOURCES`、`BT_HCI_ERR_REMOTE_POWER_OFF`、`BT_HCI_ERR_UNSUPP_REMOTE_FEATURE`、`BT_HCI_ERR_PAIRING_NOT_SUPPORTED`、`BT_HCI_ERR_UNACCEPT_CONN_PARAM`。

使用箇所: [`app/thermo-gateway/src/ble.c:400`](../app/thermo-gateway/src/ble.c#L400) (ほか 2 行)

### bt_conn_ref

```c
struct bt_conn *bt_conn_ref(struct bt_conn *conn);
```

接続オブジェクトの参照カウントを 1 増やす。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `conn` | in | 接続オブジェクト |

戻り値: 参照を増やした接続オブジェクト。参照カウントが 0 なら `NULL`。

注意:

- 増やした参照は、`bt_conn_unref()` で戻す。

使用箇所: [`app/thermo-gateway/src/ble.c:610`](../app/thermo-gateway/src/ble.c#L610)

### bt_conn_unref

```c
void bt_conn_unref(struct bt_conn *conn);
```

接続オブジェクトの参照カウントを 1 減らす。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `conn` | in | 接続オブジェクト |

注意:

- `bt_conn_ref()`、`bt_conn_le_create()`、`bt_conn_lookup_addr_le()` で得た参照は、使い終わったら必ず戻す。

使用箇所: [`app/thermo-gateway/src/ble.c:306`](../app/thermo-gateway/src/ble.c#L306) (ほか 2 行)

### bt_conn_lookup_addr_le

```c
struct bt_conn *bt_conn_lookup_addr_le(uint8_t id, const bt_addr_le_t *peer);
```

アドレスから、接続オブジェクトを探す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `id` | in | アイデンティティ (通常は `BT_ID_DEFAULT`) |
| `peer` | in | 相手のアドレス |

戻り値: 見つかれば、参照を増やした接続オブジェクト (使い終わったら `bt_conn_unref()`)。なければ `NULL`。

使用箇所: [`app/thermo-gateway/src/ble.c:768`](../app/thermo-gateway/src/ble.c#L768)

### bt_conn_get_dst

```c
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn);
```

接続の相手のアドレスを返す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `conn` | in | 接続オブジェクト |

戻り値: `conn` が有効な LE 接続 (`BT_CONN_TYPE_LE`) なら、相手のアドレスへのポインタ。

使用箇所: [`app/thermo-gateway/src/ble.c:370`](../app/thermo-gateway/src/ble.c#L370) (ほか 1 行)

## アドレスの補助関数

### bt_addr_le_copy

```c
static inline void bt_addr_le_copy(bt_addr_le_t *dst, const bt_addr_le_t *src);
```

LE アドレスをコピーする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dst` | out | コピー先 |
| `src` | in | コピー元 |

使用箇所: [`app/thermo-gateway/src/ble.c:782`](../app/thermo-gateway/src/ble.c#L782), [`app/thermo-gateway/src/switchbot.c:218`](../app/thermo-gateway/src/switchbot.c#L218)

### bt_addr_le_eq

```c
static inline bool bt_addr_le_eq(const bt_addr_le_t *a, const bt_addr_le_t *b);
```

2 つの LE アドレスが等しいかを調べる。種類 (type) と、48 ビットのアドレスが、どちらも等しいときだけ等しい。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `a` | in | 比べるアドレス |
| `b` | in | 比べるアドレス |

戻り値: 等しければ `true`、違えば `false`。

使用箇所: [`app/thermo-gateway/src/ble.c:608`](../app/thermo-gateway/src/ble.c#L608), [`app/thermo-gateway/src/switchbot.c:208`](../app/thermo-gateway/src/switchbot.c#L208)

### bt_addr_le_to_str

```c
static inline int bt_addr_le_to_str(const bt_addr_le_t *addr, char *str, size_t len);
```

LE アドレスを、文字列にする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `addr` | in | 変換するアドレス |
| `str` | out | 文字列を入れるバッファ |
| `len` | in | バッファの長さ (`BT_ADDR_LE_STR_LEN` を使う) |

戻り値: 書き込めたバイト数。

使用箇所: [`app/thermo-gateway/src/ble.c:779`](../app/thermo-gateway/src/ble.c#L779), [`app/thermo-gateway/src/main.c:117`](../app/thermo-gateway/src/main.c#L117) (ほか 1 行)

## GATT サーバ (ノード側)

### bt_gatt_service_register

```c
int bt_gatt_service_register(struct bt_gatt_service *svc);
```

GATT サービスを登録する。サービスは、`BT_GATT_PRIMARY_SERVICE`、`BT_GATT_CHARACTERISTIC`、`BT_GATT_DESCRIPTOR` などのマクロで組み立てる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `svc` | in | 属性を持つサービス |

戻り値:

- 成功は 0、失敗は負の値。
- `-EAGAIN`: `bt_init()` は済んだが `settings_load()` がまだ。

注意:

- `CONFIG_BT_SETTINGS` のとき、CCC の値のように、結合 (bond) の設定を読み込ませたいサービスは、`settings_load()` の前に登録する。
- `CONFIG_BT_GATT_CACHING` のときは、GATT データベースのハッシュに入れたいサービスも、`settings_load()` の前に登録する。後から登録すると、ハッシュが再計算されて、新しく保存される。
- 呼べるのは、`bt_init()` の前か、`settings_load()` のあと。途中で呼ぶとエラーになる。

使用箇所: [`app/thermo-node/src/ble.c:117`](../app/thermo-node/src/ble.c#L117)

### bt_gatt_attr_read

```c
ssize_t bt_gatt_attr_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          void *buf, uint16_t buf_len, uint16_t offset,
                          const void *value, uint16_t value_len);
```

属性の値を読む標準の補助関数。ローカルのデータベースの値を、バッファに入れる。読み出しのコールバックの中で使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `conn` | in | 接続オブジェクト |
| `attr` | in | 読む属性 |
| `buf` | out | 値を入れるバッファ |
| `buf_len` | in | `buf` の長さ |
| `offset` | in | 読み始める位置 |
| `value` | in | 属性の値 |
| `value_len` | in | `value` の長さ |

戻り値: 成功は読んだバイト数、失敗は負の値。

使用箇所: [`app/thermo-node/src/ble.c:257`](../app/thermo-node/src/ble.c#L257)

### bt_gatt_attr_value_handle

```c
uint16_t bt_gatt_attr_value_handle(const struct bt_gatt_attr *attr);
```

キャラクタリスティックの値のハンドルを返す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `attr` | in | キャラクタリスティックの属性 (`user_data` が `bt_gatt_chrc` で、`uuid` が `BT_UUID_GATT_CHRC`) |

戻り値: 値のハンドル。`attr` がキャラクタリスティックでなければ 0 (無効なハンドル)。

使用箇所: [`app/thermo-gateway/src/ble.c:419`](../app/thermo-gateway/src/ble.c#L419) (ほか 1 行)

### bt_gatt_notify

```c
static inline int bt_gatt_notify(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 const void *data, uint16_t len);
```

属性の値が変わったことを、通知で送る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `conn` | in | 送る接続。`NULL` なら、CCC で通知を有効にした全ての相手に送る。`NULL` でなければ、その接続だけに送る |
| `attr` | in | キャラクタリスティックの宣言 (`BT_GATT_CHARACTERISTIC` に `BT_GATT_CCC` が続くもの) か、値の宣言 |
| `data` | in | 送る値 |
| `len` | in | `data` の長さ |

戻り値: 成功は 0、失敗は負の値。

使用箇所: [`app/thermo-node/src/ble.c:213`](../app/thermo-node/src/ble.c#L213)

## GATT クライアント (ゲートウェイ側)

### bt_gatt_discover

```c
int bt_gatt_discover(struct bt_conn *conn, struct bt_gatt_discover_params *params);
```

サーバの属性を探索する。プライマリサービス、インクルードサービス、キャラクタリスティック、ディスクリプタの探索ができる。見つかった属性ごとに、コールバックが呼ばれ、続けるか止めるかを決める。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `conn` | in | 接続オブジェクト |
| `params` | in | 探索のパラメータ (`BT_GATT_DISCOVER_PRIMARY`、`BT_GATT_DISCOVER_CHARACTERISTIC`、`BT_GATT_DISCOVER_DESCRIPTOR` など)。`attr` が `NULL` のコールバックが始まるまで (またはコールバックが `BT_GATT_ITER_STOP` を返すまで) 有効にしておく |

戻り値:

- 要求を積めたら 0 (結果は `params->func` に届く)。
- `-ENOMEM`: ATT 要求のキューが満杯。要求が終わるのを待ってやり直すか、別のスレッドから呼ぶ。キューの大きさは `CONFIG_BT_ATT_TX_COUNT`。

注意:

- 応答は、`params->func` に届く。コールバックは、BT の受信スレッドから呼ばれる。

使用箇所: [`app/thermo-gateway/src/ble.c:416`](../app/thermo-gateway/src/ble.c#L416) (ほか 3 行)

### bt_gatt_subscribe

```c
int bt_gatt_subscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *params);
```

CCC のハンドルを使って、値の通知を購読する。通知が届くと、購読のコールバックが、通知された値を渡す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `conn` | in | 接続オブジェクト |
| `params` | in | 購読のパラメータ。購読している間は有効にしておき、別の購読には使い回せない |

戻り値:

- 要求を積めたら 0。
- `-ENOMEM`: ATT 要求のキューが満杯。
- `-EALREADY`: その `params` で、すでに購読している。
- `-EBUSY`: `params.ccc_handle` が 0 で、`CONFIG_BT_GATT_AUTO_DISCOVER_CCC` が有効なとき、その `params` の探索が進行中。

注意:

- 応答は `params->subscribe` に、通知は `params->notify` に届く (どちらも BT の受信スレッドなどから呼ばれる)。

使用箇所: [`app/thermo-gateway/src/ble.c:429`](../app/thermo-gateway/src/ble.c#L429)

### bt_gatt_write

```c
int bt_gatt_write(struct bt_conn *conn, struct bt_gatt_write_params *params);
```

ハンドルを指定して、属性の値を書く。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `conn` | in | 接続オブジェクト |
| `params` | in | 書き込みのパラメータ。コールバックが始まるまで有効にしておく |

戻り値:

- 要求を積めたら 0。
- `-ENOMEM`: ATT 要求のキューが満杯 (待ってやり直すか、別のスレッドから呼ぶ)。

注意:

- 応答は、`params->func` に届く。

使用箇所: [`app/thermo-gateway/src/ble.c:521`](../app/thermo-gateway/src/ble.c#L521)

## マクロ

### BT_DATA

```c
#define BT_DATA(_type, _data, _data_len)
```

アドバタイズのデータの 1 要素 (`struct bt_data`) を、種類・データ・長さで作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_type` | in | 種類 (`BT_DATA_NAME_COMPLETE` (名前)、`BT_DATA_MANUFACTURER_DATA` (製造者データ) など) |
| `_data` | in | データ |
| `_data_len` | in | データの長さ |

使用箇所: [`app/thermo-node/src/ble.c:33`](../app/thermo-node/src/ble.c#L33)

### BT_DATA_BYTES

```c
#define BT_DATA_BYTES(_type, _bytes...)
```

アドバタイズのデータの 1 要素を、バイト列の並びで作る (`BT_DATA_FLAGS` に `BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR` を渡す、など)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_type` | in | 種類 |
| `_bytes` | in | データのバイトの並び |

使用箇所: [`app/thermo-node/src/ble.c:32`](../app/thermo-node/src/ble.c#L32) (ほか 1 行)

### BT_GATT_PRIMARY_SERVICE

```c
#define BT_GATT_PRIMARY_SERVICE(_service)
```

プライマリサービスの宣言の属性を作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_service` | in | サービスの UUID |

使用箇所: [`app/thermo-node/src/ble.c:76`](../app/thermo-node/src/ble.c#L76)

### BT_GATT_CHARACTERISTIC

```c
#define BT_GATT_CHARACTERISTIC(_uuid, _props, _perm, _read, _write, _user_data)
```

キャラクタリスティックの宣言と値の宣言の 2 つの属性を作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_uuid` | in | キャラクタリスティックの UUID |
| `_props` | in | `BT_GATT_CHRC_READ` `BT_GATT_CHRC_WRITE` `BT_GATT_CHRC_NOTIFY` など |
| `_perm` | in | `BT_GATT_PERM_READ` `BT_GATT_PERM_WRITE` など |
| `_read` | in | 読み出しのコールバック |
| `_write` | in | 書き込みのコールバック |
| `_user_data` | in | コールバックに渡す値 |

使用箇所: [`app/thermo-node/src/ble.c:77`](../app/thermo-node/src/ble.c#L77) (ほか 1 行)

### BT_GATT_CCC

```c
#define BT_GATT_CCC(_changed, _perm)
```

Client Characteristic Configuration (CCC) のディスクリプタを作る。通知 (`BT_GATT_CCC_NOTIFY`) の有効・無効を、相手が書き換えられるようにする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_changed` | in | 値が変わったときのコールバック |
| `_perm` | in | アクセスの許可 |

使用箇所: [`app/thermo-node/src/ble.c:79`](../app/thermo-node/src/ble.c#L79)

### BT_GATT_SERVICE

```c
#define BT_GATT_SERVICE(_attrs)
```

属性の配列から、`struct bt_gatt_service` の初期化子を作る。`bt_gatt_service_register()` に渡す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_attrs` | in | サービスの属性の配列 |

使用箇所: [`app/thermo-node/src/ble.c:90`](../app/thermo-node/src/ble.c#L90)

### BT_GATT_ERR

```c
#define BT_GATT_ERR(_att_err)
```

ATT のエラーコードを、GATT のコールバックの戻り値に変える。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_att_err` | in | ATT のエラー (`BT_ATT_ERR_INVALID_OFFSET` `BT_ATT_ERR_INVALID_ATTRIBUTE_LEN` `BT_ATT_ERR_VALUE_NOT_ALLOWED` `BT_ATT_ERR_WRITE_NOT_PERMITTED` `BT_ATT_ERR_UNLIKELY` など) |

使用箇所: [`app/thermo-node/src/ble.c:287`](../app/thermo-node/src/ble.c#L287) (ほか 4 行)

### BT_UUID_INIT_128

```c
#define BT_UUID_INIT_128(value...)
```

128 ビットの UUID の初期化子を作る。`BT_UUID_128_ENCODE()` と組み合わせる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `value` | in | 128 ビットの UUID の値 |

使用箇所: [`app/thermo-gateway/src/ble.c:66`](../app/thermo-gateway/src/ble.c#L66) (ほか 2 行)

### BT_UUID_INIT_16

```c
#define BT_UUID_INIT_16(value)
```

16 ビットの UUID の初期化子を作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `value` | in | 16 ビットの UUID の値 |

使用箇所: [`app/thermo-gateway/src/ble.c:72`](../app/thermo-gateway/src/ble.c#L72)

### BT_GAP_MS_TO_SCAN_INTERVAL

```c
#define BT_GAP_MS_TO_SCAN_INTERVAL(_interval)
```

ミリ秒を、スキャンの間隔の単位 (0.625 ms) に換算する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_interval` | in | スキャンの間隔 [ms]。範囲は、`CONFIG_BT_EXT_ADV` が有効なら 2.5 から 40959.375、そうでなければ 2.5 から 10240 |

戻り値: スキャンの間隔 (0.625 ms の単位。`uint16_t`)。

注意:

- 単位の倍数でなければ、切り捨てる。例えば `BT_GAP_MS_TO_SCAN_INTERVAL(21)` は 20.625 ms になる。

使用箇所: [`app/thermo-gateway/src/ble.c:245`](../app/thermo-gateway/src/ble.c#L245)

### BT_GAP_MS_TO_SCAN_WINDOW

```c
#define BT_GAP_MS_TO_SCAN_WINDOW(_window)
```

ミリ秒を、スキャンの窓 (1 回の間隔のうち、受信する時間) の単位 (0.625 ms) に換算する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_window` | in | スキャンの窓 [ms]。範囲は、`BT_GAP_MS_TO_SCAN_INTERVAL` と同じ |

戻り値: スキャンの窓 (0.625 ms の単位。`uint16_t`)。

注意:

- 単位の倍数でなければ、切り捨てる。例えば `BT_GAP_MS_TO_SCAN_WINDOW(21)` は 20.625 ms になる。

使用箇所: [`app/thermo-gateway/src/ble.c:246`](../app/thermo-gateway/src/ble.c#L246)
