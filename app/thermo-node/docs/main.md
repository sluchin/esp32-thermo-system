# main.c の解析

対象: `app/thermo-node/src/main.c` (633 行)

ノードのエントリポイントです。起動の初期化を済ませたあと、50 ms ごとに回る 1 本のループで、測定、通知、表示、ボタン、機器の再初期化、時刻の確認、ログの書き出しを全て行います。BLE のコールバックと、ブザー、クラウドの送信は、別のスレッドとワークキューで動きます。

## 起動の順序

```text
main()
  1. sensor_init()            失敗 → EXIT_FAILURE
  2. ble_init()               失敗 → EXIT_FAILURE
  3. ble_set_time_callback(node_time_set)      (CONFIG_THERMO_RTC) ゲートウェイが書いた時刻を RTC へ
  4. ble_advertise()          失敗 → EXIT_FAILURE
  5. init_optional(&devs)     RTC, WiFi, ブザー, OLED, ボタン (失敗しても続ける)
  6. direct_init()            (CONFIG_THERMO_DIRECT_MQTT) 失敗しても続ける。SD カードもここでマウントする
  7. sdlog_flush()            ここまでのログを SD カードに書く
  8. boot_sequence(&devs)     起動音と起動画面 (約 3 秒、戻らない)
  9. 時間の基準 (start_ms) を取り直して、ループへ
```

- センサ、BLE、アドバタイズは必須です。失敗したら、ログを出して `main()` が `EXIT_FAILURE` を返します。
- RTC、OLED、ボタン、ブザー、WiFi は任意の機器です。失敗しても、測定と BLE の通知は止めません。
- `sdlog_flush()` を起動画面の前に呼ぶのは、起動画面の約 3 秒の間、メインループが止まるので、その間にログが RAM のバッファ (既定 768 バイト) に溜まりすぎないようにするためです。SD カードのマウント (`direct_init()` の中の `backlog_init()`) が済んでいれば、ここで、起動のログが `boot.log` に書かれます。
- 起動画面の約 3 秒を、ループの時間に数えないよう、画面のあとで `start_ms = k_uptime_get()` を取り直し、周期の基準を作り直します。

## メインループ (50 ms ごと)

`TICK_MS` = 50 ms です。ボタンの押下を調べる間隔であり、ボタンのチャタリングを無視する時間にもなります。1 周で、次の処理を、上から順に行います。

| 順 | 処理 | 周期 | 条件 |
|:---|:---|:---|:---|
| 1 | 測定、通知、直接の送信、履歴 | 5 秒 (`SAMPLE_INTERVAL_MS`) | 常に |
| 2 | ボタンの検出とページ切り替え | 50 ms | `CONFIG_THERMO_DISPLAY` で、ボタンが使える |
| 3 | OLED の自動消灯 | 50 ms で確認 | `CONFIG_THERMO_DISPLAY` で、タイムアウトが 0 でない |
| 4 | 任意の機器の再初期化 | 60 秒 (`RETRY_INTERVAL_MS`) | 常に |
| 5 | RTC の時刻の確認と、WiFi による時刻の取得 | 60 秒 (`TIME_CHECK_INTERVAL_MS`) | `CONFIG_THERMO_WIFI_TIME` |
| 6 | ログの書き出し、RTC の読み出し、OLED の更新 | 1 秒 (`REDRAW_INTERVAL_MS`) | 常に |

最後に `k_msleep(TICK_MS)` で 50 ms 待ちます。

### 周期の求め方 (next_grid_ms)

```c
return start_ms + ((((now_ms - start_ms) / interval_ms) + 1) * interval_ms);
```

「今 + 間隔」で次の時刻を決めると、`k_msleep()` のティックの丸めや処理の遅れが、周期ごとに積もります。そこで、ループを始めた時刻 (`start_ms`) から、間隔ごとに区切った格子の、今より後の最初の時刻を使います。遅れても、次の格子に戻るので、ずれが積もりません。測定が遅れて、格子を 1 つ飛び越したときは、飛ばして次の格子に進みます (遅れを取り戻す測定はしません)。

### 1. 測定 (5 秒ごと)

```text
measure()
  ├ sensor_read()            失敗 → 戻り値を返す (通知しない。値を捨てて、5 秒後に再試行)
  ├ log_sample()             ログに温度と湿度を出す
  └ ble_notify_temperature() 通知の失敗はログだけ (戻り値には含めない)
成功したら
  ├ direct_sample()          (CONFIG_THERMO_DIRECT_MQTT) SD カードへの保存と、WiFi での直接の送信
  └ (CONFIG_THERMO_DISPLAY)
      view に最新の値を入れる
      history_add(温度)、history_add(湿度。湿度なしのセンサでは加えない)
      history_store_update()   区切りが終わった点の保存と、起動後の復元
```

