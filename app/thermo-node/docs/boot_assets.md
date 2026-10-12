# boot_assets.c、button_assets.c、notes.h の解析

対象: `app/thermo-node/src/boot_assets.c` (155 行)、`boot_assets.h` (55 行)、`button_assets.c` (25 行)、`button_assets.h` (30 行)、`notes.h` (85 行)

起動画面の絵と題名、起動音とボタンの音のメロディといった、**素材のデータ** です。処理はなく、定数の表だけです。素材を、コードから分けてあるのは、別の素材に**差し替えられる**ようにするためです。

## 素材の差し替えの仕組み

```text
ビルドのとき (CMakeLists.txt)
    private/src/boot_assets.c があれば   → それを使う
    なければ                             → src/boot_assets.c (このアプリのオリジナル)
    private/src/button_assets.c も同様
```

`app/thermo-node/private` は、別のプライベートリポジトリを、サブモジュールとして置くための場所です (`SETUP.md` の「起動画面と起動音」)。公開リポジトリには、著作権のある素材 (既存のゲームの絵や曲) を置かないために、こうしています。差し替えるファイルは、`boot_assets.h` と `button_assets.h` で宣言した全てを、同じ名前と形で定義します。素材がなくても、ビルドできます。

CI は、サブモジュールを取り込みません。素材を入れてビルドした実行ファイル (`zephyr.elf`、`zephyr.bin`) は、公開しません。

## boot_assets.h の宣言

| 名前 | 型 | 内容 |
|:---|:---|:---|
| `boot_chomper[3]` | `struct boot_sprite` の配列 | 走る絵。口の開き方が違う 3 つ (閉じる、半分、開く)。右向き |
| `boot_chaser` | `struct boot_sprite` | 追いかける敵の絵 |
| `boot_title` | `struct boot_title` | 画面の上に出す題名 |
| `boot_melody[]`、`boot_melody_count` | `struct buzzer_note` の配列と、その数 | 起動音 |

### struct boot_sprite (絵)

`uint16_t rows[16]` です。16 × 16 ドットで、1 行が 1 つの `uint16_t`、**最上位ビットが左端**で、1 が点灯です。`boot_assets.c` では、見た目が分かるように、各行の右に `/* ....########.... */` のようなコメントを付けています。

この表だけ、`/* clang-format off */` と `/* clang-format on */` で囲み、整形を止めています。整形ツールが、初期化の波括弧の位置を変えて、絵が崩れるためです。

### struct boot_title (題名)

`char text[13]` です。12 文字と終端の NUL で、幅 128 ドットに収まります (1 文字が 10 ドット)。長い題名は、配列の大きさが合わず、コンパイルエラーになります。このアプリは `THERMO NODE`。

## button_assets.h の宣言

| 名前 | 内容 |
|:---|:---|
| `button_melody[]`、`button_melody_count` | ボタンを押したときの音。約 0.1 秒の、上がる 2 音 |

## notes.h: 音名から周波数への表

メロディを、周波数の数字ではなく、音名で書くための表です。平均律で、A4 = 440 Hz です。

- 範囲は `NOTE_C3` (131 Hz) から `NOTE_C8` (4186 Hz) まで、61 音です。
- 名前は、`NOTE_` + 音名 + オクターブです。半音は、シャープを `S` で書きます (`NOTE_CS4` はド#)。フラットは、同じ高さのシャープの名前を使います (レ♭は `NOTE_CS`、ミ♭は `NOTE_DS`)。
- 休符は `NOTE_REST` (0)。`buzzer.c` は、周波数 0 を、音を消す意味に扱います。
- 値は `U` つきの整数で、`uint16_t` に収まります (`struct buzzer_note` の `freq_hz`)。

メロディは、`{NOTE_C5, 100}` のように、音名と長さ [ms] の組で並べます。

## 使われ方

| 素材 | 使う場所 |
|:---|:---|
| `boot_title`、`boot_chomper`、`boot_chaser` | `oled.c` の `draw_boot_frame()` ([oled.md](oled.md)) |
| `boot_melody` | `main.c` の `boot_sequence()` が `buzzer_play()` に渡す |
| `button_melody` | `main.c` の `play_button_sound()` が `buzzer_play()` に渡す |

## 関連

- [oled.md](oled.md): 起動画面の描画
- [buzzer.md](buzzer.md): メロディの再生
- [main.md](main.md): `boot_sequence()`
- [index.md](index.md): このディレクトリの文書の一覧
