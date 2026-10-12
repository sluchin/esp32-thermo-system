# oled.c の解析

対象: `app/thermo-node/src/oled.c` (634 行) と `oled.h` (62 行)

拡張ボードの OLED (SSD1306、128 × 64 ドット、I2C のアドレス 0x3C) に表示する部分です。Zephyr の CFB (Character Frame Buffer: 文字のフレームバッファ) を使います。3 種類の画面を描きます。

| 画面 | 関数 | 内容 |
|:---|:---|:---|
| 現在値のページ | `oled_show()` (`OLED_PAGE_NOW`) | 日付、時刻、温度、湿度 |
| グラフのページ | `oled_show()` (`OLED_PAGE_TEMP_GRAPH`、`OLED_PAGE_HUMIDITY_GRAPH`) | 履歴の最小と最大、折れ線グラフ |
| 起動画面 | `oled_show_boot()` | 題名と、ドットを食べながら走る絵のアニメーション |

ページの切り替え (ボタン) と、点灯と消灯のタイミングは、`main.c` が決めます。このファイルは、描くだけです。`CONFIG_THERMO_DISPLAY=n` のときは、ビルドされません。

## 公開する関数

| 関数 | 役割 |
|:---|:---|
| `oled_init()` | 画素の形式、フレームバッファ、フォントを設定し、画面を消して、点灯する |
| `oled_set_power()` | 点灯 (`display_blanking_off`) または消灯 (`display_blanking_on`) |
| `oled_show()` | `struct oled_view` の内容を、1 画面ぶん描いて、表示を更新する |
| `oled_show_boot()` | 起動画面の `frame` 番目のコマを描いて、表示を更新する (`CONFIG_THERMO_BOOT_SCREEN`) |

## struct oled_view

`main.c` が持つ、表示する内容です。

| メンバー | 内容 |
|:---|:---|
| `has_sample` | 測定値があるか。false なら、温度と湿度を `--.-` にする |
| `temp_x10`、`humidity_x10` | 温度 (℃ の 10 倍)、湿度 (% の 10 倍。0xFFFF は、湿度なし) |
| `time` | 日時。NULL なら、日付と時刻の行を出さない |
| `page` | 表示するページ (`OLED_PAGE_NOW`、`OLED_PAGE_TEMP_GRAPH`、`OLED_PAGE_HUMIDITY_GRAPH`)。ページの数は `OLED_PAGE_COUNT` (3) |
| `temp_history`、`humidity_history` | グラフの履歴。NULL なら、データなし |

## oled_init

```text
device_is_ready(oled_dev) でなければ        → -ENODEV
display_set_pixel_format(MONO10)             ← 1 ビット/ドットで、1 が点灯
cfb_framebuffer_deinit()                     ← やり直しのため、前のフレームバッファを解放する
cfb_framebuffer_init()
cfb_framebuffer_set_font(0)                  ← フォント 0 (幅 10 × 高さ 16 ドット)
cfb_framebuffer_clear(true)                  ← 画面も消す
display_blanking_off()                       ← 点灯
```

失敗したあとに、もう一度呼んでも、安全です。`main.c` は、60 秒ごとに、使えない機器を、初期化し直します。

## 画面の寸法

| 項目 | 値 |
|:---|:---|
| 画面 | 128 × 64 ドット |
| 文字 | 幅 10 × 高さ 16 ドット (フォント 0)。1 行に 12 文字、4 行 |
| 行の位置 | 上から `行 × 16` ドット (`draw_line()`) |
| グラフの範囲 | 縦は y = 20 から 63 (`OLED_GRAPH_TOP` から `OLED_GRAPH_BOTTOM`)、横は 128 ドット |

## 現在値のページ

`format_lines()` が、行ごとの文字列を作り、`draw_line()` が、1 行ずつ描きます。

| 行 (日時あり) | 内容 | 例 |
|:---|:---|:---|
| 0 | 日付 | `2026-10-12` |
| 1 | 時刻 | `09:37:51` |
| 2 | 温度 | `Temp 23.5 C` |
| 3 | 湿度 | `Humi 45.0 %` |

- 日時が NULL なら、日付と時刻の行はなく、温度が 0 行目、湿度が 1 行目です (2 行)。
- 日時があって `valid` が false (時刻が未設定) なら、`----/--/--` と `--:--:--` です。
- 測定値がなければ、`Temp --.- C` と `Humi --.- %`。湿度だけがなければ (`THERMO_HUMIDITY_NONE`)、`Humi --.- %`。
- 温度は `format_x10()` で、10 分の 1 の値を小数点つきの文字列にします。0 ℃ 未満は、整数部が 0 でも (-0.5 など) 符号が出るように、符号と絶対値を分けて作ります。

