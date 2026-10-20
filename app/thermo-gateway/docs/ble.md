# ble.c の解析

対象: `app/thermo-gateway/src/ble.c` (796 行) と `ble.h` (66 行)

ゲートウェイの BLE の本体です。BLE の中央機器 (Central) として、次の 4 つを行います。

1. 周囲のアドバタイズを受け取り (アクティブスキャン)、Thermo サービスを持つノードを見つけて接続する。
2. 接続したノードの GATT を探索して、温度の特性の通知 (notify) を購読する。
3. ノードの時刻の特性に、UTC の UNIX 時刻を書き込む (ノードの RTC を合わせる)。
4. 接続しない SwitchBot の温湿度計のアドバタイズを、`switchbot.c` で解析して、コールバックに渡す。

`main.c` からは、`ble.h` の 5 つの関数だけが見えます。

| 関数 | 役割 |
|:---|:---|
| `ble_init()` | `bt_enable()` と接続のコールバックの登録 |
| `ble_set_temperature_callback()` | 温度を受信したときのコールバックを設定 |
| `ble_set_switchbot_callback()` | SwitchBot のアドバタイズを受信したときのコールバックを設定 |
| `ble_set_time_source()` | ノードに書く時刻を返す関数を設定 (NULL なら、時刻を書かない) |
| `ble_scan()` | アクティブスキャンを開始 (すでに動いていれば、成功) |

## 状態を持つデータ

### struct node (ノード 1 台ぶん)

| メンバー | 内容 |
|:---|:---|
| `conn` | 接続。NULL なら、この要素は空き |
| `discover` | GATT の探索のパラメータ (サービス、温度の特性、CCC、時刻の特性で使い回す) |
| `subscribe` | 通知の購読のパラメータ |
| `write` | 時刻の書き込みのパラメータ |
| `service_start`, `service_end` | Thermo サービスの属性のハンドルの範囲 (時刻の特性を、同じ範囲の中から探すため) |
| `time_handle` | 時刻の特性の値のハンドル。0 なら、ノードに時刻の特性がない |
| `time_due_ms` | 次に時刻を書き込む稼働時間 [ms] |
| `time_buf` | 書き込む時刻の 4 バイト (UTC の UNIX 時刻。uint32 のリトルエンディアン) |

- `nodes[MAX_NODES]` が、接続中のノードの表です。`MAX_NODES` は `CONFIG_BT_MAX_CONN` (`prj.conf` では 3) です。
- `discover`、`subscribe`、`write` のパラメータは、非同期の処理が終わるまで、スタックが参照し続けます。そのため、スタック変数ではなく、ノードの構造体の中に置いています。`time_buf` も同じ理由で、構造体のメンバーです。
- 各コールバックは、`CONTAINER_OF(params, struct node, discover)` のようにして、パラメータからノードを逆引きします。

### そのほかの static 変数

| 変数 | 内容 |
|:---|:---|
| `service_uuid`、`temperature_uuid`、`time_uuid`、`ccc_uuid` | 探索に使う UUID (定義は `common/thermo_ble_uuid.h`) |
| `service_uuid_val[]` | スキャン応答の UUID と、バイト列で比べるための値 |
| `temperature_cb`、`switchbot_cb`、`time_source` | `main.c` が登録するコールバックと関数 |
| `pending_addr`、`pending_node`、`pending_retries` | 接続を始める依頼 (スキャンのコールバックが記録して、ワークが実行する) |
| `connect_work`、`time_work` | システムのワークキューで動く、遅延ワーク |

## 定数

| 定数 | 値 | 意味 |
|:---|:---|:---|
| `SCAN_INTERVAL_MS` | 100 | スキャンの間隔 |
| `SCAN_WINDOW_MS` | 50 | スキャンのウィンドウ (間隔の半分。受信している時間の割合は 50%) |
| `CONNECT_RETRY_MS` | 100 | スキャンを止められなかったときの、やり直しの間隔 |
| `CONNECT_RETRY_MAX` | 10 | やり直しの最大の回数 |
| `TIME_CHECK_S` | 10 | 時刻を書く必要があるかの確認の間隔 |
| `TIME_RESYNC_S` | 3600 | 時刻を書き直す間隔 (ノードの RTC のずれを直す) |
| `TIME_RETRY_S` | 60 | 時刻の書き込みに失敗したときの、やり直しの間隔 |

## 接続の流れ

