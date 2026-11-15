# RTC の時刻と VL ビット

ノードの時刻は、拡張ボードの RTC (PCF8563) が持ちます。このドキュメントは、RTC の VL ビットの意味と、VL が立ったときにファームウェアが時刻をどう扱うかを書きます。

## VL ビットとは

VL (Voltage Low) は、秒のレジスタ (0x02) の bit 7 です。1 のとき、チップは「時刻の完全性を保証しない」と伝えます。

- 立つとき: 電源の電圧 (VDD) が、チップのしきい値を下回ったとき。電源投入のリセットでも立ちます。
- 下りるとき: ソフトウェアが秒のレジスタを書いたときだけです。時間が経っても、電圧が戻っても、自動では下りません。
- 持つ意味: 「電圧が下がった瞬間があった」という記録です。時計が止まったこと、または時刻が壊れたことの証拠ではありません。電圧の落ち込みが短ければ、発振は続き、時刻のレジスタも保たれます。

VL が立つ原因は、次のとおりです。

| 原因 | 時刻のレジスタ |
| :--- | :--- |
| 電池が切れた、または未接続の状態で、電源を切った | 失われる (2000-01-01 付近から数え直す、または不定) |
| 電源を切るときの、一瞬の電圧の落ち込み (電池への切り替わりなど) | 保たれる |
| 電源の投入 (最初) | 不定 |

VL だけでは、この 3 つを見分けられません。見分けるには、時刻のレジスタの値が、妥当かを見ます。

## 観測した例

USB を抜き挿しして、電池が新しいのに、OLED が `----/--/--` のままになったことがあります。ログは、次のとおりです。

```text
The time of the RTC is not reliable (VL bit is set). Registers: control1 0x08 control2 0x80,
time (sec min hour day wday month year) 82 c0 94 95 80 11 26
No time in the RTC, getting it over WiFi (try 1 of 5)
Got the time over WiFi (SNTP) and set it to the RTC (UNIX time 1794753618)
```

- 秒 (0x82) は、VL を除くと 02 です。ほかのレジスタも、使わないビットを除くと、2026-11-15 14:40:02 (UTC) です。
- WiFi (SNTP) で取った時刻は、14:40:18 でした。RTC を読んだのは、その約 15.5 秒前なので、RTC の時刻は正しく進んでいました。
- 制御レジスタ 1 は、電源投入直後の値 (0x08) です。

つまり、時計は電池で動き続けていて、VL だけが USB を抜く瞬間に立ちました。以前の実装は、VL が立つと時刻を捨てていたので、時刻が出ませんでした。

## ファームウェアの扱い

VL が立っていても、時刻のレジスタが範囲に入り、2026 年以降なら、時刻を使います。使い道は、SD カードの最後の記録の時刻との比較で決めます。

電池が切れた RTC は、2000-01-01 付近から数え直すので、時刻が最後の記録より前に戻ります。時計が動き続けていれば、時刻は最後の記録より後です。この違いで、VL だけでは見分けられない 2 つの場合を見分けます。

| 状態 | OLED の表示 | SD カードへの保存と履歴の復元 | WiFi での時刻の取得 |
| :--- | :--- | :--- | :--- |
| VL が 0 | 出す | する | しない (確認を終える) |
| VL が 1 で、時刻が最後の記録以上 | 出す | する | しない |
| VL が 1 で、時刻が最後の記録より前 | 出す | しない | する (最大 5 回) |
| VL が 1 で、記録が 0 件 (SD カードがない、または空) | 出す | しない | する |
| VL が 1 で、時刻が範囲外、または 2026 年より前 | 出さない (`----/--/--`) | しない | する |

- 上限は置きません。電源を切っていた期間が分からないためです。
- 記録が 0 件のときは、比べる相手がないので、保存しません。WiFi か BLE で時刻が届くまで待ちます。
- WiFi か BLE で時刻が届くと、RTC に設定します。秒のレジスタを書くので、VL は 0 になります。
- 時刻を設定するとき、RTC の今の時刻との差を、ログに出します。差が 5 分を超えると、警告です (`The time of the RTC was off by N s`)。

### 実装

- ドライバ (`drivers/rtc_pcf8563.c`): VL が 1 で、値が範囲に入っているとき、`rtc_get_time()` は時刻を `timeptr` に入れて、`THERMO_RTC_UNVERIFIED` (正の値 1。`app/common/thermo_rtc.h`) を返します。値が範囲外なら、VL に関係なく `-ENODATA` です。
- `backlog_newest_unix()`: 起動のときの走査 (`backlog_init()`) で見つけた、最も新しい記録の時刻を返します。記録がなければ -1 です。
- `node_time_set_floor()`: `direct_init()` と `direct_sd_check()` が、`backlog_newest_unix()` の値を渡します。`main()` は、`direct_init()` のあとに、時刻を読み直します。
- `node_time_get()`: 時刻を読み、`struct node_datetime` の `is_verified` に、信用できるか (VL が 0、または最後の記録以上) を入れます。VL が 1 で、2026 年より前なら、`-ENODATA` です。
- `node_time_unix()`: 信用できない時刻は、`-ENODATA` を返します。SD カードへの保存 (`direct.c`) と、履歴の復元 (`history_store.c`) が使います。
- メインのループ (`main.c`): WiFi で時刻を取るかを、`is_verified` で決めます。

### 今後の検討

- 起動してからの稼働時間 (`k_uptime`) と、RTC の進みを比べると、止まった時計を見つけられます。たとえば、1 分後に RTC が 1 秒しか進んでいない場合です。実装していません (`TODO.md`)。

## ログ

| ログ | 意味 |
| :--- | :--- |
| `The RTC has the saved time (UTC ...)` | 起動のときに、RTC に保存された時刻を読めた。VL が 1 のときは、末尾に `not verified (VL bit)` が付く |
| `The RTC has VL set, but its time is not before the last record (N s), so it is used` | VL が 1 だが、最後の記録より前に戻っていないので、信用した |
| `The time of the RTC was off by N s` | 時刻を設定するとき、RTC との差 (5 分を超えると警告) |
| `The time of the RTC is not reliable (...). Registers: ...` | VL が 1、または範囲外。そのときの制御レジスタと時刻のレジスタの値を、最初の 1 回だけ出す (有効に戻ると、また出す) |
| `No time in the RTC, getting it over WiFi (try N of 5)` | 信用できる時刻がなく、WiFi で取る |
| `Got the time over WiFi (SNTP) and set it to the RTC (UNIX time ...)` | WiFi で取って、RTC に設定した |

`Registers:` の見方は、[setup.md](setup.md) の「WiFi での時刻の取得」にあります。

## 参考

- PCF8563 のレジスタとドライバ: `app/thermo-node/docs/rtc_pcf8563.md`
- 時刻の換算と、ローカルタイムへの変換: `app/thermo-node/docs/node_time.md`