- 初回は `next_sample_ms = 0` なので、ループに入ってすぐ測ります。
- 読み取りに失敗したときは、`view` の値を更新しないので、OLED には前回の値が出続けます。
- `ble_notify_temperature()` は、ゲートウェイにつながっていなくても、値を更新して成功を返します (`ble.md` を参照)。そのため、「つながっていない」ことは、`direct_sample()` が `ble_is_connected()` で調べます。

### 2. ボタンとページ

```text
ボタンが押された (button_ready && button_pressed())
  ├ OLED が点灯中、または OLED を使えない
  │    → page = (page + 1) % OLED_PAGE_COUNT (3)
  │      ボタンの音を鳴らす (CONFIG_THERMO_BUTTON_SOUND)
  └ OLED が消灯中
       → 点灯するだけ (ページは切り替えない。音も鳴らさない)
  どちらも: 消灯の期限を「今 + タイムアウト」に延ばして、次の 1 周でその場で表示し直す
```

OLED を使えないとき (`display_ready` が false) も、ページ番号と音は動きます。表示する先がないだけです。`set_display_power(true)` が失敗すると、`display_on` は false のままで、次の押下でもう一度、点灯を試みます。

### 3. 自動消灯

`CONFIG_THERMO_DISPLAY_TIMEOUT_SEC` (既定 10 秒、0 なら消さない) の間、ボタンが押されないと、`oled_set_power(false)` で消灯します。起動画面から点灯しているので、起動後の最初の期限は、`start_ms + タイムアウト` です。消灯に失敗したら、`display_on` は true のままで、期限を 1 秒後に延ばして、やり直します。

消灯中も、OLED の更新 (`oled_show()`) は止まります (`display_on` のときだけ呼ぶため)。

### 4. 任意の機器の再初期化

60 秒ごとに `init_optional()` を呼び、まだ使えない機器を初期化し直します。起動のときの一時的な失敗 (I2C の接触不良や、電源が安定する前など) から、再起動せずに復帰するためです。OLED が使えなかったのに、使えるようになったときは、初期化の中で点灯するので、`display_on = true` にして、消灯の期限を数え直します。

### 5. 時刻の確認 (WiFi)

ゲートウェイが BLE で時刻を書き込めば、RTC に時刻が入ります。書き込まれなかったときの予備として、WiFi と SNTP で時刻を取ります。

```text
条件: !time_done && now >= next_time_check_ms && rtc_ready && !DIRECT_BUSY()
  next_time_check_ms の初期値は start_ms + 60 秒。以後 60 秒ごと
  time_done = check_time(datetime.valid, wifi_ready, &wifi_tries)
```

- `DIRECT_BUSY()` は、`direct_active()` です。WiFi での直接の送信が動いている間は、WiFi を時刻の取得に使いません (WiFi は 1 つしかなく、時刻の取得が WiFi を切るため)。
- `check_time()` の結果は次のとおりです。

| 状況 | 動作 | 戻り値 |
|:---|:---|:---|
| RTC に時刻がある | 何もしない | true (確認を終える) |
| WiFi を使えない (初期化の失敗) | 警告を出す。試みに数えない | false |
| 取得に失敗 | `tries` を数える | `tries >= 5` |
| 取得できたが、RTC の設定に失敗 | `tries` を数える | `tries >= 5` |
| 取得して、RTC に設定した | | true |

- `wifi_time_fetch()` は、最大で約 25 秒 (WiFi の接続 20 秒 + SNTP 5 秒) 戻りません。その間、メインループは止まります。ボタンも、測定も、OLED の更新も、止まります。時刻がないという異常なときだけに、1 分ごとに、最大 5 回だけ起きる動作です。
- 5 回で確認を終えたあとも、ゲートウェイが BLE で時刻を書けば、RTC に設定されます (書き込みは、BLE のスレッドで処理されるため)。
- `datetime.valid` は、1 秒ごとの更新 (6) で読んだ値です。

### 6. 1 秒ごとの更新

```text
sdlog_flush()                溜めたログを SD カードに書く (カードがなければ何もしない)
node_time_get(&datetime)     RTC を読む (rtc_ready のとき。失敗しても valid が false になるだけ)
oled_show(&view)             点灯しているときだけ。失敗したらログを出して、次の 1 秒に再試行
```