```text
Bluetooth のスレッド                      システムのワークキュー
─────────────────                      ──────────────────
scan_cb()
  ├ parse_ad() で UUID を探す
  ├ SwitchBot → switchbot_cb() して終了
  ├ Thermo UUID なし → 終了
  ├ 依頼中 / 接続済み / 空きなし → 終了
  └ pending_* を記録 ──────────────────▶ connect_work_handler()
                                            ├ bt_le_scan_stop()  (EBUSY なら 100 ms 後に再試行 ×10、EALREADY は成功)
                                            │   失敗 → ble_scan() でスキャンを再開して終了
                                            └ bt_conn_le_create()
connected()  ◀────────────────────────────  (結果は、コールバックで届く)
  ├ 失敗 → release_node()
  └ 成功 → bt_gatt_discover(サービス)
  そして、どちらも ble_scan() でスキャンを再開
discover_cb()  ×4 (サービス → 温度の特性 → CCC → 時刻の特性)
notify_cb()    温度の通知のたびに
disconnected() → release_node() → ble_scan()
```

### scan_cb: ノードを見つける

1. `bt_data_parse()` で、アドバタイズデータの要素を 1 つずつ `parse_ad()` に渡します。
2. `parse_ad()` が見るのは 2 つです。
   - 要素の種類が `BT_DATA_UUID128_ALL` で、長さが 16 バイト、中身が Thermo サービスの UUID と同じなら、`thermo = true`。
   - `switchbot_parse()` が `true` を返せば、`has_switchbot = true` (結果は `result->switchbot` に入る)。
   - どちらかが見つかったら、`false` を返して、解析を打ち切ります。
3. SwitchBot なら、`switchbot_cb()` を呼んで終わりです。接続はしません。
4. Thermo UUID がなければ、無視します。
5. 次の 3 つのどれかなら、無視します。
   - `pending_node != NULL`: 前の接続の依頼を処理中。
   - `bt_conn_lookup_addr_le()` で、すでに接続している (または接続中)。
   - `find_free_node()` が NULL。接続できる台数に達している。
6. 残ったときだけ、`pending_addr` と `pending_node` を記録して、`connect_work` を `K_NO_WAIT` で予約します。

ノードの Thermo サービスの UUID は、スキャン応答 (scan response) に入っています (128 ビットの UUID は 18 バイトで、名前と合わせると、アドバタイズの 31 バイトに入らないため。`thermo-node/src/ble.c` を参照)。ゲートウェイが **アクティブスキャン** を使うのは、そのためです。アドバタイズとスキャン応答は、別々のコールバックで届きます (`adv_type` は使いません)。

### connect_work_handler: スキャンを止めて接続する

スキャンのコールバックの中で、スキャンを止めたり、接続を始めたりすると、スキャンの処理と重なって、`bt_le_scan_stop()` が `-EBUSY` になることがあります。そのため、コールバックは依頼を記録するだけにして、実際の処理はワークキューで行います。

- `bt_le_scan_stop()` が `-EBUSY` で、`pending_retries < 10` なら、`pending_retries` を増やして、100 ms 後に自分を予約し直します。
- `-EALREADY` (すでに止まっている) は、成功として扱います。スキャンを止める目的は、満たしているためです。
- ほかのエラーなら、`pending_node` を NULL に戻して、接続をあきらめ、`ble_scan()` でスキャンを再開します。再開しないと、スキャンが止まったままになり、以降は、スキャンのコールバックが呼ばれず、ノードを見つけられなくなります (ゲートウェイを再起動するまで、つながらない)。再開に失敗したときは、ログを出すだけです。次にノードが見つかったときに、またやり直します。
- `bt_conn_le_create()` が成功したら、`node->conn` に接続を入れます。ただし、`bt_conn_le_create()` が戻る前に、`connected()` が先に呼ばれることがあります。そのときは、`connected()` が `bt_conn_ref()` で参照を取っているので、ここで受け取った参照は `bt_conn_unref()` で手放します。
- 失敗したときは、スキャンを再開します。
- 最後に、必ず `pending_node = NULL` にします。

### connected: 接続できた

- `find_node(conn)` で、ノードを探します。見つからず、`pending_node` があれば、接続先のアドレスが `pending_addr` と同じかを調べて、同じなら、その接続を `pending_node` に割り当てます (上の「戻る前に呼ばれる」場合の対応です)。
- どちらでもなければ、このゲートウェイが始めた接続ではないので、何もしません。
- 失敗 (`err != 0`) なら、`release_node()` で状態を戻します。
- 成功なら、Thermo サービスの探索 (`BT_GATT_DISCOVER_PRIMARY`、ハンドル全体) を始めます。始められなければ、切断します。
- 成功でも失敗でも、最後に `ble_scan()` を呼びます。接続を始めるときにスキャンを止めたので、ほかのノードを探すために再開します。

