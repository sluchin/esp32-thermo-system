# button.c の解析

対象: `app/thermo-node/src/button.c` (76 行) と `button.h` (26 行)

拡張ボードのユーザボタン (D1 = GPIO3) を読みます。OLED のページの切り替えと、消灯中の OLED の点灯に使います。`CONFIG_THERMO_DISPLAY=n` のときは、ビルドされません。

ボタンは、Devicetree の alias `thermo-button` で選びます。`boards/xiao_esp32c3.overlay` の設定は、プルアップで、押すと 0 になる (`GPIO_PULL_UP | GPIO_ACTIVE_LOW`) ものです。`gpio_pin_get_dt()` は `ACTIVE_LOW` を考慮するので、押されていれば 1 を返します。

## 公開する関数

| 関数 | 役割 |
|:---|:---|
| `button_init()` | GPIO を入力に設定して、今の状態を覚える |
| `button_is_pressed()` | 前回の呼び出しのあとに、押されたか |

## button_init

```text
gpio_is_ready_dt() でなければ                -> -ENODEV
gpio_pin_configure_dt(GPIO_INPUT)             失敗 -> その値
is_prev_down = (gpio_pin_get_dt() > 0)             <- 起動のときに押されていても押された動作とはしない
```

## button_is_pressed

```c
bool is_down = (gpio_pin_get_dt(&button) > 0);   /* 今押されているか */
bool is_pressed = (is_down && !is_prev_down);           /* 押された動作か */
is_prev_down = is_down;
return is_pressed;
```

**押されていない状態から押された状態に変わった呼び出しだけ**、true を返します。押し続けても、1 回だけです。GPIO を読めないとき (戻り値が負) は、押されていないものとして扱います。

## チャタリングの扱い

割り込みは使わず、`main.c` が 50 ms ごとに呼びます。前回と今回の状態を比べるだけなので、**呼び出しの間隔 (50 ms) が、チャタリングを無視する時間になります。** ソフトウェアのデバウンスのタイマーはありません。50 ms より短い接点の揺れは見えません。

## 注意点

- 押してすぐ離した場合に、50 ms の周期の呼び出しが押された状態を見逃すと、検出できません。実際に押す時間は 50 ms より長いので、問題ありません。
- ほかの処理 (`wifi_time_fetch()` の最大 25 秒など) がメインループを止めている間は、ボタンも検出できません。

## 関連

- [main.md](main.md): ボタンによるページの切り替えと、点灯
- [index.md](index.md): このディレクトリの文書の一覧