`view.time` は、RTC を使えるとき (`rtc_ready`) だけ、`&datetime` を指します。それまでは NULL で、OLED には日付と時刻の行が出ません。

## 構造体 optional_devices

| メンバー | 意味 |
|:---|:---|
| `rtc_ready` | RTC を初期化できたか |
| `display_ready` | OLED を初期化できたか |
| `button_ready` | ボタンを初期化できたか |
| `buzzer_ready` | ブザーを初期化できたか |
| `wifi_ready` | WiFi (時刻の取得) を初期化できたか |

`init_optional()` は、`false` のメンバーだけを初期化して、成功したら `true` にします。一度 `true` になった機器は、もう呼びません。

## 関数

| 関数 | 役割 |
|:---|:---|
| `init_optional()` | まだ使えない任意の機器を初期化する。失敗は `LOG_ERR` に、`retry in 60 s` と出す |
| `set_display_power()` | `oled_set_power()` を呼び、失敗をログに出して `bool` で返す |
| `boot_sequence()` | ブザーでメロディを始め (すぐ戻る)、起動画面を `OLED_BOOT_FRAME_MS` (60 ms) ごとに `OLED_BOOT_FRAMES` (50) フレーム出す。約 3 秒戻らない。失敗したら、画面の途中でやめる |
| `check_time()` | RTC に時刻がなければ、WiFi で取って設定する (上の表) |
| `play_button_sound()` | ブザーが使えるときだけ、ボタンの音を鳴らす。失敗してもページの切り替えには影響しない |
| `measure()` | センサ、ログ、BLE の通知 |
| `log_sample()` | 温度と湿度をログに出す。負の値は符号を別に出す (-0.5 のような値を正しく出すため) |
| `next_grid_ms()` | 格子の次の時刻 |

## ビルド構成による違い

| マクロ | 外れるもの |
|:---|:---|
| `CONFIG_THERMO_RTC` | RTC の初期化、時刻のコールバック、1 秒ごとの RTC の読み出し |
| `CONFIG_THERMO_DISPLAY` | OLED、ボタン、履歴、自動消灯 |
| `CONFIG_THERMO_BOOT_SCREEN` | 起動画面 |
| `CONFIG_THERMO_BUZZER` | ブザーの初期化と起動音 |
| `CONFIG_THERMO_BUTTON_SOUND` | ボタンの音 |
| `CONFIG_THERMO_WIFI_TIME` | WiFi の初期化と、時刻の確認 |
| `CONFIG_THERMO_DIRECT_MQTT` | `direct_init()`、`direct_sample()`。`DIRECT_BUSY()` は常に false |
| `CONFIG_SIMULATOR` | 起動ログの文言だけ |

`sdlog.h` は、`CONFIG_THERMO_SDLOG` が無効のとき、何もしないインライン関数になるので、`main.c` には `#ifdef` がありません。

## 気づいた点

- `Kconfig` の `THERMO_WIFI_TIME` の説明は、「RTC に時刻がない状態が 5 分続いたら」と書いていますが、実装は、起動の 1 分後から 1 分ごとに確認して、WiFi での取得を最大 5 回試みます。BLE で時刻が書かれず、取得が毎回失敗すれば、約 5 分で終わるので、ほぼ同じ意味です。ただし、WiFi を使えないとき (初期化の失敗) は、試みに数えないので、5 回で終わるとは限りません。
- `main()` の大部分が、1 つのループに入っています。そのため、`wifi_time_fetch()` (最大 25 秒) のあいだは、ボタンも測定も止まります。BLE の接続の維持と、クラウドの送信は、別のスレッドなので止まりません。ただし、温度の通知は、メインループが送るので、その間は出ません。
- 測定の 5 秒の格子は、`start_ms` から数えます。履歴の区切り (`history_add()`) は、稼働時間 / 区切りの長さ (`now_ms / step_ms`) なので、`start_ms` ではなく、起動からの絶対的な稼働時間で数えます。起動画面の約 3 秒の分だけ、測定の格子と区切りの格子の位置が、ずれます。

## 関連

- [ble.md](ble.md): アドバタイズ、通知、時刻の受信
- [direct.md](direct.md): `direct_init()` と `direct_sample()`
- [oled.md](oled.md)、[history.md](history.md)、[history_store.md](history_store.md): 表示と履歴
- [node_time.md](node_time.md)、[wifi_time.md](wifi_time.md): 時刻
- [index.md](index.md): このディレクトリの文書の一覧
