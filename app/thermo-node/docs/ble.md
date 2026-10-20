# ble.c の解析

対象: `app/thermo-node/src/ble.c` (396 行) と `ble.h` (46 行)

ノードの BLE です。周辺機器 (Peripheral) として、アドバタイズを出し、ゲートウェイからの接続を待ちます。つながったゲートウェイには、温度と湿度と気圧を通知 (notify) し、ゲートウェイが書き込んだ時刻を受け取ります。

## 公開する関数 (ble.h)

| 関数 | 役割 |
|:---|:---|
| `ble_init()` | `bt_enable()`、GATT のサービスの登録、接続のコールバックの登録 |
| `ble_advertise()` | 接続可能なアドバタイズを開始 |
| `ble_notify_temperature()` | 値を更新して、購読しているゲートウェイへ通知 |
| `ble_is_connected()` | ゲートウェイとつながっているか |
| `ble_get_address()` | このノードの BLE のアドレスを取得 |
| `ble_set_time_callback()` | 時刻を受け取ったときのコールバックを登録 (`CONFIG_THERMO_RTC` のときだけ) |

## アドバタイズ

接続可能で高速な `BT_LE_ADV_CONN_FAST_1` で始めます。

| データ | 内容 |
|:---|:---|
| アドバタイズデータ `ad[]` | フラグ (LE 一般発見可能 + BR/EDR 非対応) と、完全なデバイス名 (`CONFIG_BT_DEVICE_NAME`。`prj.conf` では `Thermo-Node`) |
| スキャン応答 `sd[]` | Thermo サービスの UUID (128 ビット) |

UUID を、アドバタイズデータではなく、スキャン応答に入れるのは、128 ビットの UUID が 18 バイトあり、名前と合わせると、アドバタイズデータの上限 31 バイトに入らないためです。そのため、ゲートウェイは、アクティブスキャンでスキャン応答も受け取って、ノードを見分けます (`thermo-gateway` の `docs/ble.md`)。

## GATT のサービス

`thermo_attrs[]` は、動的に登録する (`CONFIG_BT_GATT_DYNAMIC_DB=y`) Thermo サービスです。UUID は、`app/common/thermo_ble_uuid.h` にあります。

| 位置 | 属性 | 備考 |
|:---|:---|:---|
| 0 | プライマリサービス | UUID 9F3C1A00-... |
| 1 | 温度の特性の宣言 | 読み取り (READ) と通知 (NOTIFY) |
| 2 | 温度の特性の値 | UUID 9F3C1A01-...。読み取りは `read_temperature()` |
| 3 | CCC (通知の設定) | 読み書きできる |
| 4 | 時刻の特性の宣言 | `CONFIG_THERMO_RTC` のときだけ。書き込みだけ (WRITE) |
| 5 | 時刻の特性の値 | UUID 9F3C1A02-...。書き込みは `write_time()` |

`ATTR_INDEX_TEMPERATURE_VALUE` (2) が、通知に使う属性の位置です。属性の並びを変えるときは、この値と、ゲートウェイが CCC を探す位置 (値のハンドル + 1) の前提を、確認してください。

### 温度の値の形式 (6 バイト)

| 位置 | 内容 |
|:---|:---|
| 0 から 1 | 温度 (`int16_t`、℃ の 10 倍、リトルエンディアン) |
| 2 から 3 | 湿度 (`uint16_t`、% の 10 倍、リトルエンディアン)。0xFFFF は、湿度なし |
| 4 から 5 | 気圧 (`uint16_t`、hPa の 10 倍、リトルエンディアン)。0xFFFF は、気圧なし |

気圧を足す前は 4 バイト (温度と湿度) でした。ゲートウェイは、4 バイトの通知も受け取り、気圧なしとして扱います (`THERMO_TEMPERATURE_SIZE_NO_PRESSURE`)。値を読むほかのアプリ (thermo-term など) は、先頭の 4 バイトの意味が変わらないので、4 バイトだけを読んでも、温度と湿度は正しく読めます (長さを 4 バイトに限っているアプリは、6 バイトを受け付けるように直します)。

`pack_value()` が、`sys_put_le16()` で作ります。通知と読み取りの両方に、同じ関数を使います。

## ble_notify_temperature

```text
latest_temp_x10 / latest_humidity_x10 / latest_pressure_x10 を更新   ← 通知できなくても、読み取りで取れるように
pack_value() → bt_gatt_notify(NULL, &thermo_attrs[2], value, 6)
  -ENOTCONN → 成功として返す
  その他の負 → LOG_ERR して返す
```

`bt_gatt_notify()` に `NULL` の接続を渡すと、通知を購読している全ての接続へ送ります。購読している接続がないときは、`-ENOTCONN` が返ります (Zephyr の実装)。ゲートウェイにつながっていないときと、つながっているが購読していないときは、どちらも、エラーにしません。メインループは、通知の失敗を `LOG_ERR` に出すだけで、次の測定でやり直します。

## ble_is_connected と connection_count

