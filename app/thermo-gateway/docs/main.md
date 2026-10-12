# main.c の解析

対象: `app/thermo-gateway/src/main.c` (173 行)

ゲートウェイのエントリポイントです。やることは、初期化、コールバックの登録、スキャンの開始の 3 つだけで、そのあとは 10 秒ごとにログを出すだけのループに入ります。温度の受信、時刻の書き込み、AWS IoT Core への送信は、全て別のスレッドやワークキューが行います。

## 全体の流れ

```text
main()
  ├─ ble_init()                      Bluetooth を有効にして、接続のコールバックを登録
  ├─ cloud_init(true)                (CONFIG_THERMO_CLOUD) 設定を読んで、送信のスレッドを起動
  ├─ ble_set_temperature_callback()  ノードの温度 → on_temperature()
  ├─ ble_set_time_source()           (CONFIG_THERMO_CLOUD) ノードに書く時刻 → ntp_unix_time()
  ├─ ble_set_switchbot_callback()    SwitchBot の値 → on_switchbot()
  ├─ ble_scan()                      アクティブスキャンを開始
  └─ while (true) { ログ; k_sleep(10 s); }
```

- 初期化の順序には意味があります。`ble_init()` が先で、`cloud_init()` はその後です。コールバックは、`ble_scan()` を呼ぶ前に登録します (スキャンを始めると、すぐにコールバックが呼ばれるおそれがあるため)。
- `ble_init()`、`cloud_init()`、`ble_scan()` のどれかが失敗したら、`EXIT_FAILURE` で `main()` を抜けます。正常なときは、`main()` から戻りません。
- Zephyr では、`main()` から戻っても、システムは止まりません (ほかのスレッドは動き続けます)。失敗したときは、ログを出して `EXIT_FAILURE` を返し、スキャンなどの以降の処理は行いません。

## ビルド構成による違い

| マクロ | 有効なとき | 無効なとき |
|:---|:---|:---|
| `CONFIG_SIMULATOR` | 起動ログに `SIMULATOR MODE` と出す | `started on ESP32C3` と出す |
| `CONFIG_THERMO_CLOUD` (既定は y) | `cloud_init(true)`、`ntp_unix_time` の登録、`cloud_publish_*()` の呼び出しが入る | AWS IoT Core への送信、ノードへの時刻の書き込みが、全て外れる (native_sim 用) |

`CONFIG_THERMO_CLOUD` が無効なビルドでは、`cloud.h` と `ntp.h` を読み込みません。`on_temperature()` と `on_switchbot()` は、ログを出すだけになります。

## 関数

### main

| 項目 | 内容 |
|:---|:---|
| 戻り値 | `EXIT_FAILURE` (初期化かスキャン開始の失敗)。正常時は戻らない |
| ループの周期 | `STATUS_INTERVAL_S` = 10 秒 (`k_sleep(K_SECONDS(...))`) |
| ログ | 10 秒ごとに `Gateway scanning for nodes...` |

ループは、稼働状況を見せるためだけにあります。ゲートウェイがスキャンしていること (固まっていないこと) を、シリアルのログで確認できます。

### on_temperature (static)

ノードから温度の通知を受け取ったときに、Bluetooth のスレッドから呼ばれます。

1. `bt_addr_le_to_str()` でアドレスを文字列にします。
2. 温度を符号と絶対値に分けて出力します。`-0.5` のように整数部が 0 の負の値でも符号が出るように、`temp / 10` と `temp % 10` を使わず、符号 (`"-"` か `""`) を別に作ります。
3. 湿度が `THERMO_HUMIDITY_NONE` (0xFFFF) のときは、温度だけを出力します。
4. `CONFIG_THERMO_CLOUD` が有効なら、`cloud_publish_temperature()` で、送信のキューに入れます。失敗しても `LOG_WRN` を出すだけで、受信は続けます (次の通知で、また試すため)。

Bluetooth のスレッドで動くので、時間のかかる処理はありません。送信そのものは、`cloud.c` のスレッドが行います。

### on_switchbot (static)

SwitchBot 屋外用温湿度計のアドバタイズを受け取ったときに、呼ばれます。

1. `switchbot_accept()` に、アドレス、解析結果、現在の稼働時間 (`k_uptime_get_32()`) を渡します。
2. `false` が返ったら (サービスデータの記録だけ、機種が違う、間隔が短い、機器の数が上限)、何もしないで戻ります。
3. `true` なら、`sample` (温度、湿度、電池残量) を、ログに出して、`cloud_publish_switchbot()` でキューに入れます。

間引きの判断は、`switchbot.c` が全て行います。`main.c` は、間引かれずに残った値を扱うだけです。詳しい条件は [switchbot.md](switchbot.md) を参照してください。

## 気づいた点

- `on_temperature()` と `on_switchbot()` には、温度を「符号と絶対値」に分ける同じ処理が書かれています (さらに、ノード側の `main.c` と `oled.c` にもあります)。共通の関数にまとめることもできますが、今は、各ファイルが自己完結するように重複させています。
- ゲートウェイは、ノードからの温度を保存しません。AWS IoT Core につながっていない間に送信のキュー (16 件。`cloud.c` の `QUEUE_LEN`) が満杯になると、`Send queue is full, the value was dropped` と警告を出して、値を捨てます。ノードは、ゲートウェイにつながっていた間の値を「送信済み」として SD カードに残しますが、ゲートウェイがインターネットにつながっていなかった分は、AWS には届きません (`SD_LOG.md` に記載)。

## 関連

- [ble.md](ble.md): ノードのスキャンと接続、温度の購読、時刻の書き込み
- [switchbot.md](switchbot.md): SwitchBot のアドバタイズの解析と間引き
- [index.md](index.md): このディレクトリの文書の一覧
