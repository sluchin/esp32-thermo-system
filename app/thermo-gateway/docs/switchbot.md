# switchbot.c の解析

対象: `app/thermo-gateway/src/switchbot.c` (224 行) と `switchbot.h` (56 行)

SwitchBot 屋外用温湿度計 (Outdoor Meter) の値を、BLE のアドバタイズだけから取り出すためのコードです。接続はしません。Zephyr の関数をほとんど呼ばない (アドレスの比較とコピーだけ) 純粋な計算なので、単体テストはモックを使いません。

役割は 2 つです。

| 関数 | 役割 |
|:---|:---|
| `switchbot_parse()` | アドバタイズデータの 1 要素を解析して、`struct switchbot_ad` にする |
| `switchbot_accept()` | 解析した結果を機器ごとに記録して、送信する値を取り出す (送信の間引き) |

`switchbot_reset()` は、単体テストが前のテストの記録を消すための関数です。通常は呼びません。

## アドバタイズデータの形式

1 台の温湿度計の情報は、2 種類のアドバタイズデータに分かれて届きます (SwitchBot の公開仕様 OpenWonderLabs/SwitchBotAPI-BLE に基づく)。

### サービスデータ (機種と電池残量)

AD の種類は `BT_DATA_SVC_DATA16` です。

| 位置 | 内容 |
|:---|:---|
| 0 から 1 | UUID 0xFD3D (リトルエンディアンなので、`3D FD`) |
| 2 | 機種コード (下位 7 ビット)。屋外用温湿度計は 0x77 (ASCII の `w`) |
| 3 | 状態 (使わない) |
| 4 | 電池残量 [%] (下位 7 ビット) |

UUID のあとが 3 バイト以上あれば、解析できます (`SERVICE_PAYLOAD_LEN`)。

### 製造者データ (温度と湿度)

AD の種類は `BT_DATA_MANUFACTURER_DATA` です。

| 位置 | 内容 |
|:---|:---|
| 0 から 1 | 会社 ID 0x0969 (SwitchBot。リトルエンディアンなので、`69 09`) |
| 2 から 7 | 機器の MAC アドレス (6 バイト) |
| 8 から 9 | 不明 (2 バイト。読み飛ばす) |
| 10 | 温度の小数部。下位 4 ビット (10 分の 1 ℃) |
| 11 | 温度の整数部。下位 7 ビットが整数部で、最上位ビットが符号 (1 なら 0 ℃ 以上、0 なら 0 ℃ 未満) |
| 12 | 湿度 [%]。下位 7 ビット |

位置は、会社 ID の先頭からの数え方です。コードでは、会社 ID のあと (位置 2 から) を `data` として渡し、`MFR_ENV_OFFSET` (8) 番目から温度と湿度を読みます。会社 ID のあとが 11 バイト以上あれば、解析できます (`MFR_PAYLOAD_LEN`)。

温度の計算は、次のとおりです。

```text
magnitude = (整数部の下位 7 ビット) * 10 + (小数部の下位 4 ビット)
temp_x10  = (整数部の最上位ビットが 1) ? +magnitude : -magnitude
```

例: 整数部のバイトが `0x97` (最上位ビット 1、下位 7 ビット 23)、小数部のバイトが `0x05` なら、`+235` で 23.5 ℃。整数部のバイトが `0x17` なら (最上位ビット 0)、`-235` で -23.5 ℃です。

湿度が 100 を超えていたら、壊れたデータとして、`false` を返します。

## struct switchbot_ad (解析の結果)

| メンバー | 内容 |
|:---|:---|
| `kind` | `SWITCHBOT_NONE` (対象外)、`SWITCHBOT_INFO` (機種と電池残量)、`SWITCHBOT_ENV` (温度と湿度) |
| `model`、`battery` | `INFO` のとき有効 |
| `temp_x10`、`humidity` | `ENV` のとき有効 |

`SWITCHBOT_NONE` は 0 です。呼び出し側 (`ble.c` の `scan_result`) が、0 で初期化してから使うので、解析に失敗した (`false` を返した) ときは、`kind` が `NONE` のままです。`switchbot_parse()` は、`false` のとき `out` を変えません。

## switchbot_parse

```text
種類が SVC_DATA16 で、長さ ≥ 2 で、先頭が 3D FD  → parse_service_data(2 バイト目から)
種類が MANUFACTURER_DATA で、長さ ≥ 2 で、先頭が 69 09 → parse_manufacturer_data(2 バイト目から)
それ以外 → false
```

