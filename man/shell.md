# シェル

シェル (`zephyr/shell/shell.h`) の、使っている API。ノードとゲートウェイのシェルに、設定のコマンド (`app/common/cfg_shell.c`) を足す。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## 出力

### shell_print

```c
#define shell_print(_sh, _ft, ...)
```

シェルに、通常のメッセージを出力する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_sh` | in | シェルのインスタンス |
| `_ft` | in | 書式文字列 |
| `...` | in | 書式の引数 |

使用箇所: [`app/common/cfg_shell.c:68`](../app/common/cfg_shell.c#L68) (ほか 10 行), [`app/thermo-node/src/wifi_shell.c:62`](../app/thermo-node/src/wifi_shell.c#L62) (ほか 1 行)

### shell_warn

```c
#define shell_warn(_sh, _ft, ...)
```

シェルに、警告のメッセージを出力する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_sh` | in | シェルのインスタンス |
| `_ft` | in | 書式文字列 |
| `...` | in | 書式の引数 |

使用箇所: [`app/thermo-node/src/wifi_shell.c:95`](../app/thermo-node/src/wifi_shell.c#L95)

### shell_error

```c
#define shell_error(_sh, _ft, ...)
```

シェルに、エラーのメッセージを出力する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `_sh` | in | シェルのインスタンス |
| `_ft` | in | 書式文字列 |
| `...` | in | 書式の引数 |

使用箇所: [`app/common/cfg_shell.c:59`](../app/common/cfg_shell.c#L59) (ほか 5 行), [`app/thermo-node/src/wifi_shell.c:53`](../app/thermo-node/src/wifi_shell.c#L53) (ほか 2 行)

## コマンドの定義

### SHELL_CMD_REGISTER

```c
#define SHELL_CMD_REGISTER(syntax, subcmd, help, handler)
```

ルートのコマンド (レベル 0) を定義して、登録する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `syntax` | in | コマンド名 (例: `thermo`) |
| `subcmd` | in | サブコマンドの配列へのポインタ |
| `help` | in | ヘルプの文字列 |
| `handler` | in | ハンドラ |

使用箇所: [`app/common/cfg_shell.c:247`](../app/common/cfg_shell.c#L247)

### SHELL_CMD_ARG

```c
#define SHELL_CMD_ARG(syntax, subcmd, help, handler, mand, opt)
```

引数つきのコマンドを初期化する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `syntax` | in | コマンド名 |
| `subcmd` | in | サブコマンドの配列へのポインタ |
| `help` | in | ヘルプの文字列 |
| `handler` | in | ハンドラ |
| `mand` | in | 必須の引数の数 (コマンド名を含む) |
| `opt` | in | 省略できる引数の数 |

使用箇所: [`app/common/cfg_shell.c:230`](../app/common/cfg_shell.c#L230) (ほか 1 行)

### SHELL_CMD

```c
#define SHELL_CMD(syntax, subcmd, help, handler)
```

引数の数を検査しないコマンドを初期化する (`SHELL_CMD_ARG(..., 0, 0)` と同じ)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `syntax` | in | コマンド名 |
| `subcmd` | in | サブコマンドの配列へのポインタ |
| `help` | in | ヘルプの文字列 |
| `handler` | in | ハンドラ |

使用箇所: [`app/common/cfg_shell.c:234`](../app/common/cfg_shell.c#L234) (ほか 4 行), [`app/thermo-node/src/shell_extra.h:29`](../app/thermo-node/src/shell_extra.h#L29) (ほか 2 行)

### SHELL_STATIC_SUBCMD_SET_CREATE

```c
#define SHELL_STATIC_SUBCMD_SET_CREATE(name, ...)
```

静的なサブコマンドの集まりを作る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `name` | in | 集まりの名前 |
| `...` | in | `SHELL_CMD_ARG()` などで作ったコマンドの並び。最後に `SHELL_SUBCMD_SET_END` を付ける |

使用箇所: [`app/common/cfg_shell.c:228`](../app/common/cfg_shell.c#L228)
