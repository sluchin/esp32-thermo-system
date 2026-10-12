# button.c の解析

対象: `app/thermo-node/src/button.c` (76 行) と `button.h` (26 行)

拡張ボードのユーザボタン (D1 = GPIO3) を読みます。OLED のページを切り替えたり、消灯中の OLED を点灯したりするために使います。`CONFIG_THERMO_DISPLAY=n` のときは、ビルドされません。

ボタンは、Devicetree の alias `thermo-button` で選びます。`boards/xiao_esp32c3.overlay` では、プルアップで、押すと 0 になる (`GPIO_PULL_UP | GPIO_ACTIVE_LOW`) 設定です。`gpio_pin_get_dt()` が、`ACTIVE_LOW` を考慮するので、押されていれば 1 が返ります。

## 公開する関数

| 関数 | 役割 |
|:---|:---|
| `button_init()` | GPIO を入力に設定して、今の状態を覚える |
| `button_pressed()` | 前回の呼び出しのあとに、押されたか |

## button_init

```text
gpio_is_ready_dt() でなければ                → -ENODEV
gpio_pin_configure_dt(GPIO_INPUT)             失敗 → その値
was_down = (gpio_pin_get_dt() > 0)             ← 起動のときに押されていても、押された動作とはしない
```

## button_pressed

```c
bool down = (gpio_pin_get_dt(&button) > 0);   /* 今、押されているか */
bool pressed = (down && !was_down);           /* 押された動作か */
was_down = down;
return pressed;
```

**押されていない状態から、押された状態に変わった呼び出しだけ**、true を返します。押し続けても、1 回だけです。GPIO を読めないとき (戻り値が負) は、押されていないものとして扱います。

## チャタリングの扱い

割り込みを使わず、`main.c` が 50 ms ごとに呼びます。前回と今回の状態の比べ方だけなので、**呼び出しの間隔 (50 ms) が、チャタリングを無視する時間になります。** ソフトウェアのデバウンスのタイマーはありません。50 ms より短い間の、接点の揺れは、見えません。

## 気づいた点

- 押されて、すぐ離された場合でも、押された状態を 50 ms の周期の呼び出しが見逃すと、検出できません。実際の押下は、50 ms より長いので、問題ありません。
- `main.c` で、ほかの処理 (`wifi_time_fetch()` の最大 25 秒など) がメインループを止めている間は、ボタンも検出できません。

## 関連

- [main.md](main.md): ボタンによるページの切り替えと、点灯
- [index.md](index.md): このディレクトリの文書の一覧