## グラフのページ

```text
points = history_get(...)                   ← 古い順に、最大 128 点
low、high = 測定値のある点 (HISTORY_NONE でない点) の最小と最大
測定値のある点がない → "No data" (1 行目) を出して終わり
1 行目: "<low>~<high> C" (温度) または "<low>~<high> %" (湿度)
折れ線: draw_polyline(points, count, low - 50, (high - low) + 100)
```

### 縦軸の計算

```text
y = 63 - ((value - 下端) * (63 - 20)) / 幅
下端 = low - 50、幅 = (high - low) + 100       ← 単位は 10 分の 1。上下に 5.0 の余白
```

- 余白 (`OLED_GRAPH_MARGIN_X10` = 50) を、最小の下と最大の上に足します。温度が全く変化しなくても (`low == high`)、幅が 100 になるので、0 で割ることはなく、線が画面の真ん中あたりに出ます。
- 最小の点は、下端から 5.0 だけ上の位置に、最大の点は、上端から 5.0 だけ下の位置に出ます。

### 折れ線 (draw_polyline)

- 点が `count` 個のとき、x 座標は `128 - count + i` です。**右端が最新**で、点が少ないときは、右に寄ります。
- 隣り合う点を `cfb_draw_line()` で結びます。
- `HISTORY_NONE` の点は、描きません。その前後は、線で結ばず、次の測定値のある点を、`cfb_draw_point()` で打ちます。電源が入っていなかった間は、グラフの切れ目になります。

## 起動画面

`oled_show_boot(frame)` は、`OLED_BOOT_FRAME_MS` (60 ms) ごとに、`main.c` の `boot_sequence()` が、`OLED_BOOT_FRAMES` (50) コマ呼びます。合計で約 3 秒です。`frame` が 50 以上のときは、最後のコマ (49) として描きます。

`draw_boot_frame()` が描くのは、4 つです。

| 部品 | 位置と動き |
|:---|:---|
| 題名 (`boot_title.text`) | 画面の上端。横は `(128 - 文字数 × 10) / 2` で、中央に寄せる |
| ドット | y = 24 + 7。x = 4 + 8 × i (i は 0 から 15)。2 × 2 ドット。走る絵の中心 (x + 8) が通り過ぎたドットは、食べられて消える |
| 走る絵 (`boot_chomper`) | y = 24。x = -16 + 3 × コマ。口の開き方は、コマ % 4 で 閉じる、半分、開く、半分 の順 |
| 追いかける絵 (`boot_chaser`) | 走る絵の 28 ドット左 |

- 走る絵は、画面の左の外から始まり、右の外 (x = 131) まで進みます。`draw_sprite()` は、画面の外 (0 から 127 の外) のドットを描きません。
- 絵は、`struct boot_sprite` (16 行、各行が `uint16_t` で、最上位ビットが左端) です。題名は、12 文字までで、それを超えるとコンパイルエラーになります (配列の大きさが 13)。素材の定義は、[boot_assets.md](boot_assets.md) にあります。

## 補助の関数

| 関数 | 役割 |
|:---|:---|
| `draw_now()` | 現在値のページ。`format_lines()` と `draw_line()` を呼ぶ |
| `draw_graph()` | グラフのページ |
| `draw_polyline()` | 折れ線 |
| `draw_line()` | `cfb_print()` で 1 行を描く |
| `format_lines()`、`format_x10()` | 文字列を作る |
| `draw_boot_frame()`、`draw_dots()`、`draw_sprite()`、`is_in_range()` | 起動画面 |

## 気づいた点

- `oled_show()` は、描く前に `cfb_framebuffer_clear(false)` で、フレームバッファだけを消し、描き終えてから、`cfb_framebuffer_finalize()` で、1 回だけ画面に送ります。そのため、描いている途中の状態が、画面にちらつくことは、ありません。
- 消灯中 (`blanking`) も、フレームバッファの内容は残ります。`main.c` は、消灯中の `oled_show()` を止めています。
- `draw_graph()` は、`int16_t points[128]` (256 バイト) をスタックに置きます。メインスレッドのスタックは 6144 バイトです。
- グラフの 1 点が、1 ドットです。区切り (既定 170 秒) の平均なので、5 秒ごとの測定値の細かい揺れは、見えません。

## 関連

- [main.md](main.md): 点灯と消灯、ページの切り替え、`oled_show()` の呼び出し
- [history.md](history.md): `history_get()`
- [boot_assets.md](boot_assets.md): 起動画面の素材
- [index.md](index.md): このディレクトリの文書の一覧
