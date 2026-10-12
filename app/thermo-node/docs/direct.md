# direct.c の解析

対象: `app/thermo-node/src/direct.c` (363 行) と `direct.h` (50 行)

ノードが、ゲートウェイを介さずに、WiFi と MQTT で、AWS IoT Core へ測定値を直接送るための制御です。同時に、全ての測定値を SD カードに保存します (ストア・アンド・フォワード)。`CONFIG_THERMO_DIRECT_MQTT` が有効なときだけ、ビルドされます。

実際の保存は `backlog.c`、WiFi と MQTT の接続と送信は `app/common/cloud.c` が行います。`direct.c` は、**いつ WiFi を使うか、何を送るか、送れたらどう記録するか** を決める、状態機械です。

## 公開する関数 (direct.h)

| 関数 | 呼ぶ場所 | 役割 |
|:---|:---|:---|
| `direct_init()` | `main()` の起動時 | 準備 (アドレス、クラウドのスレッド、SD カード) |
| `direct_sample()` | `main()` の測定のたび (5 秒) | 測定値の保存と、WiFi の開始と停止の判断、未送信の記録の送信 |
| `direct_active()` | `main()` の時刻の確認 | 直接の送信を動かしているか (動かしている間は、WiFi を時刻の取得に使わない) |
| `direct_sd_check()` | シェルの `thermo sd` | SD カードが使えるかを確認する (使えなければ、マウントし直す) |
| `direct_init_status()` | シェルの `thermo sd` | `direct_init()` の結果を返す |

## 定数

| 定数 | 値 | 意味 |
|:---|:---|:---|
| `DIRECT_BLE_FAIL_LIMIT` | 5 | ゲートウェイにつながっていない測定が、この回数続いたら、WiFi を始める (5 秒 × 5 = 約 25 秒) |
| `DIRECT_ATTEMPT_LIMIT` | 5 | WiFi か MQTT の接続が、この回数続けて失敗したら、あきらめる |
| `FLUSH_QUEUE_MAX` | 4 | 送信のキューに、SD カードの記録を入れておく数の上限 |

## 状態変数

| 変数 | 意味 |
|:---|:---|
| `ble_failures` | ゲートウェイにつながっていない測定の連続回数 (5 で頭打ち) |
| `was_connected` | 前の測定のとき、つながっていたか |
| `active` | 直接の送信 (WiFi と MQTT) を動かしているか |
| `gave_up` | あきらめたか (ゲートウェイにつながるまで、やり直さない) |
| `warned_incomplete`、`warned_no_time` | 警告を、測定のたびに出さないための印 |
| `node_addr`、`addr_ready` | このノードの BLE のアドレス (トピックと payload に使う) と、取得できたか |
| `init_status` | `direct_init()` の結果。まだ呼んでいなければ `-EAGAIN` |
| `backlog_ready` | SD カードを使えるか |
| `queued` | SD カードの記録のうち、送信のキューに入れて、まだ「送り終えた」ことにしていない数 |
| `sent_mark` | `queued` が 0 のときの `cloud_sent_count()` の値。送れた数を求める基準 |

## direct_init

1. 全ての状態を初期値に戻します (`backlog_ready` 以外。やり直しても、前の状態を引き継がないため)。
2. `ble_get_address()` でアドレスを取ります。失敗したら、`init_status` に結果を入れて、返ります。
3. `cloud_init(false)` で、設定を読み、送信のスレッドを作ります。`false` は、`cloud_set_enabled(true)` が呼ばれるまで、接続しないで待つ、という意味です (ゲートウェイは `true`)。
4. `cloud_set_attempt_limit(5)` で、接続の失敗が続いたときに、あきらめる回数を決めます。
5. `backlog_init()` で SD カードをマウントします。失敗しても、`direct_init()` は成功します (保存しないだけ)。
6. `addr_ready = true`、`init_status = EXIT_SUCCESS`。

**重要**: 2 か 3 で失敗すると、`addr_ready` が false のままなので、`direct_sample()` は、毎回すぐに返ります。SD カードがマウントできていなくても (5 の前に返るので、そもそもマウントしません)、測定値は、保存されません。このとき、シェルの `thermo sd` が `Saving is off` と警告します。

