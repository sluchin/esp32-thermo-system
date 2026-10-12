# history_store.c の解析

対象: `app/thermo-node/src/history_store.c` (158 行) と `history_store.h` (54 行)

OLED のグラフの履歴は、RAM にしかないので、再起動すると消えます。起動のあと、RTC に時刻が入ったときに、SD カードの測定値の記録 (`backlog.c`。送信済みも含む) から、直近の点を作り直して、履歴に戻します。`CONFIG_THERMO_HISTORY_STORE` が有効なときだけ、ビルドされます (有効にするには、`THERMO_BACKLOG`、`THERMO_DISPLAY`、`THERMO_RTC` が要ります)。無効のときは、ヘッダの空のインライン関数が呼ばれます。

## 公開する関数

| 関数 | 役割 |
|:---|:---|
| `history_store_update()` | 測定値を履歴に加えるたびに呼ぶ。RTC に時刻が入ったら、1 回だけ復元する |
| `history_store_reset()` | 単体テストが、再起動を模擬するための関数。復元済みの印を消す |

## 区切りの番号の対応

履歴の区切りの番号は、**稼働時間** / 区切りの長さです。SD カードの記録の区切りの番号は、**測った UNIX 時刻** / 区切りの長さです。2 つを合わせるために、差 `base` を使います。

```text
base         = (今の UNIX 時刻[s] - 今の稼働時間[s]) / 区切りの長さ[s]      ← 復元のとき 1 回だけ求める
履歴の番号   = 記録の時刻の区切り - base
```

起動からの時間 (稼働時間) と、RTC の時刻の差は、動いている間は、一定です。この差を区切りの長さで割った値を引くことで、記録が、グラフの正しい位置に戻ります。

`base` は、整数の割り算で求めるので、稼働時間を区切りに分ける境界と、UNIX 時刻を区切りに分ける境界は、最大で区切り 1 つぶん (既定 170 秒) までずれます。復元した点と、そのあとの、実際に測る点の境目で、位置が少しずれることがあります。

## history_store_update

```text
active なら返る                                     ← 復元は、1 回だけ
node_time_unix() が失敗 (RTC に時刻がない) なら返る    ← 次の測定で、また試す
active = true
temp->count > 0 なら返る                             ← すでに点がある。戻さない
base = ...、limit = now_ms / step_ms                 ← limit: 今の区切りの番号
backlog_foreach_since((limit + base - 128) * 区切りの長さ[s], visit, &state)
flush(&state)                                        ← 最後の区切りを戻す
ログ: Restored N graph points from the SD card
```

- 「RTC に時刻が入るまで何もしない」ので、RTC が使えない、または、時刻がまだ入っていない間は、測定のたびに `node_time_unix()` を呼ぶだけです。時刻が入った時点で、復元します。
- `temp->count > 0` で返るのは、時刻が入るのが遅く、すでに区切りが完了して、履歴に点があるときです。このときは、戻しません (履歴の途中に、古い点を挟めないため)。そのため、起動のときに、RTC に時刻がなく、ゲートウェイから時刻が届くのが、区切り 1 つ (既定 170 秒) より遅いと、復元されません。
- 復元する範囲は、今の区切りより前の、直近 128 区切り (`HISTORY_SIZE`) です。`since` の時刻を `(limit + base - 128) * step_s` として、そこ以降の記録を読みます。
- `backlog_foreach_since()` が失敗しても、読めたところまでは戻し、警告を出して続けます。

## visit と flush: 区切りごとの平均

`backlog_foreach_since()` は、記録を、古い順に `visit()` へ渡します。

```text
visit(記録)
    slot = 記録の時刻 / 区切りの長さ
    slot が前の記録と違えば: flush() して、slot を更新
    温度の合計と数に加える
    湿度が「なし」でなければ、湿度の合計と数に加える

flush()
    bucket = slot - base
    温度の数 > 0 かつ bucket < limit なら
        history_restore(温度の履歴, bucket, 温度の平均)
        湿度の数 > 0 なら history_restore(湿度の履歴, bucket, 湿度の平均)
        points++
    合計と数を 0 に戻す
```

- 平均は、整数の割り算です。
- 湿度のないセンサ (ADC) の記録は、湿度の数が 0 なので、湿度の履歴には、点が入りません。
- `bucket < limit` で、今の区切り (これから測る区切り) の記録は、戻しません。今の区切りは、`history_add()` が、これから測る値で作ります。
- 記録のない区切りは、`flush()` が呼ばれないので、点が入りません。`history_restore()` が、前の点との間の番号の空きを、`HISTORY_NONE` で埋めます。電源が入っていなかった間は、グラフの切れ目になります。

## struct restore_state

`backlog_foreach_since()` の `ctx` として、`visit()` に渡す状態です。

| メンバー | 内容 |
|:---|:---|
| `temp`、`humidity` | 戻す先の履歴 |
| `step_s` | 区切りの長さ [s] (`step_ms / 1000`) |
| `base` | 時刻の区切りの番号から、履歴の区切りの番号にするために引く値 |
| `limit` | 戻す区切りの上限 (これより前だけ戻す) |
| `slot` | 今の区切りの番号 (時刻の区切り)。記録がまだなければ -1 |
| `temp_sum`、`humidity_sum` | 今の区切りの合計 |
| `temp_n`、`humidity_n` | 今の区切りの数 |
| `points` | 戻した点の数 |

## 気づいた点

- `step_ms` は、秒単位でなければなりません (`step_ms / 1000` が割り切れること)。`CONFIG_THERMO_GRAPH_STEP_SEC` から作るので、満たされます。
- 呼び出しは、毎回の測定のあとです。`active` になったあとは、`node_time_unix()` も呼ばずに、すぐ返ります。
- `main.c` では、`direct_sample()` (SD カードへの保存) のあとに呼びます。そのため、今の測定値は、すでに SD カードにありますが、今の区切りなので、戻されません。

## 関連

- [history.md](history.md): `history_restore()` と、リングバッファ
- [backlog.md](backlog.md): `backlog_foreach_since()`
- [node_time.md](node_time.md): `node_time_unix()`
- [main.md](main.md): `history_store_update()` の呼び出し
- [index.md](index.md): このディレクトリの文書の一覧
