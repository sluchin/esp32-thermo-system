# history.c の解析

対象: `app/thermo-node/src/history.c` (152 行) と `history.h` (46 行)

OLED のグラフに描く、温度と湿度の履歴です。測定値 (5 秒ごと) を、一定の時間 (区切り) ごとの平均にして、新しい方から 128 点だけ覚えます。Zephyr の関数を呼ばない、純粋な計算なので、単体テストはモックを使いません。

## struct history

| メンバー | 内容 |
|:---|:---|
| `points[128]` | 区切りごとの平均 (リングバッファ)。`HISTORY_SIZE` = 128 は OLED の幅 (128 ドット) と同じで、1 点が 1 ドット |
| `head` | 次に書く位置 (`points` の添字) |
| `count` | 覚えている点の数 (128 まで) |
| `end` | 最後に覚えた点の区切りの番号 + 1 (`count` が 1 以上のときだけ使う) |
| `bucket` | 今の区切りの番号 (稼働時間 / 区切りの長さ) |
| `sum` | 今の区切りの測定値の合計 |
| `n` | 今の区切りの測定値の数 |

0 で初期化して使います。温度用と湿度用に、別々の履歴を持ちます (`main.c`)。

`HISTORY_NONE` (`INT16_MIN`) は、測定値がなかった区切りの印です。温度や湿度の 10 倍の値では、取らない値です。

区切りの長さは、`CONFIG_THERMO_GRAPH_STEP_SEC` (既定 170 秒) です。128 点で 21760 秒、約 6 時間ぶんになります。

## history_add

```text
bucket = now_ms / step_ms
n > 0 かつ bucket が前回と違う → 前の区切りの平均を push_at() で覚える。sum と n を 0 に戻す
h->bucket = bucket、sum += value、n++
```

- 平均は、整数の割り算 (`sum / n`、0 の方向への切り捨て) です。
- 区切りが変わったときに、前の区切りの平均を確定します。今の区切りは、まだ `points[]` に入っていません (`sum` と `n` に溜まっています)。
- 測定値のない区切りがあっても (電源が入っていなかった、読み取りに失敗し続けた、など)、`history_add()` は、何も補いません。穴は、次に確定する点を `push_at()` が覚えるときに、`HISTORY_NONE` で埋めます。

## push_at: 穴を埋めて 1 点を加える

```text
count > 0 かつ end < bucket
    → gap = min(bucket - end, 128) 個の HISTORY_NONE を push()
push(value)
end = bucket + 1
```

「最後に覚えた点の区切り」と「今回の区切り」の間に、番号が空いているぶんだけ、`HISTORY_NONE` を入れます。128 個を超える穴は、全て押し出されるので、128 個で打ち切ります。

`push()` は、`head` の位置に書いて、`head` を進め (128 で一周)、`count` を 128 まで増やします。満杯のときは、最も古い点を上書きします。

## history_get: グラフ用に点を取り出す

古い順に、`out[]` へ取り出して、数を返します。

```text
has_current = (n > 0)
gap = has_current && count > 0 && end < bucket ? min(bucket - end, 128) : 0
total = count + gap + (has_current ? 1 : 0)
skip  = total > max ? total - max : 0         ← 古い方を捨てる
oldest = (head + 128 - count) % 128
i を skip から total まで:
    i < count             → points[(oldest + i) % 128]
    i < count + gap       → HISTORY_NONE
    それ以外              → 今の区切りの平均 (sum / n)
```

- 確定した点に加えて、**今の区切りの途中の平均** も、最後の点として含めます。そのため、グラフの右端は、常に最新の測定値を反映します。
- 確定した最後の点と、今の区切りの間に、測定値のない区切りがあれば、その数だけ `HISTORY_NONE` を挟みます。
- `max` より多いときは、新しい方から `max` 個だけを返します。OLED は、`HISTORY_SIZE` を渡します。

## history_restore

保存してあった点 (SD カードの記録から作った平均) を、起動のあとに戻すために使います。`push_at()` を呼ぶだけです。前の点との間に、番号が空いていれば、`HISTORY_NONE` が入ります。

- 戻す点の区切りの番号は、前に戻した点より大きく、今の区切りより前であること。今の区切りの `sum` と `n` は、変えません。
- 呼び出しは、`history_store.c` だけです。

## history_last

最後に確定した点 (`points[(head + 127) % 128]`) を返します。`count` が 0 のときは、使えません。現在は、ソースの中に呼び出し元がなく、単体テストだけが使います。

## 気づいた点

- 区切りの番号は、稼働時間 (`k_uptime_get()`) から求めるので、再起動すると 0 から数え直します。再起動をまたぐ履歴は、SD カードの時刻つきの記録から、`history_store.c` が戻します。
- `end` は、`count > 0` のときだけ意味があります。`count == 0` で `history_get()` を呼ぶと、`gap` は 0 で、今の区切りの 1 点だけ (または 0 点) を返します。
- 平均は、10 倍した値の整数の割り算です。0.1 の単位より細かい端数は、切り捨てられます (負の値は、0 に近い方へ丸められます)。

## 関連

- [main.md](main.md): `history_add()` の呼び出し (測定のたび)
- [history_store.md](history_store.md): 起動のときの復元
- [oled.md](oled.md): `history_get()` を使ったグラフの描画
- [index.md](index.md): このディレクトリの文書の一覧
