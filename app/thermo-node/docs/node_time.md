# node_time.c の解析

対象: `app/thermo-node/src/node_time.c` (291 行) と `node_time.h` (41 行)

ノードの時刻です。拡張ボードの RTC (PCF8563) を、Zephyr の RTC API (`rtc_get_time()`、`rtc_set_time()`) で読み書きします。このファイルの仕事は、次の 3 つです。

1. RTC が持つ時刻 (UTC) と、UNIX 時刻 (1970-01-01 からの秒数) を、相互に換算する。
2. 読み出すとき、ローカルタイム (UTC + `CONFIG_THERMO_UTC_OFFSET_MIN`、既定 +540 分 = 日本標準時) の年月日と時分秒に直す。
3. 設定できる範囲 (2000-01-01 から 2099-12-31) を守る。

RTC は **UTC** を保持します。ローカルタイムに直すのは、表示のときだけです。SD カードに保存する測定値の時刻も、UTC の UNIX 時刻です。`CONFIG_THERMO_RTC=n` のときは、このファイルはビルドされません (`struct node_datetime` は、ヘッダにあるので、使えます)。

## 公開する関数

| 関数 | 役割 | 呼び出し元 |
|:---|:---|:---|
| `node_time_init()` | RTC を使えるかを確認する | `main.c` の `init_optional()` |
| `node_time_get()` | ローカルタイムを読む (`struct node_datetime`) | `main.c` (1 秒ごと。OLED の表示) |
| `node_time_unix()` | UTC の UNIX 時刻を読む | `direct.c`、`history_store.c` |
| `node_time_set()` | UNIX 時刻を RTC に設定する | `ble.c` (ゲートウェイが書いた時刻)、`main.c` と `wifi_shell.c` (WiFi で取った時刻) |

## struct node_datetime

| メンバー | 内容 |
|:---|:---|
| `valid` | 時刻が設定済みで、正しく読めたか。false のときは、下の値を使わない |
| `year` | 年 (2000 から 2100) |
| `month`、`day` | 月 (1 から 12)、日 (1 から 31) |
| `hour`、`minute`、`second` | 時、分、秒 |

ローカルタイムに直すと、UTC の 2099-12-31 15:00 以降は、ローカルタイムで 2100 年になります。このため、年の範囲の上限は、2100 です。

## node_time_init

```text
device_is_ready(rtc_dev) でなければ → -ENODEV (ドライバの初期化に失敗している)
read_utc()
    -ENODATA (時刻が未設定) → 警告を出して、成功にする
    それ以外の失敗 → そのまま返す (I2C の通信の失敗など)
成功 → "RTC initialized"
```

時刻が未設定でも成功とするのは、RTC そのものは、つながっているからです。時刻がないことは、`node_time_get()` の `valid` や `node_time_unix()` の `-ENODATA` で分かります。

## node_time_get

```text
local->valid = false
read_utc() → 失敗ならそのまま返す
local_s = unix_s + OFFSET_MIN * 60
secs    = local_s % 86400                  ← その日の 0 時からの秒数
civil_from_days(local_s / 86400, ...)      ← 年月日
時 = secs / 3600、分 = (secs % 3600) / 60、秒 = secs % 60
valid = true
```

呼び出し側 (`main.c`) は、失敗の戻り値を使いません。`valid` が false なら、OLED に `----/--/--` と `--:--:--` を出します。

## node_time_set

```text
unix_s が 946684800 (2000-01-01) 未満、または 4102444799 (2099-12-31 23:59:59) より大きい → -ERANGE
days = unix_s / 86400、secs = unix_s % 86400
civil_from_days() で年月日
rtc_time を作る:
    tm_year = 年 - 1900、tm_mon = 月 - 1 (0 から 11)
    tm_wday = (days + 4) % 7                ← 1970-01-01 は木曜日 (日曜日が 0 で、木曜日は 4)
    tm_yday = -1、tm_isdst = -1             ← 使わない
rtc_set_time()
```

範囲を 2000 年から 2099 年にするのは、PCF8563 の年のレジスタが 2 桁 (00 から 99) だからです。範囲の外の時刻は、ゲートウェイや WiFi が、まちがった値を送ってきたときの防御にもなります。ノードの BLE は、`-ERANGE` を、ATT のエラー `VALUE_NOT_ALLOWED` としてゲートウェイに返します。

## 日付の換算 (days_from_civil と civil_from_days)

標準ライブラリの `gmtime()` や `mktime()` を使わず、H. Hinnant の civil 日付のアルゴリズムで、グレゴリオ暦の日付と、1970-01-01 からの日数を、相互に換算します。うるう年の規則 (4 で割り切れる、ただし 100 で割り切れる年は除く、ただし 400 で割り切れる年は含む) が、400 年を 1 周期とする計算の中に、組み込まれています。

考え方は、次のとおりです。

1. 1 月と 2 月を、前の年の 13 月と 14 月と見て、年を **3 月始まり** にする。うるう日 (2 月 29 日) が、年の最後に来る。
2. 400 年 (146097 日) を 1 周期 (`era`) として、周期の中の年 (`yoe`) と、年の中の日 (`doy`) を求める。
3. 3 月始まりの月は、`(153 * mp + 2) / 5` で、月の初日の年内の日数に変換できる (大の月と小の月の並びを、近似する係数)。
4. 0000-03-01 を基準にした日数から、`DAYS_TO_EPOCH` (719468) を引いて、1970-01-01 基準にする。

例: 2000-01-01 は `y = 1999`、`era = 4`、`yoe = 399`、`mp = 10`、`doy = 306` で、`4 * 146097 + (399 * 365 + 99 - 3 + 306) - 719468 = 10957` 日。`10957 * 86400 = 946684800` で、`UNIX_TIME_MIN` と一致します。

式の中の小さな数 (2、3、5、9、12 など) は、アルゴリズム固有の定数なので、名前を付けていません (`days_from_civil()` の Doxygen コメントに、その旨を書いています)。

`days_from_civil()` は、1970 年以降の日付だけを扱います (Doxygen コメントに書いてあります)。

## read_utc

`rtc_get_time()` で `struct rtc_time` を読み、`days_from_civil() * 86400 + 時 * 3600 + 分 * 60 + 秒` を求めます。`-ENODATA` は、時刻が未設定のことなので、ログを出さずに返します。ほかの失敗は、`LOG_ERR` を出します。

## 気づいた点

- 時刻の設定 (`rtc_set_time`) は、書き込みの瞬間の遅れを補いません。ゲートウェイが SNTP で得た時刻を、BLE で書き込むまでの遅れ (数十ミリ秒から数秒) だけ、ずれます。
- ゲートウェイは、`TIME_RESYNC_S` (1 時間) ごとに、時刻を書き直します。RTC の時刻が、SNTP の時刻より進んでいると、書き直したときに、時刻が戻ります。このとき、SD カードの記録の時刻は、単調に増えなくなります。`backlog_foreach_since()` の二分探索は、時刻が増える順に並んでいることに頼っているので、戻った幅が大きいと、グラフの復元の範囲がずれます。
- `node_time_unix()` は、RTC を I2C で読みます。`direct_sample()` が、5 秒ごとに呼びます。

## 関連

- [rtc_pcf8563.md](rtc_pcf8563.md): RTC のドライバ (BCD とレジスタの扱い)
- [ble.md](ble.md): ゲートウェイからの時刻の書き込み
- [wifi_time.md](wifi_time.md): WiFi と SNTP による時刻の取得
- [main.md](main.md): RTC の初期化の再試行と、1 秒ごとの読み出し
- [index.md](index.md): このディレクトリの文書の一覧
