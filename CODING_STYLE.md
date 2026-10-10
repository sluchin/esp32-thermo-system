# コーディングスタイル

このプロジェクトの C ソース (`app/` 配下。実装と単体テスト) は、[Zephyr のコーディング規約](https://docs.zephyrproject.org/latest/contribute/style/code.html) に合わせます。Zephyr と違う点 (例外) は、このドキュメントに、理由とともに記録します。

MISRA-C への対応方針と、あえて従わない規則は、[MISRA.md](MISRA.md) に書いてあります。日常の規約 (命名、ログ、戻り値、Doxygen など) は、「4. このプロジェクトの規約」にまとめています。

## 1. 規約を守るための道具

Zephyr と同じ道具を、Zephyr のバージョン (v4.3.0) に合わせて、Docker イメージの中で使います。

| 道具 | 設定 | 内容 |
|:---|:---|:---|
| clang-format | [.clang-format](.clang-format) | 整形。Zephyr の `.clang-format` に、6 点だけ変えたもの (例外 1、例外 5。`StatementMacros` の追加は、下の注を参照) |
| EditorConfig | [.editorconfig](.editorconfig) | エディタの設定。Zephyr の `.editorconfig` に、C / C++ / Perl と、Devicetree / Kconfig のインデントだけ変えたもの (例外 1) |
| checkpatch.pl | Zephyr の `.checkpatch.conf` | Zephyr のリンター。インデントの 3 種類だけ無視する (例外 1) |
| 行末の空白 | `scripts/check-whitespace.sh` | 全てのテキストファイル (Markdown、YAML、CMake、シェルスクリプトなど) に、行末の空白がないことを確認する (`.editorconfig` の `trim_trailing_whitespace = true` に対応。patch ファイルと `LICENSE` は、対象外) |
| コンパイラの警告 | [app/warnings.txt](app/warnings.txt) | 約 55 個の警告オプション。警告ゼロを維持する |
| 静的解析 | gcc `-fanalyzer` | 指摘ゼロを維持する |

```bash
# 整形の確認 (整形が必要なファイルがあれば失敗する)
docker compose run --rm format-thermo
# 整形して、ファイルを書き換える
docker compose run --rm format-thermo-fix
# checkpatch.pl による検査 (指摘があれば失敗する)
docker compose run --rm lint-thermo
# 行末の空白の確認 (全てのテキストファイル)
docker compose run --rm whitespace-thermo
```

`.clang-format` の `StatementMacros` には、`K_THREAD_STACK_DEFINE` が入っています。Zephyr のこのマクロは、末尾に `;` を付けない書き方なので、clang-format が文の終わりを見失い、次のコメントや関数を、8 桁字下げしてしまうためです。

コードを修正したら、`format-thermo-fix` で整形してから、`lint-thermo` と `whitespace-thermo` を実行します。CI (GitHub Actions) の `lint` ジョブも、同じ確認を行います。

Zephyr のバージョンを上げたときは、Zephyr の `.clang-format` と `.editorconfig` を取り込み直して、例外 1 (`.editorconfig` の Devicetree と Kconfig を含む) と例外 5 の変更 (と、初期化リストのインデント、`StatementMacros`) だけを、もう一度行います。

## 2. Zephyr と同じにしているもの (例外ではない)

次は、Zephyr の規約のとおりです。

- **中括弧の位置**: 関数の定義は、次の行。制御構文は、同じ行 (Linux スタイル)。
- **1 行の長さ**: 100 桁まで。
- **ポインタの `*`**: 変数名に付ける (`char *p`)。
- **命名**: スネークケース (`snake_case`)。マクロと定数は、大文字。
- **`#include` の順序**: 整形で並べ替えない (`SortIncludes: Never`)。
- **ファイルの先頭に、著作権とライセンスを書く**: C のソースとヘッダ (実装と単体テスト) の先頭に、Zephyr の形式で、`Copyright` と `SPDX-License-Identifier` を付けます。関数ごとには、付けません。関数には、Doxygen コメントを付けます。

  ```c
  /*
   * Copyright (c) 2026 Tetsuya Higashi
   *
   * SPDX-License-Identifier: GPL-3.0-or-later
   */
  ```

  ライセンスの種類だけは、Zephyr (Apache-2.0) と違い、このリポジトリの [LICENSE](LICENSE) (GPLv3) に合わせて、`GPL-3.0-or-later` です。新しいファイルにも、付けてください。

## 3. 例外 (Zephyr と違うところ)

### 例外 1: インデントは、タブではなく、空白 4 つ

- **Zephyr**: タブ (幅 8)。
- **このプロジェクト**: 空白 4 つ (C のソースとヘッダ。実装と単体テスト。Devicetree と Kconfig の設定ファイルも)。
- **理由**: 空白 4 つの方が、浅く見えて、100 桁の中に、収まりやすいためです。ほかのプロジェクトのコードとも、そろいます。
- **設定**:
  - `.clang-format`: `IndentWidth: 4`、`UseTab: Never`、`TabWidth: 4` (この 3 行だけが、Zephyr と違います。折り返した行は、Zephyr のとおり、開き括弧にそろえます。括弧の中で折り返さないときの字下げは、8 です)。構造体などの初期化リストの中だけは、`BracedInitializerIndentWidth: 4` で、4 にします。
  - `.editorconfig`: C のソースとヘッダ (`*.c`、`*.h`)、C++ (`*.cpp`、`*.hpp`)、Perl (`*.pl`) を、`indent_style = space`、`indent_size = 4` にする。C++ と Perl のファイルは、今のリポジトリにはなく、C と同じ流儀にそろえてある。Devicetree (`*.dts`、`*.dtsi`、`*.overlay`) と Kconfig (`Kconfig*`) も、`indent_style = space`、`indent_size = 4` にする (Kconfig の `help` の本文は、`help` より、空白 2 つ深くする)。patch ファイル (`*.patch`、`*.diff`) の `trim_trailing_whitespace = false` は、Zephyr のとおり (patch の空行の文脈を、壊さないため)。
  - checkpatch.pl: インデントの検査は、タブが前提なので、`LEADING_SPACE`、`CODE_INDENT`、`SUSPECT_CODE_INDENT` の 3 種類だけ無視する (`scripts/lint.sh`)。インデントは、clang-format が確認する。
- **対象外**: `prj.conf` は、字下げがないので、変えていません。

### 例外 2: コメントは、日本語

- **Zephyr**: コメントは、英語。
- **このプロジェクト**: ソースのコメント (Doxygen コメントを含む) は、日本語 (「4. このプロジェクトの規約」の Doxygen コメントの規約)。
- **理由**: このプロジェクトの読み手が、日本語を使うためです。
- **注意**: ログのメッセージ (`LOG_INF` など) と、識別子は、英語です。

### 例外 3: コミットメッセージの形式

- **Zephyr**: `subsystem: summary` の形式で、`Signed-off-by:` を付ける。
- **このプロジェクト**: 件名は、Conventional Commits の `type(scope): description` の形式 (Zephyr の `subsystem: summary` ではなく、一般的な流儀にそろえる。type の一覧は、 `.claude/CLAUDE.md` のコミットガイドライン)。英語で書き、本文は `- ` で始まる箇条書き。`Signed-off-by:` や、`Co-Authored-By:` などの帰属行は付けない。

### 例外 4: 関数の戻り値に、名前付き定数を使う

- **Zephyr**: 成功は `0`、失敗は負の `errno` 値 (`return 0;`、`return -EIO;`)。
- **このプロジェクト**: 成功は `EXIT_SUCCESS` (値は 0)、失敗は負の `errno` 値 (`-ENODEV` など)。`main()` の失敗は `EXIT_FAILURE`。
- **理由**: MISRA-C の方針で、戻り値にリテラルを直書きしないためです (`MISRA.md` を参照)。値の意味は、Zephyr と同じです。

### 例外 5: 1 文の本体には、{} を付けない

- **Zephyr**: `if` / `else` / `for` / `while` の本体は、1 文でも `{}` を付ける (`.clang-format` の `InsertBraces: true`)。
- **このプロジェクト**: 本体が 1 文のときは、`{}` を付けなくてよい (付けてもよい)。2 文以上のときは、付ける。
- **設定**: `.clang-format` の `InsertBraces: false` (整形のときに、`{}` を自動で付けない。すでにある `{}` も、自動では消さない)。
- **注意**: MISRA C:2012 Rule 15.6 の例外でもある (`MISRA.md` の例外 7)。

## 4. このプロジェクトの規約

Zephyr の規約に加えて、このプロジェクトで決めている規約です。Zephyr の規約にはないか、緩いものは、より厳しく決めています (MISRA-C の方針は、[MISRA.md](MISRA.md))。

### 基本

1. **インデント**: 空白 4 つ (Zephyr は、タブ 8。例外 1)。整形は clang-format (`.clang-format`) が行う。
2. **ログ**: `LOG_INF`, `LOG_ERR`, `LOG_DBG` 等の Zephyr Logging API を使用。
   - 各ファイルで `LOG_MODULE_REGISTER` を定義。
3. **戻り値**: 成功は `EXIT_SUCCESS`、エラー時は負の `errno` 値を返す（例: `-ENODEV`, `-EIO`）。
4. **ハードウェア操作**: Devicetree (DT) と `device_is_ready()` を使用してドライバにアクセス。
5. **ヘッダ**: `#include <zephyr/...>` 形式で Zephyr API をインクルード。
6. **変数命名**: スネークケース (`snake_case`)。

### 共通ルール

1. **戻り値は必ずチェックする**: 戻り値を返す関数は、戻り値を確認し、失敗時は `LOG_ERR` でログを出力して呼び出し元へ伝える。
2. **無視する戻り値は `(void)` でキャストする**: あえてチェックしない標準関数や API は `(void)memset(...)` のように明示する。
3. **エラー処理は、早期リターンで行う**: `goto` は使わない (MISRA C:2012 Rule 15.1。`MISRA.md` を参照)。アプリのコードは、解放が必要なリソースを確保しないので、エラーの原因を `LOG_ERR` で出力して、すぐに `return` する。
4. **動的メモリは原則使わない**: 静的確保を使う。やむを得ず確保する場合は、結果を `NULL` チェックし、解放後に `NULL` を代入する。
5. **バッファの安全性**: `strcpy` / `sprintf` / `strcat` などの長さを見ない関数は使わない。`snprintf` など長さを指定する関数を使い、サイズと境界を確認する。
6. **ログに `printk` / `printf` を使わない**: 必ず Zephyr Logging API (`LOG_*`) を使う。
7. **MISRA-C に従えない箇所**: 理由をコメントで残す。
8. **行末コメントは桁を揃える**: 型やサフィックス (`U` `L`) を足して、コードの長さが変わったら、連続する行の行末コメントの桁も揃え直す。
9. **整数リテラルは、全て大文字にする**: サフィックスは `U` `UL` `ULL` `L` `LL`、16 進数の数字は `0xFF` のように、大文字で書く (小文字の `u` `ul` `l` と、`0xff` は使わない)。ただし、浮動小数点リテラルのサフィックス `f` は、一般的な書き方のとおり、小文字のままにする (`1.0f`。`1.0F` とは書かない)。16 進数の数字の `F` (`0xFF`) は、サフィックスではないので、大文字にする。`unsigned long` の変数への代入・初期化、比較、演算に使う整数リテラルには、`UL` を付ける (`unsigned long mask = 0UL;`)。`unsigned long long` には、`ULL` を付ける (`* 1000000ULL`)。`U` や `ul` は使わない。ビットマスクの反転は、`~(size_t)7U` のように、幅を合わせてから反転する (`~7U` は 32 ビットなので、64 ビットの `size_t` では上位ビットが落ちる)。
10. **「最大のサイズ」の指定**: 巨大な `size_t` が必要なときは、 `(size_t)-1` のようにキャストせず、`SIZE_MAX` (`<stdint.h>`) を使う。
11. **ヘッダのインクルードガード**: ファイルの場所とファイル名を大文字にして、`.` を `_` にした形式にする (`thermo-gateway/src/cfg.h` → `THERMO_GATEWAY_CFG_H`)。アンダースコアで始めない (予約済み識別子を避ける。MISRA C:2012 Rule 21.1 / 21.2)。標準ヘッダには、使う関数をコメントで添える (`#include <string.h> /* memset memcpy */`)。
12. **ローカル変数の行末コメント**: ローカル変数には、宣言時の初期化に加えて、行末に、意味と単位をコメントで付ける (`int temp = 0; /* 温度 [℃ の 10 倍] */`)。
13. **関数の最後の `return` の前には、空行を入れる**: 関数の最後の `return` は、直前の文との間に空行を 1 行入れる (本体が `return` だけの関数は、入れない)。直前にコメントがあるときは、コメントの前に入れる。ただし、直前が `LOG_*` のときは、`LOG_*` の前には空行を入れず、直前の文 (`if` のブロックの `}` など) に続けて書く (直前がローカル変数の宣言のときは、checkpatch.pl の `LINE_SPACING` のため、空行を入れる)。`LOG_*` と `return` の間は、空行を入れる (`}` → `LOG_INF(...)` → 空行 → `return`)。
14. **範囲の判定は、数直線の順に書く**: 範囲に入っているかは `(min <= value) && (value <= max)`、範囲の外かは `(value < min) || (max < value)` のように、小さい値を左に書く (`(value >= min) && (value <= max)` と書かない。条件が、数直線の上の並びと、同じ向きになり、読みやすい)。リテラルの定数が左に来ると、checkpatch.pl の `CONSTANT_COMPARISON` が警告するので、`(0 <= px)` ではなく、名前付きの定数 (`(OLED_LEFT_X <= px)`) にする。判定をする関数の名前は、`is_in_range()` のように、`is_` か `has_` で始める (「MISRA-C 準拠ルール」の 1)。
15. **関数の並び**: `.c` では、公開関数を先に、`static` 関数を後ろに並べる (概要から読めるようにする。calc と同じ)。`static` 関数のプロトタイプは、ファイルの先頭 (最初の関数の前。`static` の変数や、初期化の表のように、関数より前で `static` 関数の名前を使うものがあるときは、その前) にまとめて書き、コメントは付けない (定義のコメントと重複させない。Doxygen は、定義のコメントを、宣言にも使う)。`#if` の中の `static` 関数は、その場所に置く。単体テスト (`app/*/tests/`) は、対象外。
16. **データの表は, 整形を止めてよい**: 絵のビットマップのように, 見た目を, そのまま残したい表だけ, `/* clang-format off */` と `/* clang-format on */` で囲んでよい (理由を, コメントに書く。例: `boot_assets.c`)。囲んだ部分でも, checkpatch.pl の指摘 (波括弧は, 同じ行に書く, など) には, 従う。

### MISRA-C 準拠ルール

`app/` 配下の C ソースは極力 MISRA-C に従う。
MISRA-C への対応方針と、あえて従わない規則 (例外事項。Deviations) は、[MISRA.md](MISRA.md) を参照。例外を増やすときは、`MISRA.md` に理由と安全対策を追記する。

1. **条件式の中の関数呼び出し**: 判定だけをする関数 (状態を調べるだけで、何も変えない関数) は、条件式に、そのまま書いてよい。副作用のある関数 (状態を変える、書き込む、開始・初期化・設定する関数) は、先に呼び出して、戻り値を変数に入れてから、判定する。
   - 条件式に書いてよい関数: `bool` を返す判定関数 (`adc_is_ready_dt()` `device_is_ready()` や、自分で作る `is_xxx()` / `has_xxx()`)、`isdigit()` などの `is*()`、`strcmp()` (文字列が同じかの判定)、`strlen()` など。
   - 変数に入れる関数の例: `bt_enable()` `bt_le_adv_start()` `bt_le_scan_start()` `ble_init()` `sensor_init()` `adc_channel_setup_dt()` `adc_read_dt()` `k_sleep()`。
     - NG: `if (ble_init() != 0)`
     - OK: `ret = ble_init();` → `if (ret != EXIT_SUCCESS)`
   - 厳密には、判定関数も、変数に入れてから判定するべきだが、変数が増えて、かえって読みにくくなるので、許容する。
   - 単体テスト (`app/*/tests/`) のコードは、この規約の対象外とする。
2. **条件式は bool だけにする (Rule 14.4)**: 整数やポインタを、そのまま条件式に書かず、比較して bool にする (`if (err != 0)`、`if (ptr != NULL)`)。bool の値 (`bool` 型の変数、`bool` を返す関数) は、そのまま書き、`== true` / `== false` とは比較しない (`if (ready)`、`if (!ready)`)。無限ループは `while (true)` (`<stdbool.h>`)。
   - NG: `if (err)`、`if (ptr)`、`if (!count)`、`if (ready == false)`
   - OK: `if (err != 0)`、`if (ptr != NULL)`、`if (count == 0)`、`if (!ready)`
3. **戻り値にリテラルを直書きしない**: `return 0;` / `return -1;` は使わず、`EXIT_SUCCESS` / `EXIT_FAILURE` (`<stdlib.h>`)、または `-ENODEV` 等の名前付き定数を使う。
4. **マジックナンバーを使わない**: 時間、解像度、範囲などは `#define` で名前を付ける。符号なしの値には、大文字の `U` サフィックスを付ける (`12U`、`0U`)。`long` / `ssize_t` には `L`、`long long` には `LL` を付ける (小文字の `l` は使わない)。
5. **三項演算子は、式全体を括弧で囲む**: `((x != NULL) ? x : y)` のように書く (`(x != NULL) ? x : y` と書かない)。条件は、2 と同じく、bool にする。
6. **ローカル変数は宣言時に必ず初期化する**: 例: `int err = EXIT_SUCCESS;`、`bool ready = false;`、`char buf[N] = {0};`。ただし、`static` の変数は、0 になるので、0 で初期化しない (checkpatch.pl の `INITIALISED_STATIC` が、エラーにする)。
7. **`extern` 宣言を `.c` に書かない**: 関数宣言はヘッダ (`ble.h`, `sensor.h` 等) に置き、利用側で `#include "xxx.h"` する。ヘッダにはインクルードガードを付ける。
8. **論理演算子の前後の比較に括弧を付ける (Rule 12.1)**: `&&` や `||` でつなぐ、それぞれの比較は、括弧で囲む。`if ((err == 0) && (ptr != NULL))` のように書き、`if (err == 0 && ptr != NULL)` とは書かない。

### Doxygen コメント

1. **関数の Doxygen コメントは、 `.c` の定義の直前に書く**: `/** ... */` 形式で `@brief`、`@param[in]`/`@param[out]`、`@retval` (または `@return`) を記述する。`@brief` の説明の終わりには、句点を付けない。ソースのコメントの句読点は、「, 」(カンマと空白) と「.」(ピリオド) にする (「、」と「。」は使わない)。読点 (「, 」) は、読みやすさのために、必要なところだけに打つ (「は」「が」「を」などの助詞の直後には、基本的に打たない。節の切れ目と、並列には打つ)。ヘッダ (`.h`) の関数宣言には、コメントを書かない (`.c` と重複させない。Doxygen は、定義のコメントを、宣言にも使う)。
2. **各ファイルの先頭に、`Copyright` と `SPDX-License-Identifier: GPL-3.0-or-later` (Zephyr の形式。`CODING_STYLE.md` を参照)、続けて `@file` と `@brief` を書く** (Zephyr と同じく、`@file` の後に、ファイル名は書かない)。
3. **`static` 関数、`#define`、グローバル/静的変数、構造体のメンバー、単体テストのテストケース (`ZTEST`) と補助関数にも `/** ... */` で説明を付ける**。`docs-thermo` が、説明のないものを警告にする (`Doxyfile` の `PREDEFINED` で、`ZTEST` と `K_*_DEFINE` を、関数や変数として扱う。FFF のモック (`FAKE_*`) は、対象外。モックの宣言の前に、1 行の注釈を付ける)。
4. コメントは日本語で記述する。
5. **全ての引数と戻り値を書く**: 引数がある関数には `@param[in]` / `@param[out]`、値を返す関数には `@retval` (または `@return`) を書く。値を返さない (`void`) 関数には `@return なし` を書かない。
6. **ドキュメントは、警告ゼロにする**: コメントを書いたら、`docker compose run --rm docs-thermo` で、警告がないことを確認する (ドキュメントのない関数・引数・変数が 1 つでもあれば失敗する)。Markdown で、日本語の直後に、`.` か `(` で始まるコードを続けると、Doxygen が誤るので、空白を 1 つ入れる。