`cloud_init()` と `wifi_time_init()` の両方が `cfg_init()` を呼びます。2 回目の登録は `-EEXIST` になるので、`app/common/cfg.c` が、これを許容します (コミット `fix(app): Accept the settings handler registered twice`)。これを許容しなかったころは、`direct_init()` が `-17` (`-EEXIST`) で失敗し、保存が止まっていました。

## direct_sample

測定のたびに呼ばれます。処理は 6 つの段階です。

```text
(1) addr_ready でなければ、何もしない

(2) connected = ble_is_connected()
      つながっている: 前回はつながっていなかったなら gave_up = false (あきらめを解く)
                     ble_failures = 0、warned_incomplete = false
      つながっていない: ble_failures++ (5 まで)
      was_connected = connected

(3) saved = save_value(温度, 湿度, sent = connected)
      ゲートウェイにつながっていれば、ゲートウェイが AWS に送るので、送信済み (1) で保存
      つながっていなければ、未送信 (0) で保存

(4) wifi_wanted =
        (!connected && ble_failures >= 5)       ... つながらない測定が続いている
     || (connected && backlog_pending() > 0)    ... つながったが、溜まった記録がある

(5) WiFi の開始と停止
      !wifi_wanted: active なら stop_sending()
      wifi_wanted && !active: !gave_up なら start_sending()
      wifi_wanted && active && cloud_has_given_up(): active = false、gave_up = true、queued = 0

(6) 後始末
      保存できず(!saved)、つながっていない(!connected) → publish_live() (そのまま送る)
      active なら flush_backlog()
```

### 状態の遷移

```text
              ┌──────────── ゲートウェイにつながった (記録がなければ、WiFi を止める) ─────────┐
              ▼                                                                              │
        [BLE で送る]  ─ つながらない測定が 5 回続く ─▶ [WiFi で直接送る (active)]  ◀───────────┤
         (保存: 送信済み)                               (保存: 未送信 → 送れたら送信済み)       │
              ▲                                                  │                           │
              │                                  接続が 5 回続けて失敗                        │
              │                                                  ▼                           │
              └──── ゲートウェイにつながったら、あきらめを解く ── [あきらめた (gave_up)] ─────────┘
                                                            (保存は続ける。WiFi は使わない)
```

### 判断の例

| 状況 | connected | pending | 結果 |
|:---|:---|:---|:---|
| ふつうに動いている | true | 0 | 送信済みで保存する。WiFi は使わない |
| ゲートウェイの電源を切った (最初の 4 回) | false | 増える | 未送信で保存する。まだ WiFi は始めない |
| 5 回目 | false | 増える | `start_sending()`。WiFi と MQTT が動き始める |
| 送信中 | false | 減る | 古い順に送って、送れた分を送信済みにする |
| ゲートウェイに戻った | true | 残りがある | WiFi を続けて、残りを送る。送り終えたら止める |
| WiFi が 5 回続けて失敗 | false | 増える | `gave_up`。保存は続ける。ゲートウェイにつながるまで、WiFi は使わない |

### start_sending

設定が揃っていなければ (`cfg_is_complete()` が false)、始めません。警告は 1 回だけ出します (`warned_incomplete`)。揃っていれば、`cloud_set_enabled(true)` で、接続を始めます。このとき、`cloud.c` は、送信のキューの古い値を捨てて、失敗の回数を 0 に戻します。

### stop_sending

`cloud_set_enabled(false)` で、MQTT を閉じて、WiFi を切ります。

### あきらめたとき

`cloud.c` は、接続の失敗が `DIRECT_ATTEMPT_LIMIT` 回に達すると、自分で `enabled = false` にして、`cloud_has_given_up()` が true を返します。`direct.c` は、それを見て、`active = false`、`gave_up = true`、`queued = 0` にします。あきらめた状態は、ゲートウェイにつながった瞬間 (`was_connected` が false から true) に解きます。

## save_value

```text
backlog_ready でなければ → false
node_time_unix() で RTC の UNIX 時刻を取る
    失敗 (時刻なし) → 警告を 1 回だけ出して false (測った時刻が要るので、保存しない)
    成功 → warned_no_time = false
backlog_append(&rec, sent) → 失敗なら LOG_ERR して false
true
```

測定値に、測った時刻 (UTC の UNIX 時刻) を付けるのは、あとから送るときに、その時刻をそのまま payload に入れるためです。RTC に時刻がない間は、保存できません (保存できない測定値は、`publish_live()` が、条件を満たすときだけ、そのまま送ります)。

