# 表示装置と文字のフレームバッファ

OLED の表示 (`zephyr/drivers/display.h` `zephyr/display/cfb.h`) の、使っている API。表示は、`cfb_*` でフレームバッファに描いて、`cfb_framebuffer_finalize()` で画面に出す。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## 表示装置 (display)

### display_set_pixel_format

```c
static inline int display_set_pixel_format(const struct device *dev, const enum display_pixel_format pixel_format);
```

表示装置が使う、ピクセルの形式を設定する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | 表示装置のデバイス |
| `pixel_format` | in | 使うピクセルの形式 |

戻り値: 成功は 0、失敗は負の errno。ドライバが実装していなければ `-ENOSYS`。

使用箇所: [`app/thermo-node/src/oled.c:142`](../app/thermo-node/src/oled.c#L142)

### display_blanking_on

```c
static inline int display_blanking_on(const struct device *dev);
```

表示を消す (ブランキングを有効にする)。画面全体を消すが、フレームバッファの内容は残り、消している間も、読み書きできる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | 表示装置のデバイス |

戻り値: 成功は 0、失敗は負の errno。ドライバが実装していなければ `-ENOSYS`。

注意:

- ドライバがバックライトの制御に対応していれば、バックライトを消す。バックライトの設定は、保持される。
- ドライバがブランキングに対応していれば、ドライバの初期の状態は、この関数を呼んだ状態と同じ。

使用箇所: [`app/thermo-node/src/oled.c:198`](../app/thermo-node/src/oled.c#L198)

### display_blanking_off

```c
static inline int display_blanking_off(const struct device *dev);
```

表示を戻す (ブランキングを無効にする)。フレームバッファの内容を、表示装置に戻す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | 表示装置のデバイス |

戻り値: 成功は 0、失敗は負の errno。ドライバが実装していなければ `-ENOSYS`。

注意:

- ドライバがバックライトの制御に対応していれば、バックライトの設定を戻す。

使用箇所: [`app/thermo-node/src/oled.c:169`](../app/thermo-node/src/oled.c#L169) (ほか 1 行)

## 文字のフレームバッファ (CFB)

### cfb_framebuffer_init

```c
int cfb_framebuffer_init(const struct device *dev);
```

文字のフレームバッファ (Character Framebuffer) を初期化する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | ドライバのインスタンスのデバイス |

戻り値: 成功は 0、失敗は負の値。

使用箇所: [`app/thermo-node/src/oled.c:151`](../app/thermo-node/src/oled.c#L151)

### cfb_framebuffer_deinit

```c
void cfb_framebuffer_deinit(const struct device *dev);
```

文字のフレームバッファの初期化を解除する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | ドライバのインスタンスのデバイス |

使用箇所: [`app/thermo-node/src/oled.c:149`](../app/thermo-node/src/oled.c#L149)

### cfb_framebuffer_set_font

```c
int cfb_framebuffer_set_font(const struct device *dev, uint8_t idx);
```

使うフォントを選ぶ。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | ドライバのインスタンスのデバイス |
| `idx` | in | フォントの番号 |

戻り値: 成功は 0、失敗は負の値。

使用箇所: [`app/thermo-node/src/oled.c:157`](../app/thermo-node/src/oled.c#L157)

### cfb_framebuffer_clear

```c
int cfb_framebuffer_clear(const struct device *dev, bool clear_display);
```

フレームバッファを消す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | ドライバのインスタンスのデバイス |
| `clear_display` | in | `true` なら、表示装置の画面も消す |

戻り値: 成功は 0、失敗は負の値。

使用箇所: [`app/thermo-node/src/oled.c:163`](../app/thermo-node/src/oled.c#L163) (ほか 2 行)

### cfb_print

```c
int cfb_print(const struct device *dev, const char *const str, uint16_t x, uint16_t y);
```

文字列を、フレームバッファの指定の位置に描く。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | ドライバのインスタンスのデバイス |
| `str` | in | 描く文字列 |
| `x` | in | 文字列の先頭の X 方向の位置 [ピクセル] |
| `y` | in | 文字列の先頭の Y 方向の位置 [ピクセル] |

戻り値: 成功は 0、失敗は負の値。

使用箇所: [`app/thermo-node/src/oled.c:434`](../app/thermo-node/src/oled.c#L434) (ほか 1 行)

### cfb_draw_point

```c
int cfb_draw_point(const struct device *dev, const struct cfb_position *pos);
```

点を描く。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | ドライバのインスタンスのデバイス |
| `pos` | in | 点の位置 |

戻り値: 成功は 0、失敗は負の値。

使用箇所: [`app/thermo-node/src/oled.c:408`](../app/thermo-node/src/oled.c#L408) (ほか 2 行)

### cfb_draw_line

```c
int cfb_draw_line(const struct device *dev, const struct cfb_position *start, const struct cfb_position *end);
```

線を描く。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | ドライバのインスタンスのデバイス |
| `start` | in | 線の始点 |
| `end` | in | 線の終点 |

戻り値: 成功は 0、失敗は負の値。

使用箇所: [`app/thermo-node/src/oled.c:406`](../app/thermo-node/src/oled.c#L406)

### cfb_framebuffer_finalize

```c
int cfb_framebuffer_finalize(const struct device *dev);
```

フレームバッファを確定して、表示装置の RAM に書き込む。必要なら、ピクセルの反転や並べ替えをする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dev` | in | ドライバのインスタンスのデバイス |

戻り値: 成功は 0、失敗は負の値。

注意:

- 描いたあとに呼ばないと、画面に出ない。

使用箇所: [`app/thermo-node/src/oled.c:244`](../app/thermo-node/src/oled.c#L244) (ほか 1 行)
