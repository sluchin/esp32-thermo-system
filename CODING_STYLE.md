# コーディングスタイル

このプロジェクトの C ソース (`app/` 配下。実装と単体テスト) は、[Zephyr のコーディング規約](https://docs.zephyrproject.org/latest/contribute/style/code.html) に合わせます。Zephyr と違う点 (例外) は、このドキュメントに、理由とともに記録します。

MISRA-C への対応方針と、あえて従わない規則は、[MISRA.md](MISRA.md) に書いてあります。日常の規約 (命名、ログ、戻り値、Doxygen など) は、[.CLAUDE.md](.CLAUDE.md) にあります。

## 1. 規約を守るための道具

Zephyr と同じ道具を、Zephyr のバージョン (v4.3.0) に合わせて、Docker イメージの中で使います。

| 道具 | 設定 | 内容 |
|:---|:---|:---|
| clang-format | [.clang-format](.clang-format) | 整形。Zephyr の `.clang-format` に、4 行だけ変えたもの (例外 1、例外 5) |
| EditorConfig | [.editorconfig](.editorconfig) | エディタの設定。Zephyr の `.editorconfig` に、C / C++ / Perl のインデントだけ変えたもの (例外 1) |
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

コードを修正したら、`format-thermo-fix` で整形してから、`lint-thermo` と `whitespace-thermo` を実行します。CI (GitHub Actions) の `lint` ジョブも、同じ確認を行います。

Zephyr のバージョンを上げたときは、Zephyr の `.clang-format` と `.editorconfig` を取り込み直して、例外 1 と例外 5 の変更だけを、もう一度行います。

## 2. Zephyr と同じにしているもの (例外ではない)

次は、Zephyr の規約のとおりです。

- **中括弧の位置**: 関数の定義は、次の行。制御構文は、同じ行 (Linux スタイル)。
- **1 行の長さ**: 100 桁まで。
- **ポインタの `*`**: 変数名に付ける (`char *p`)。
- **命名**: スネークケース (`snake_case`)。マクロと定数は、大文字。
- **`#include` の順序**: 整形で並べ替えない (`SortIncludes: Never`)。
- **Devicetree、Kconfig の設定ファイル**: Zephyr のとおり、タブ 8。
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
- **このプロジェクト**: 空白 4 つ (C のソースとヘッダ。実装と単体テスト)。
- **理由**: 空白 4 つの方が、浅く見えて、100 桁の中に、収まりやすいためです。ほかのプロジェクトのコードとも、そろいます。
- **設定**:
  - `.clang-format`: `IndentWidth: 4`、`UseTab: Never`、`TabWidth: 4` (この 3 行だけが、Zephyr と違います。折り返した行のインデントは、Zephyr のとおり 8 です)。
  - `.editorconfig`: C のソースとヘッダ (`*.c`、`*.h`)、C++ (`*.cpp`、`*.hpp`)、Perl (`*.pl`) を、`indent_style = space`、`indent_size = 4` にする。C++ と Perl のファイルは、今のリポジトリにはなく、C と同じ流儀にそろえてある。patch ファイル (`*.patch`、`*.diff`) の `trim_trailing_whitespace = false` は、Zephyr のとおり (patch の空行の文脈を、壊さないため)。
  - checkpatch.pl: インデントの検査は、タブが前提なので、`LEADING_SPACE`、`CODE_INDENT`、`SUSPECT_CODE_INDENT` の 3 種類だけ無視する (`scripts/lint.sh`)。インデントは、clang-format が確認する。
- **対象外**: Devicetree (`*.overlay`)、Kconfig、`prj.conf` は、Zephyr のとおりで、変えていません。

### 例外 2: コメントは、日本語

- **Zephyr**: コメントは、英語。
- **このプロジェクト**: ソースのコメント (Doxygen コメントを含む) は、日本語 (`.CLAUDE.md` の Doxygen コメントの規約)。
- **理由**: このプロジェクトの読み手が、日本語を使うためです。
- **注意**: ログのメッセージ (`LOG_INF` など) と、識別子は、英語です。

### 例外 3: コミットメッセージの形式

- **Zephyr**: `subsystem: summary` の形式で、`Signed-off-by:` を付ける。
- **このプロジェクト**: 英語で書き、本文は `- ` で始まる箇条書き。`Signed-off-by:` や、`Co-Authored-By:` などの帰属行は付けない (`.CLAUDE.md` のコミットガイドライン)。

### 例外 4: 関数の戻り値に、名前付き定数を使う

- **Zephyr**: 成功は `0`、失敗は負の `errno` 値 (`return 0;`、`return -EIO;`)。
- **このプロジェクト**: 成功は `EXIT_SUCCESS` (値は 0)、失敗は負の `errno` 値 (`-ENODEV` など)。`main()` の失敗は `EXIT_FAILURE`。
- **理由**: MISRA-C の方針で、戻り値にリテラルを直書きしないためです (`MISRA.md` を参照)。値の意味は、Zephyr と同じです。

### 例外 5: 1 文の本体には、`{}` を付けない

- **Zephyr**: `if` / `else` / `for` / `while` の本体は、1 文でも `{}` を付ける (`.clang-format` の `InsertBraces: true`)。
- **このプロジェクト**: 本体が 1 文のときは、`{}` を付けなくてよい (付けてもよい)。2 文以上のときは、付ける。
- **設定**: `.clang-format` の `InsertBraces: false` (整形のときに、`{}` を自動で付けない。すでにある `{}` も、自動では消さない)。
- **注意**: MISRA C:2012 Rule 15.6 の例外でもある (`MISRA.md` の例外 7)。

## 4. Zephyr より厳しくしているもの (参考)

Zephyr の規約にはないか、緩いものを、このプロジェクトでは、より厳しく決めています。詳細は、 `.CLAUDE.md` と `MISRA.md` にあります。

- 条件式は、bool だけにする (整数やポインタは、比較して bool にする。`if (!ptr)` と書かない)。
- 副作用のある関数は、戻り値を変数に入れてから判定する。
- 符号なし整数のリテラルには、小文字の `u` を付ける (`long` 系は `L`、`long long` は `LL`)。
- ローカル変数は、宣言時に必ず初期化する。`extern` 宣言を、 `.c` に書かない。
- 戻り値は、必ず確認する (無視するときは、 `(void)` を付ける)。
- 警告オプションを約 55 個付けて、警告ゼロにする。静的解析 (`-fanalyzer`) の指摘も、ゼロにする。