### disconnected: 切れた

`release_node()` で、接続の参照を解放して、構造体を 0 で埋めます。そのあと `ble_scan()` でスキャンを再開します。ノードのほうは、切断されると、アドバタイズを再開するので、また見つかります。

## GATT の探索と購読

`discover_cb()` は、1 つの関数で、探索の段階ごとの結果を受け取ります。`params->type` と `params->uuid` を書き換えて、`bt_gatt_discover()` を呼び直すことで、次の段階に進みます。

| 段階 | `params->type` | `params->uuid` | 見つかったときの処理 |
|:---|:---|:---|:---|
| 1 | `PRIMARY` | Thermo サービス | サービスの範囲 (`service_start`、`service_end`) を記録して、温度の特性を探す |
| 2 | `CHARACTERISTIC` | 温度の特性 | 値のハンドルを `subscribe.value_handle` に入れて、CCC を探す (開始は `attr->handle + 2`) |
| 3 | `DESCRIPTOR` | CCC (0x2902) | `ccc_handle` を入れて `bt_gatt_subscribe()`。成功したら、時刻の特性の探索を始める |
| 4 | `CHARACTERISTIC` | 時刻の特性 | `time_discover_cb()` が処理する |

- 時刻の特性の探索かどうかは、`params->uuid == &time_uuid.uuid` で見分けます。段階 1 から 3 では、`uuid` を、そのたびに書き換えているので、取り違えません。この判定を `attr == NULL` の判定より先に行うことで、「ノードに時刻の特性がない」場合を、サービスが見つからない場合 (切断する) と区別します。
- サービスが見つからない (`attr == NULL`) と、`Thermo service not found` を出して、切断します。
- 探索や購読が失敗しても、切断します。
- `bt_gatt_subscribe()` が `-EALREADY` を返したときは、成功として扱います。
- どの段階でも、`BT_GATT_ITER_STOP` を返します (次の段階は、自分で `bt_gatt_discover()` を呼んで始めるため)。
- CCC を探す開始位置が `attr->handle + 2` なのは、特性の宣言 (+0)、値 (+1)、CCC (+2) の順に並ぶ、というノード側の定義 (`thermo-node/src/ble.c` の `thermo_attrs`) に頼っているためです。

### notify_cb: 温度を受け取る

- `data == NULL` は、購読が解除されたことを表します。`value_handle = 0` にして、`BT_GATT_ITER_STOP` を返します。
- 長さが `THERMO_TEMPERATURE_SIZE` (6 バイト) でも `THERMO_TEMPERATURE_SIZE_NO_PRESSURE` (4 バイト) でもなければ、警告を出して、無視します。
- 先頭の 2 バイトが温度 (`int16_t`、℃ の 10 倍)、続く 2 バイトが湿度 (`uint16_t`、% の 10 倍。0xFFFF は湿度なし)、続く 2 バイトが気圧 (`uint16_t`、hPa の 10 倍。0xFFFF は気圧なし) で、どれもリトルエンディアンです。
- 4 バイトの通知は、気圧を足す前のノードからです。気圧なし (`THERMO_PRESSURE_NONE`) とします (ノードとゲートウェイを、別々に更新できます)。
- `temperature_cb(アドレス, 温度, 湿度, 気圧)` を呼びます。

## 時刻の書き込み

ノードに RTC (PCF8563) があっても、電池が切れていたり、初めて起動したりすると、時刻がありません。ゲートウェイは、SNTP で合わせた時刻を、ノードに書き込みます。

```text
時刻の特性を見つける ─▶ time_work を即時に予約
                              │
        time_work_handler() ◀─┘   (動いている間は 10 秒ごとに、自分を予約し直す)
          ├ time_source(&unix_s)    ← ntp_unix_time()。まだ同期していなければ、負を返す
          └ 各ノードについて
              (conn あり && time_handle != 0) なら 待ち対象
              時刻を得られていて、now >= time_due_ms なら write_time()
```

### time_work_handler

- 時刻を書く対象のノード (`conn != NULL` で、`time_handle != 0`) が 1 台でもあれば、`TIME_CHECK_S` (10 秒) 後に、自分をもう一度予約します。対象がなくなれば、止まります。
- `time_source` が NULL のとき (`CONFIG_THERMO_CLOUD` が無効のビルド) は、時刻の特性の探索そのものを始めないので (`start_time_discovery()`)、`time_handle` が 0 のままで、`time_work` は何も書きません。
- 時刻が得られていて (SNTP の同期のあと)、そのノードの `time_due_ms` を過ぎていれば、`write_time()` を呼びます。

