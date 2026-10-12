# buzzer.c の解析

対象: `app/thermo-node/src/buzzer.c` (137 行) と `buzzer.h` (33 行)

拡張ボードのパッシブブザー (D3 = GPIO5) を、PWM で鳴らします。パッシブ型は、PWM の **周波数が音の高さ** になるので、メロディを、音の高さ (周波数) と長さの表で与えて、順に鳴らします。起動音 (`boot_melody`) と、ボタンの音 (`button_melody`) に使います。`CONFIG_THERMO_BUZZER=n` のときは、ビルドされません。

ブザーは、Devicetree の alias `thermo-buzzer` で選びます (`compatible = "thermo,buzzer"`、`pwms = <&ledc0 0 ...>`。バインディングは `dts/bindings/pwm/thermo,buzzer.yaml`)。

## 公開する関数と構造体

| 項目 | 役割 |
|:---|:---|
| `struct buzzer_note` | 音 1 つ。`freq_hz` (周波数 [Hz]。0 は休符) と `duration_ms` (長さ [ms]) |
| `buzzer_init()` | PWM のデバイスが使えるかを確かめる (`-ENODEV`) |
| `buzzer_play(notes, count)` | メロディを鳴らし始める。すぐに戻る |

## 鳴らす仕組み

呼び出し元を待たせないために、**システムのワークキュー** で 1 音ずつ進めます。

```text
buzzer_play(notes, count)
    鳴っているメロディのワークを取り消す
    playing = notes、play_count = count、play_pos = 0
    step_work を即時に予約 ───────────┐
                                       ▼
step_handler()  (ワークキューで実行)
    メロディがない、または全部鳴らした → set_tone(0) で音を消して終わり
    note = notes[play_pos++]
    set_tone(note.freq_hz)            失敗 → ログを出して、メロディを止める
    note.duration_ms 後に、自分をもう一度予約
```

- 鳴っている途中に `buzzer_play()` を呼ぶと、今のメロディを止めて、新しいメロディを最初から鳴らします。ボタンを押すたびに、起動音の途中でも、ボタンの音に切り替わります。
- `count` が 0 のときは、鳴らしているメロディを止めて、音を消します。
- `notes` は、鳴らし終わるまで、書き換えないでください。コピーを取らず、ポインタを保持します。`static const` の表を渡します。
- `buzzer_play()` は、常に `EXIT_SUCCESS` を返します。ワークキューが止まっているときは、鳴らないだけです。

### set_tone

```text
freq == 0 → pwm_set_pulse_dt(0)                            ← 音を消す
それ以外   → period = 1000000000 / freq [ns]
             pwm_set_dt(period, period / 2)                ← デューティ比 50%
```

Devicetree の `PWM_HZ(1000)` は、周期の初期値です。鳴らすときに、音ごとに周期を設定するので、使われません。

## 音の表の書き方

音名は `notes.h` の `NOTE_C4` などの定数で書きます ([boot_assets.md](boot_assets.md))。**音と音の間には、休符を入れないと、音が続けてつながります** (同じ高さの音を 2 回続けると、1 つの長い音になります)。区切りたいところに、`NOTE_REST` を入れます。

## 気づいた点

- `k_work_cancel_delayable()` は、実行中のハンドラの終了を待ちません。取り消しと同時にハンドラが動いている場合は、`playing` と `play_pos` を、そのあとで書き換えます。メインスレッドとワークキューの間の排他制御はありません。ボタンの音は、押下の間隔 (人の操作) が十分に長いので、問題になりません。
- PWM の失敗は、ハンドラの中で `playing = NULL` にして、メロディを止めます。

## 関連

- [boot_assets.md](boot_assets.md): 音名の表と、メロディの素材
- [main.md](main.md): 起動音とボタンの音の呼び出し
- [index.md](index.md): このディレクトリの文書の一覧