## publish_live

保存できなかった測定値を、1 件、そのまま送ります。次の 3 つを満たすときだけです。

1. `active` (直接の送信を動かしている)
2. `cloud_is_connected()` (MQTT につながっている)
3. `backlog_pending() == 0` (先に送るべき記録がない。順序を保つため)

どれかを満たさなければ、捨てます (SD カードにも保存されていません)。

## flush_backlog: 溜めた記録を送る

`active` のとき、`direct_sample()` の最後で呼ばれます。MQTT につながっていなければ、何もしません。

```text
(a) 送れた分を「送り終えた」ことにする
      done = cloud_sent_count() - sent_mark
      0 < done <= queued なら
          backlog_commit(done)      ← SD カードの記録のフラグを書き換える
          queued -= done、sent_mark += done
          失敗したら、ここで返る (次の周期でやり直す)

(b) 先の記録を、送信のキューに入れる
      while (queued < backlog_pending() && cloud_pending() < FLUSH_QUEUE_MAX)
          backlog_read(&rec, queued)         ← 未送信の queued 番目 (0 が最も古い)
          queued == 0 なら sent_mark = cloud_sent_count()
          cloud_publish_temperature_at(addr, 温度, 湿度, 記録の時刻)
          成功したら queued++
```

- `queued` は、キューに入れたが、まだ SD カードで送信済みにしていない記録の数です。**送信済みの印は、送れたことを確認してから付けます。** キューに入れただけでは、付けません。電源が切れたり、接続が切れたりして、送れなかった記録は、未送信のまま残ります。
- 送れた数は、`cloud.c` が数える `sent_count` (MQTT に publish できた数) の増え方から求めます。キューには、4 件までしか入れないので、`queued` は、4 以下です。
- `backlog_read(&rec, queued)` の `index` は、**まだ送信済みにしていない** 記録の中での順番です。キューに入れた分は、まだ未送信のままなので、`queued` 番目を読めば、その次の記録になります。
- `backlog_commit(done)` は、古い方から `done` 件を送信済みにします。キューは先入れ先出しなので、送れた分は、古い順に並びます。

## direct_sd_check と direct_init_status

シェルの `thermo sd` のための関数です。

- `direct_sd_check()`: `backlog_ready` が false なら、`backlog_init()` でマウントし直します。成功したら `backlog_ready = true`。すでに使えているときは、マウントし直しません (測定値を保存している途中に外さないため)。未送信の数を返します。
- `direct_init_status()`: `init_status` を返します。

SD カードを挿し直したあと、再起動せずに、使えるようにできます。ただし、`direct_init()` が失敗していて `addr_ready` が false のときは、マウントできても、保存は始まりません。`thermo sd` は、その場合に警告します。

## 気づいた点

- 状態を持つ変数が多く、`direct_sample()` に分岐が集まっています。状態は全て `static` 変数で、`direct_init()` が (`backlog_ready` 以外を) 初期値に戻します。
- `backlog_ready` は、`direct_init()` で初期化 (リセット) しません。`backlog_init()` の結果で上書きするだけです。
- `wifi_wanted` が true でも、`gave_up` の間は、WiFi を始めません。その間も、測定値は保存し続けます。ゲートウェイにつながったときに、残りを送ります。
- 送る速さには、上限があります。`flush_backlog()` は、測定の周期 (5 秒) ごとに 1 回だけ動き、キューに入れるのは、キューにある数が 4 件になるまでです。そのため、溜まった記録を送る速さは、最大でも 5 秒に 4 件 (毎秒 0.8 件) 程度です。一方、保存は、5 秒に 1 件ずつ増え続けます。ゲートウェイが長く不在だった後の回復には、時間がかかります。たとえば 1 時間 (720 件) 溜めると、送り終えるまでに、約 15 分かかります (新しい測定値が、増え続ける分は、別です)。

## 関連

- [backlog.md](backlog.md): SD カードの記録の形式と、`backlog_*()` の動き
- [ble.md](ble.md): `ble_is_connected()`、`ble_get_address()`
- [node_time.md](node_time.md): `node_time_unix()`
- [wifi_shell.md](wifi_shell.md): `thermo sd`
- [main.md](main.md): `direct_sample()` の呼び出し
- [../../../SD_LOG.md](../../../SD_LOG.md): SD カードの記録の読み方
- [index.md](index.md): このディレクトリの文書の一覧