### write_time

- `unix_s` を `uint32_t` にして、リトルエンディアンの 4 バイトを `node->time_buf` に入れます (ノード側の `write_time()` が期待する形式)。
- 応答つきの書き込み (`bt_gatt_write()`) を始めます。結果は `write_time_cb()` で届きます。
- 次の書き込みの時刻は、**成功を見込んで** 3600 秒後 (`TIME_RESYNC_S`) にします。書き込みを始められなかったときは、60 秒後 (`TIME_RETRY_S`) に直します。

### write_time_cb

ノードが書き込みを拒否した (ATT のエラー) ときは、警告を出して、`time_due_ms` を 60 秒後にします。成功なら、`Time written to the node` を出すだけです。

ノードは、次の場合に拒否します (`thermo-node/src/ble.c`)。

| ATT のエラー | 原因 |
|:---|:---|
| `INVALID_OFFSET` | オフセットが 0 でない |
| `INVALID_ATTRIBUTE_LEN` | 長さが 4 バイトでない |
| `WRITE_NOT_PERMITTED` | ノード側にコールバックがない |
| `VALUE_NOT_ALLOWED` | 時刻が RTC の範囲 (2000 年から 2099 年) の外 |
| `UNLIKELY` | RTC の失敗 |

### 時刻の特性の探索 (start_time_discovery、time_discover_cb)

- `start_time_discovery()` は、温度の購読が成功した直後に呼ばれます。`time_source` が NULL なら、何もしません。サービスの範囲 (`service_start` から `service_end`) の中で、時刻の特性を探します。始められなくても、接続は切りません (温度の受信は続けます)。
- `time_discover_cb()` は、見つかったら、値のハンドルを `time_handle` に入れ、`time_due_ms = 0` にして、`time_work` を即時に予約します (予約し直しなので、すでに待っているタイマーは、置き換わります)。見つからなければ、警告を出すだけです。古いノードのファームウェアなど、時刻の特性がなくても動き続けます。

## 補助の関数

| 関数 | 役割 |
|:---|:---|
| `find_node(conn)` | 接続から、`nodes[]` の要素を探す |
| `find_free_node()` | `conn == NULL` の要素を探す |
| `release_node(node)` | `bt_conn_unref()` して、構造体全体を `memset` で 0 にする |
| `adv_hexdump()` | `THERMO_LOG_LEVEL` がデバッグ (4) 以上のときだけ、50 要素に 1 回、16 進ダンプを出す。それ以外は、何もしないマクロ |

## スレッドと排他制御

| 処理 | 動くスレッド |
|:---|:---|
| `scan_cb`、`connected`、`disconnected`、`discover_cb`、`notify_cb`、`write_time_cb` | Bluetooth のスレッド |
| `connect_work_handler`、`time_work_handler` | システムのワークキュー |
| `ble_init`、`ble_scan` (起動時) | メインスレッド |

`nodes[]` と `pending_*` には、ミューテックスなどの排他制御はありません。`pending_node` が非 NULL の間は、`scan_cb()` が新しい依頼を作らない、という約束で、依頼の重なりを防いでいます。`time_work_handler()` は `conn` と `time_handle` を読み、`write_time()` がノードの `write` と `time_buf` を書きます。切断 (`release_node()` がノードを 0 にする) と重なる可能性は、理屈の上ではあります。

## 気づいた点

- ノードの時刻の特性は、認証なしで書き込めます (`thermo-node/src/ble.c` に注記あり)。近くの BLE 機器なら、誰でも、ノードの表示時刻を変えられます。
- `scan_cb()` は、スキャン応答のたびに呼ばれます。Thermo サービスの UUID がない機器は、`bt_data_parse()` の直後に返るので、まわりに機器が多くても、`bt_conn_lookup_addr_le()` などは呼ばれません。接続済みのノードの Thermo のスキャン応答だけが、そこまで進みます。
- `ble_scan()` は、`bt_le_scan_start()` が `-EALREADY` を返したときは、成功として扱います。接続を始めるたびにスキャンを止めて、`connected()` や `disconnected()` で再開する設計なので、再開の呼び出しが重なっても、安全です。

## 関連

- [main.md](main.md): コールバックの登録と、受信した値の扱い
- [switchbot.md](switchbot.md): `switchbot_parse()` と `switchbot_accept()`
- ノード側の GATT: `app/thermo-node/docs/ble.md`
- [index.md](index.md): このディレクトリの文書の一覧
