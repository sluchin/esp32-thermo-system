# デバッグログの読み方 (thermo-gateway)

Thermo Gateway のデバッグログ (`THERMO_DEBUG_LOG=ON`) の、取り方と、読み方です。例は、実機 (XIAO ESP32C3) で、SwitchBot 屋外用温湿度計 (`ED:2E:C4:46:3B:11`) を受信して、AWS IoT Core に送信したときのログです。

> Thermo ノードの実機がないので、ノードから受信した温度 (`ble.c` の `Notification` の 16 進ダンプ) と、ノードの温度の送信のログは、この文書に載せていません (実機では未確認)。確認できているのは、SwitchBot の受信と送信だけです。

## 1. ログの取り方

```bash
# デバッグログ ON でビルドして、書き込む
THERMO_DEBUG_LOG=ON docker compose run --rm build-thermo-gateway
scripts/flash.sh gateway /dev/ttyACM0

# シリアルコンソールをつなぐ (ファイルにも保存する)
picocom -b 115200 --logfile debug.log /dev/ttyACM0
```

- `picocom` は、`Ctrl-A` のあと `Ctrl-X` で終了します。
- 通常のビルドに戻すときは、`THERMO_DEBUG_LOG=ON` を付けずに、ビルドし直します (`build/thermo-gateway` を、同じ場所に作るので、そのまま上書きされます)。
- デバッグログは、アプリのソース (`app/thermo-gateway/src/`) だけで出ます。Zephyr の内部 (Bluetooth、ネットワーク、mbedTLS) のログは、通常と同じです (詳しくは [README.md](README.md) の「デバッグログ」)。

## 2. 1 行の形式

```
[00:03:43.972,000] <dbg> thermo_cloud: publish_sample: MQTT publish (id 18, QoS 1) to thermo/...
 ^^^^^^^^^^^^^^^^^  ^^^^^ ^^^^^^^^^^^^  ^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 起動からの時間      レベル モジュール    関数名         メッセージ
```

| 部分 | 意味 |
|:---|:---|
| `[00:03:43.972,000]` | 起動からの時間 (時:分:秒.ミリ秒,マイクロ秒)。時刻 (UNIX 時間) ではありません |
| `<inf>` / `<dbg>` / `<wrn>` / `<err>` | レベル。`<dbg>` は、デバッグログ ON のビルドだけで出ます |
| `thermo_gateway` など | ログのモジュール名 (下の表) |
| `parse_ad:` など | 関数名。`<dbg>` のときだけ付きます |

| モジュール名 | ソース | 内容 |
|:---|:---|:---|
| `thermo_gateway` | `main.c` | メインループ。SwitchBot の値を受け取ったときの要約 |
| `ble_thermo_gateway` | `ble.c` | BLE のスキャン、アドバタイズの受信 |
| `thermo_cloud` | `cloud.c` | MQTT の接続、送信、確認応答 |
| `thermo_wifi` | `wifi_link.c` | WiFi の接続 |
| `thermo_ntp` | `ntp.c` | SNTP の時刻合わせ |
| `thermo_cfg` | `cfg.c` | 設定の保存と読み込み |

## 3. 16 進ダンプの形式

`LOG_HEXDUMP_DBG` は、データを、16 バイトずつ、次の形で出します。

```
                                       34 3a 34 36 3a 33 42 3a  31 31 22 2c 22 74 79 70 |4:46:3B: 11","typ
                                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^
                                       16 進数 (8 バイトごとに、空白が 1 つ増える)         ASCII (表示できない文字は、ピリオド)
```

- 右側の ASCII は、8 バイトめと 9 バイトめの間に、空白が 1 つ入ります (`|4:46:3B: 11","typ`)。実際のデータには、この空白は、入っていません。
- データが 16 バイトに満たない行は、残りが空白で埋まります。

## 4. 受信した SwitchBot のアドバタイズ

### 4.1 ログの意味

BLE で、SwitchBot のアドバタイズを受信すると、解析できた要素ごとに、次の 2 行が出ます (`ble.c` の `parse_ad`)。

```
[00:03:43.958,000] <dbg> ble_thermo_gateway: parse_ad: SwitchBot advertising data (AD type 0xFF)
[00:03:43.961,000] <dbg> ble_thermo_gateway: parse_ad: SwitchBot advertising data
                                             69 09 ed 2e c4 46 3b 11  11 02 08 95 36 00       |i....F;. ....6.
```

- `AD type` は、アドバタイズデータの要素の種類です。

| AD type | 名前 | 中身 |
|:---|:---|:---|
| `0x16` | サービスデータ (16 bit UUID) | UUID `0xFD3D` に続けて、機種、状態、電池残量 |
| `0xFF` | 製造者データ | 会社 ID `0x0969` (SwitchBot) に続けて、MAC アドレス、温度、湿度 |

