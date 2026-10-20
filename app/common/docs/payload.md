# payload.c の解析

対象: `app/common/payload.c` (214 行) と `payload.h` (34 行)

AWS IoT Core に送る MQTT のトピックと、JSON のペイロードを作ります。文字列を作るだけの関数で、Zephyr の関数を呼ばないので、単体テストはモックを使いません。

## 公開する関数

| 関数 | 作るもの |
|:---|:---|
| `payload_format_topic()` | Thermo ノードのトピック |
| `payload_format_temperature()` | Thermo ノードのペイロード |
| `payload_format_switchbot_topic()` | SwitchBot のトピック |
| `payload_format_switchbot()` | SwitchBot のペイロード |

どれも、書いた文字数 (NUL を除く) を返し、バッファが足りなければ `-ENOSPC` を返します。

## トピック

| 種類 | 形式 | 例 |
|:---|:---|:---|
| Thermo ノード | `thermo/<クライアント ID>/<ノードのアドレス>/temperature` | `thermo/gateway-01/00:AA:01:00:00:42/temperature` |
| SwitchBot | `thermo/<クライアント ID>/switchbot/<機器のアドレス>` | `thermo/gateway-01/switchbot/B0:E9:FE:12:34:56` |

アドレスは、`format_addr()` で「大文字の 16 進を、コロンで区切った 6 バイト」にします。`bt_addr_le_to_str()` は、末尾に ` (public)` などが付くので、使いません。上位のバイトから順に出します (`val[5]` から `val[0]`)。

## ペイロード

### Thermo ノード

```json
{"node":"00:AA:01:00:00:42","type":"thermo","temperature_c":23.5,"humidity":45.0,"pressure_hpa":1013.2,"uptime_ms":123456,"timestamp":1791765471}
```

- 湿度が `THERMO_HUMIDITY_NONE` (0xFFFF) のときは、`humidity` を出しません。
- 気圧が `THERMO_PRESSURE_NONE` (0xFFFF) のときは、`pressure_hpa` を出しません。単位は hPa です (キーの名前に単位を入れるのは、`temperature_c` と同じ)。
- UNIX 時刻が負 (わからない) のときは、`timestamp` を出しません。
- 温度と湿度と気圧は、10 分の 1 の桁までの固定小数です。温度は符号を別に出すので、`-0.5` も正しく出ます。

### SwitchBot

```json
{"node":"B0:E9:FE:12:34:56","type":"switchbot","temperature_c":23.4,"humidity":60,"battery":87,"uptime_ms":123456,"timestamp":1791765471}
```

- 湿度は整数です。
- 電池残量が負 (`SWITCHBOT_BATTERY_UNKNOWN`) のときは、`battery` を出しません。

## 実装の要点

- `snprintf()` で作ります。切り捨てを見分けるために、`check_length()` が、戻り値が `size` 以上なら `-ENOSPC` にします (`snprintf()` の失敗の負の値も同じ扱い)。
- 任意の項目 (`humidity`、`pressure_hpa`、`battery`、`timestamp`) は、先に別の小さなバッファに作り、あれば、本体に差し込みます。
- バッファの大きさ (`ADDR_STR_SIZE`、`TIMESTAMP_FIELD_SIZE` など) は、最大の桁数から決めています。`cloud.c` の `PAYLOAD_SIZE` (176) が、全ての項目の最大の長さ (Thermo ノードの 163 文字 + NUL。温度 -3276.8、湿度 6553.4、気圧 6553.4、稼働時間と UNIX 時刻が最大の桁) に足りることは、`BUILD_ASSERT` で確認しています (長さは、単体テストの `test_payload_max_values` でも確かめます)。

## 気づいた点

- ペイロードの `node` と、トピックの中のアドレスは、同じ文字列です。ノードが直接送るときは、ノード自身のクライアント ID が、トピックに入ります。ゲートウェイを経由した値は、ゲートウェイのクライアント ID です。同じノードの値が、**別のトピック**に分かれて届きます。AWS 側のルールで、`thermo/+/...` のように、クライアント ID を任意にして拾う必要があります。
- 文字列の値 (クライアント ID) は、JSON のペイロードには入りません (トピックだけ)。ペイロードの文字列は、アドレスだけなので、エスケープが要りません。

## 関連

- [cloud.md](cloud.md): `publish_sample()` が呼ぶ
- [index.md](index.md): このディレクトリの文書の一覧