`connection_count` は、`connected()` で増やし、`disconnected()` で減らします (0 のときは減らしません)。`ble_is_connected()` は、1 以上かどうかを返します。

- 数えているのは、BLE の接続です。通知を購読しているかは、見ません。
- `volatile` ですが、アトミックではありません。更新するのは Bluetooth のスレッド、読むのはメインスレッドです。1 ワードの読み書きなので、値が壊れることはありません。

`direct.c` が、この値で「ゲートウェイにつながっているか」を判断します。

## 時刻の受け取り (write_time)

`CONFIG_THERMO_RTC` が有効なときだけ、ビルドされます。ゲートウェイが、時刻の特性に書き込むと、Bluetooth のスレッドで `write_time()` が呼ばれます。

| 確認 | 失敗したときの ATT のエラー |
|:---|:---|
| `offset == 0` | `BT_ATT_ERR_INVALID_OFFSET` |
| `len == 4` (`THERMO_TIME_SIZE`) | `BT_ATT_ERR_INVALID_ATTRIBUTE_LEN` |
| `time_cb != NULL` | `BT_ATT_ERR_WRITE_NOT_PERMITTED` |
| コールバックが `-ERANGE` を返さない | `BT_ATT_ERR_VALUE_NOT_ALLOWED` (警告を出す) |
| コールバックが成功する | `BT_ATT_ERR_UNLIKELY` (ほかの失敗) |

値は、UTC の UNIX 時刻 (`uint32_t`、リトルエンディアン) です。`main.c` は、コールバックに `node_time_set()` を登録するので、時刻が 2000 年から 2099 年の外なら、`-ERANGE` で、ゲートウェイに拒否が返ります。成功したら、書き込まれた長さ (4) を返して、`Time set by the gateway` をログに出します。

認証はしません。近くの BLE 機器は、誰でも、時刻を書き込めます。影響するのは、表示と SD カードに保存する測定値の時刻です。

## 接続のコールバックとアドバタイズの再開

```text
connected(err)     失敗 → ログだけ / 成功 → connection_count++
                   connection_count < CONFIG_BT_MAX_CONN なら ble_advertise() (-EALREADY は無視)
disconnected()     connection_count-- (0 未満にしない)
recycled()         ble_advertise() を呼ぶ (-EALREADY は無視)
```

Zephyr は、接続すると、アドバタイズを止めます。`connected()` は、接続できる数 (`CONFIG_BT_MAX_CONN`) に空きがあれば、アドバタイズを続けて、ほかの機器 (スマートフォンのアプリ thermo-term など) が見つけられるようにします。`prj.conf` は `CONFIG_BT_MAX_CONN=1` なので、実機では、続けません (空きがない)。2 にすると、BLE が使うヒープが増えて、WiFi と TLS のぶんが足りなくなり、直接の送信が届かなくなるためです (RAM は、99.9% 使われている)。単体テスト (`tests/ble`) は、2 にして、続ける経路を確かめます。

切断のコールバック (`disconnected()`) の中では、スタックが、まだ接続のオブジェクトを持っています。そこで接続可能なアドバタイズを始めると、空きがなく、`-ENOMEM` で失敗することがあります (Zephyr の仕様)。そのため、アドバタイズの再開は、オブジェクトが解放されたあとに呼ばれる `recycled()` で行います。これで、ゲートウェイが切れても、自動で、もう一度見つかるようになります。

## ble_get_address

`bt_id_get()` で、ID 0 のアドレスを取って返します (`count == 0` なら `-ENOENT`)。ゲートウェイがノードを見るアドレスと同じなので、`direct.c` が、AWS IoT Core のトピックと payload の識別子として使います。直接送信した値と、ゲートウェイ経由の値が、同じノードの値として並びます。`ble_init()` のあとで呼びます。

## 気づいた点

- 通知の購読者がいないことは、エラーにならないので、「ゲートウェイにつながっていない」ことの判断は `connection_count` で行います。購読が切れていても (接続だけが残っているとき)、`ble_is_connected()` は true を返します。
- `connection_count` は、つないできた相手を区別しません。ゲートウェイの代わりに、スマートフォン (thermo-term の BLE のタブ) がつないでいるときも、`ble_is_connected()` は true を返すので、`direct.c` は、ゲートウェイにつながっているとみなして、WiFi での直接の送信を始めません。区別するなら、時刻の特性に書き込んだ接続 (ゲートウェイ) だけを数える方法があります。
- 時刻の特性は、暗号化や認証を要求しません (`BT_GATT_PERM_WRITE`)。より厳しくするなら、`BT_GATT_PERM_WRITE_AUTHEN` などと、ペアリングが要ります。

## 関連

- [main.md](main.md): `ble_init()` の呼び出し順、通知の呼び出し
- [direct.md](direct.md): `ble_is_connected()` と `ble_get_address()` の利用
- [node_time.md](node_time.md): `node_time_set()`
- [index.md](index.md): このディレクトリの文書の一覧