- 屋外用温湿度計は、サービスデータと製造者データを、別のパケット (アドバタイズとスキャン応答) で、約 2 秒ごとに送ります。
- 16 進ダンプは、AD 構造の、長さと種類の 2 バイトを除いた、中身だけです。
- 受信のたびに出ます (10 秒の間引きより前)。そのため、ログの量が多くなります。

### 4.2 サービスデータ (AD type `0x16`) の読み方

```
3d fd 77 00 64
^^^^^ ^^ ^^ ^^
UUID  機種 状態 電池残量
```

| バイト | 値 | 意味 |
|:---|:---|:---|
| `3d fd` | `0xFD3D` | SwitchBot のサービス UUID (リトルエンディアン) |
| `77` | `'w'` | 機種コード。`0x77` (ASCII の `w`) が、屋外用温湿度計 |
| `00` | | 状態 (使っていない) |
| `64` | 100 | 電池残量 [%] (下位 7 ビット) |

ログには、別の機種のものも出ます。

```
3d fd 6f 80 64
```

機種コードが `0x6F` (ASCII の `o`) で、屋外用温湿度計 (`0x77`) ではありません。SwitchBot の仕様では、鍵 (Lock) の機種コードとされています (未確認)。解析では、機種と電池残量を記録するだけで、AWS には、送信しません。

### 4.3 製造者データ (AD type `0xFF`) の読み方

```
69 09 | ed 2e c4 46 3b 11 | 11 02 | 08 95 36 | 00
会社ID  MAC アドレス        不明     温湿度     不明
```

| バイト | 値 | 意味 |
|:---|:---|:---|
| `69 09` | `0x0969` | SwitchBot の会社 ID (リトルエンディアン) |
| `ed 2e c4 46 3b 11` | | MAC アドレス `ED:2E:C4:46:3B:11` |
| `11 02` | | 用途は不明 (使っていない) |
| `08` | | 温度の小数部 (下位 4 ビット): `.8` |
| `95` | `1001 0101` | 最上位ビットが 1 なら、0 ℃ 以上。下位 7 ビット `0x15` = 21 (整数部) |
| `36` | 54 | 湿度 [%] (下位 7 ビット) |

温度は、整数部 21 と、小数部 8 から、**21.8 ℃**、湿度は **54 %** です。このあとの `thermo_gateway: SwitchBot ED:2E:C4:46:3B:11 (random): 21.8 C, 54 %, battery 100 %` と合っています (電池 100 % は、サービスデータの `64`)。

もう 1 台の SwitchBot の製造者データも、出ています。

```
69 09 c9 4a 74 cf 77 45  3c 58 00 00 00 00
```

MAC アドレスは `C9:4A:74:CF:77:45` で、温湿度計 (`ED:2E:...`) とは別の機器です。サービスデータの機種が、屋外用温湿度計ではないので、AWS には、送信しません。

### 4.4 ほかの機器のアドバタイズ

`ble_thermo_gateway: adv_hexdump: Advertising data` は、SwitchBot 以外も含めて、まわりの BLE 機器のアドバタイズデータです。大量に届くので、**要素 50 個に 1 個だけ**出します (`ble.c` の `ADV_HEXDUMP_EVERY`)。

| 例 | 意味 (推定) |
|:---|:---|
| `1a` / `0c` / `06` | 1 バイトだけのもの。Flags (AD type `0x01`) のことが多い |
| `4c 00 ...` | 先頭が `4c 00`: Apple の会社 ID `0x004C` (iPhone などの通知) |
| `4b 57 31 30 35 2d 47 30 31` | ASCII で `KW105-G01`。機器の名前 |

どれも、このシステムとは、関係ありません。

## 5. 値を受け取ってから AWS に届くまで

1 回の送信の流れです。

```
[00:03:43.958,000] <dbg> ble_thermo_gateway: parse_ad: SwitchBot advertising data (AD type 0xFF)
[00:03:43.961,000] <dbg> ble_thermo_gateway: parse_ad: SwitchBot advertising data
                                             69 09 ed 2e c4 46 3b 11  11 02 08 95 36 00
[00:03:43.964,000] <inf> thermo_gateway: SwitchBot ED:2E:C4:46:3B:11 (random): 21.8 C, 54 %, battery 100 %
[00:03:43.966,000] <dbg> ble_thermo_gateway: parse_ad: SwitchBot advertising data (AD type 0x16)
[00:03:43.968,000] <dbg> ble_thermo_gateway: parse_ad: SwitchBot advertising data
                                             3d fd 77 00 64
[00:03:43.972,000] <dbg> thermo_cloud: publish_sample: MQTT publish (id 18, QoS 1) to thermo/gateway-01/switchbot/ED:2E:C4:46:3B:11
[00:03:43.975,000] <dbg> thermo_cloud: publish_sample: MQTT publish payload
                                       7b 22 6e 6f 64 65 22 3a  22 45 44 3a 32 45 3a 43 |{"node": "ED:2E:C
                                       ...
[00:03:44.996,000] <dbg> thermo_cloud: mqtt_evt_handler: Publish acknowledged (id 18)
```