UUID と会社 ID の 2 バイトは、数値ではなく、バイトごとに比べます (`SERVICE_UUID_LO`、`SERVICE_UUID_HI`、`COMPANY_ID_LO`、`COMPANY_ID_HI`)。バイト順の変換が要らず、アラインメントの問題も起きません。

## switchbot_accept: 機器ごとの記録と間引き

```text
find_device(addr)            ← 記録を探す。なければ、空きを割り当てる。空きもなければ NULL
  NULL                       → false (機器が 5 台目以降)
kind == INFO                 → 機種と電池残量を記録して false
kind != ENV                  → false
機種 != 0x77                 → false (屋外用温湿度計と確認できていない)
reported && (now - last) < 間隔 → false
それ以外                     → reported = true、last = now、値を out に入れて true
```

- **機種の確認**: 製造者データ (温度と湿度) は、ほかの SwitchBot の機種でも似た形で届くことがあります。そのため、サービスデータで `0x77` と確認できた機器の分だけを使います。サービスデータが届く前の製造者データは、捨てます。
- **間引きの間隔**: `CONFIG_THERMO_SWITCHBOT_INTERVAL_MS` です。`Kconfig` の既定は 1000 ms ですが、`prj.conf` で 10000 ms (10 秒) にしています。温湿度計は、アドバタイズのたびに値を載せるので、間引かないと、AWS IoT Core への送信が多すぎます。
- **最初の 1 回**: `reported` が `false` の間は、間隔を見ないので、すぐに返します。
- **時間の計算**: `now_ms - last_ms` は、`uint32_t` の引き算です。`k_uptime_get_32()` が一周 (約 49.7 日) しても、差は正しく求まります。
- **電池残量**: 最後に受け取ったサービスデータの値を、`out->battery` に入れます。まだ受け取っていなければ、`SWITCHBOT_BATTERY_UNKNOWN` (-1) です。機種が確認できている (サービスデータを受け取った) ので、通常は、値が入っています。

### 機器の記録 (struct sb_device)

| メンバー | 内容 |
|:---|:---|
| `addr` | 機器のアドレス |
| `used` | 使っているか |
| `model` | 機種コード。サービスデータを受け取るまでは 0 |
| `battery` | 電池残量。わからなければ -1 |
| `reported` | 1 回でも値を返したか |
| `last_ms` | 最後に値を返した稼働時間 |

記録は、`devices[SWITCHBOT_MAX_DEVICES]` (4 台) の固定の配列です。`find_device()` は、アドレスが同じ記録を探します。見つからなければ、最初の空きの要素を 0 で初期化して、アドレスをコピーし、`used` を立てて、電池残量を -1 にします。一度割り当てた記録は、解放しません。5 台目の機器は、無視されます。

## 補助の関数

| 関数 | 役割 |
|:---|:---|
| `parse_service_data()` | 長さを確認して、`model` と `battery` を取り出す (どちらも `& 0x7F`) |
| `parse_manufacturer_data()` | 長さと湿度の範囲を確認して、温度と湿度を計算する |
| `find_device()` | アドレスから記録を探す。なければ割り当てる |

## 気づいた点

- 機器の記録は、4 台で固定です。ゲートウェイのまわりに SwitchBot の温湿度計が 5 台以上あると、5 台目以降は、記録されません (別の機種の機器も、サービスデータで記録が使われます。`switchbot_accept()` は、`INFO` を受け取ると、機種を見る前に、記録を割り当てるため)。屋外用ではない SwitchBot が近くにあると、枠を使います。
- 機器は、アドレス (`bt_addr_le_eq()`) だけで見分けます。機器がアドレスを変えると (ランダムなプライベートアドレスなど)、別の機器として、記録の枠を使います。
- 製造者データの「不明の 2 バイト」は、読んでいません。仕様が公開されていないためです。

## 関連

- [ble.md](ble.md): `switchbot_parse()` を呼ぶ `parse_ad()` と、コールバックの呼び出し
- [main.md](main.md): `switchbot_accept()` を呼ぶ `on_switchbot()`
- `app/common/switchbot_sample.h`: 送信する値の構造体 `struct switchbot_sample`
- [index.md](index.md): このディレクトリの文書の一覧
