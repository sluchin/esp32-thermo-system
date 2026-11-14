# payload.c の解析

対象: `app/common/payload.c` (214 行) と `payload.h` (34 行)

AWS IoT Core に送る、MQTT のトピックと JSON のペイロードを作ります。文字列を作るだけで Zephyr の関数を呼ばないので、単体テストではモックを使いません。

## 公開する関数

| 関数 | 作るもの |
|:---|:---|
| `payload_format_topic()` | Thermo ノードのトピック |
| `payload_format_temperature()` | Thermo ノードのペイロード |
| `payload_format_switchbot_topic()` | SwitchBot のトピック |
| `payload_format_switchbot()` | SwitchBot のペイロード |

どれも、書いた文字数 (NUL を除く) を返します。バッファが足りなければ、`-ENOSPC` を返します。

## トピック

| 種類 | 形式 | 例 |
|:---|:---|:---|
| Thermo ノード | `thermo/<クライアント ID>/<ノードのアドレス>/temperature` | `thermo/gateway-01/00:AA:01:00:00:42/temperature` |
| SwitchBot | `thermo/<クライアント ID>/switchbot/<機器のアドレス>` | `thermo/gateway-01/switchbot/B0:E9:FE:12:34:56` |

アドレスは、`format_addr()` で「大文字の 16 進の 6 バイトを、コロンで区切った形」にします。`bt_addr_le_to_str()` は末尾に `(public)` などが付くので、使いません。上位のバイトから順に出します (`val[5]` から `val[0]`)。

## ペイロード

### Thermo ノード

```json
{"node":"00:AA:01:00:00:42","type":"thermo","temperature_c":23.5,"humidity":45.0,"pressure_hpa":1013.2,"uptime_ms":123456,"timestamp":1791765471}
```

- 湿度が `THERMO_HUMIDITY_NONE` (0xFFFF) のときは、`humidity` を出しません。
- 気圧が `THERMO_PRESSURE_NONE` (0xFFFF) のときは、`pressure_hpa` を出しません。単位は hPa です (キーの名前に単位を入れるのは、`temperature_c` にそろえたもの)。
- UNIX 時刻が負 (分からない) のときは、`timestamp` を出しません。
- 温度、湿度、気圧は、10 分の 1 の桁までの固定小数です。温度は符号を別に出すので、`-0.5` も正しく出ます。

### SwitchBot

```json
{"node":"B0:E9:FE:12:34:56","type":"switchbot","temperature_c":23.4,"humidity":60,"battery":87,"uptime_ms":123456,"timestamp":1791765471}
```

- 湿度は整数です。
- 電池残量が負 (`SWITCHBOT_BATTERY_UNKNOWN`) のときは、`battery` を出しません。

## 実装の要点

- `snprintf()` で作ります。戻り値が `size` 以上なら、切り捨てられたものとして、`check_length()` が `-ENOSPC` にします (`snprintf()` が失敗したときの負の値も、同じ扱い)。
- 任意の項目 (`humidity`、`pressure_hpa`、`battery`、`timestamp`) は、先に別の小さなバッファに作っておき、あれば本体に差し込みます。
- バッファの大きさ (`ADDR_STR_SIZE`、`TIMESTAMP_FIELD_SIZE` など) は、最大の桁数から決めています。`cloud.c` の `PAYLOAD_SIZE` (176) が、全ての項目が最大のときの長さに足りることは、`BUILD_ASSERT` で確かめています。最大の長さは、Thermo ノードの 163 文字 + NUL です (温度 -3276.8、湿度 6553.4、気圧 6553.4、稼働時間と UNIX 時刻が最大の桁)。単体テストの `test_payload_max_values` でも、長さを確かめます。

## 注意点

- ペイロードの `node` と、トピックの中のアドレスは、同じ文字列です。ノードが直接送るときは、ノード自身のクライアント ID がトピックに入ります。ゲートウェイを経由した値では、ゲートウェイのクライアント ID が入ります。そのため、同じノードの値が**別のトピック**に分かれて届きます。AWS 側のルールでは、`thermo/+/...` のように、クライアント ID を任意にして拾う必要があります。
- クライアント ID は、トピックだけに入り、JSON のペイロードには入りません。ペイロードの文字列はアドレスだけなので、エスケープは要りません。

## 関連

- [cloud.md](cloud.md): `publish_sample()` が呼ぶ
- [index.md](index.md): このディレクトリの文書の一覧