| 時間 | ログ | 意味 |
|:---|:---|:---|
| 43.958 | `parse_ad` (`0xFF`) と 16 進ダンプ | 製造者データを受信 (温度と湿度)。解析できた |
| 43.964 | `<inf> thermo_gateway: SwitchBot ...` | **送信する値に決まった**。同じ機器は、10 秒に 1 回 (`CONFIG_THERMO_SWITCHBOT_INTERVAL_MS`) だけ。この行は、デバッグ版でなくても、出る |
| 43.966 | `parse_ad` (`0x16`) | サービスデータを受信 (機種と電池残量) |
| 43.972 | `MQTT publish (id 18, QoS 1) to ...` | AWS IoT Core に、送信を始めた。トピックと、メッセージ ID が分かる |
| 43.975 | `MQTT publish payload` と 16 進ダンプ | 送信するペイロード (JSON) |
| 44.996 | `Publish acknowledged (id 18)` | AWS から、確認応答 (PUBACK) が届いた。約 1 秒後。**これが出れば、AWS に届いています** |

`<inf> thermo_gateway: SwitchBot ...` が出ない回は、10 秒の間引きで、受信した値を、送信しなかったことを示します。そのあいだも、`parse_ad` の行は、受信のたびに出ます。

### 5.1 ペイロードの 16 進ダンプ

```
7b 22 6e 6f 64 65 22 3a  22 45 44 3a 32 45 3a 43 |{"node": "ED:2E:C
34 3a 34 36 3a 33 42 3a  31 31 22 2c 22 74 79 70 |4:46:3B: 11","typ
65 22 3a 22 73 77 69 74  63 68 62 6f 74 22 2c 22 |e":"swit chbot","
74 65 6d 70 65 72 61 74  75 72 65 5f 63 22 3a 32 |temperat ure_c":2
31 2e 38 2c 22 68 75 6d  69 64 69 74 79 22 3a 35 |1.8,"hum idity":5
34 2c 22 62 61 74 74 65  72 79 22 3a 31 30 30 2c |4,"batte ry":100,
22 75 70 74 69 6d 65 5f  6d 73 22 3a 32 32 33 39 |"uptime_ ms":2239
36 36 2c 22 74 69 6d 65  73 74 61 6d 70 22 3a 31 |66,"time stamp":1
37 39 31 33 33 38 31 33  31 7d                   |79133813 1}
```

ASCII の部分を、つなげると、次の JSON です。

```json
{"node":"ED:2E:C4:46:3B:11","type":"switchbot","temperature_c":21.8,"humidity":54,"battery":100,"uptime_ms":223966,"timestamp":1791338131}
```

| 項目 | 意味 |
|:---|:---|
| `node` | SwitchBot の MAC アドレス |
| `temperature_c`、`humidity`、`battery` | 温度 [℃]、湿度 [%]、電池残量 [%]。4.3 の値と同じ |
| `uptime_ms` | ゲートウェイが、受信したときの、起動からの時間 [ミリ秒] (`[00:03:43...]` の約 223.9 秒と合う) |
| `timestamp` | 受信したときの UNIX 時刻 [秒]。SNTP で時計が合っているときだけ入る |

このペイロードは、AWS コンソールの MQTT テストクライアントに表示されるものと、同じです ([AWS_SETUP.md](AWS_SETUP.md) の 4)。

## 6. ログの確認のしかた

ファイルに保存したログ (`debug.log`) から、必要な行だけを取り出す例です。

```bash
# 送信した値 (10 秒ごと)
grep "<inf> thermo_gateway: SwitchBot" debug.log

# 送信と、確認応答
grep -E "MQTT publish \(id|Publish acknowledged" debug.log

# 異常 (警告とエラー)
grep -E "<wrn>|<err>" debug.log
```

| 見たいこと | 見るログ |
|:---|:---|
| SwitchBot を受信できているか | `parse_ad: SwitchBot advertising data` が、約 2 秒ごとに出ているか |
| 値が正しく解析できているか | 16 進ダンプを 4.2、4.3 の表で読んで、`<inf> thermo_gateway: SwitchBot ...` の値と比べる |
| AWS に送信できているか | `MQTT publish` のあと、約 1 秒以内に、同じ id の `Publish acknowledged` が出ているか |
| 接続が切れていないか | `<wrn>` の `Connection failed`、`MQTT connection lost`、`Send queue is full` が出ていないか |

- `Send queue is full, the value was dropped` が出ているのは、AWS に接続できていないときです。
- `Publish acknowledged` が、`MQTT publish` に対して、出ないときは、メッセージが届いていません。AWS 側のポリシー (`iot:Publish` の対象のトピック) を、確認してください ([AWS_SETUP.md](AWS_SETUP.md) の 5)。
