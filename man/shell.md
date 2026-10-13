# シェル

シェル (`zephyr/shell/shell.h`) の、使っている API。ノードとゲートウェイのシェルに、設定のコマンド (`app/common/cfg_shell.c`) を足す。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

## 出力

### shell_print

```c
#define shell_print(_sh, _ft, ...)
```

シェルに、通常のメッセージを出力する。

- `_sh`: シェルのインスタンス。`_ft`: 書式文字列。`...`: 書式の引数。

使用箇所: `app/common/cfg_shell.c:68 (ほか 10 行)`, `app/thermo-node/src/wifi_shell.c:62 (ほか 1 行)`

### shell_warn

```c
#define shell_warn(_sh, _ft, ...)
```

シェルに、警告のメッセージを出力する。

- `_sh`: シェルのインスタンス。`_ft`: 書式文字列。`...`: 書式の引数。

使用箇所: `app/thermo-node/src/wifi_shell.c:95`

### shell_error

```c
#define shell_error(_sh, _ft, ...)
```

シェルに、エラーのメッセージを出力する。

- `_sh`: シェルのインスタンス。`_ft`: 書式文字列。`...`: 書式の引数。

使用箇所: `app/common/cfg_shell.c:59 (ほか 5 行)`, `app/thermo-node/src/wifi_shell.c:53 (ほか 2 行)`

## コマンドの定義

### SHELL_CMD_REGISTER

```c
#define SHELL_CMD_REGISTER(syntax, subcmd, help, handler)
```

ルートのコマンド (レベル 0) を定義して、登録する。

- `syntax`: コマンド名 (例: `thermo`)。`subcmd`: サブコマンドの配列へのポインタ。`help`: ヘルプの文字列。`handler`: ハンドラ。

使用箇所: `app/common/cfg_shell.c:247`

### SHELL_CMD_ARG

```c
#define SHELL_CMD_ARG(syntax, subcmd, help, handler, mand, opt)
```

引数つきのコマンドを初期化する。

- `mand`: 必須の引数の数 (コマンド名を含む)。`opt`: 省略できる引数の数。ほかは `SHELL_CMD_REGISTER()` と同じ。

使用箇所: `app/common/cfg_shell.c:230 (ほか 1 行)`

### SHELL_CMD

```c
#define SHELL_CMD(syntax, subcmd, help, handler)
```

引数の数を検査しないコマンドを初期化する (`SHELL_CMD_ARG(..., 0, 0)` と同じ)。

使用箇所: `app/common/cfg_shell.c:234 (ほか 4 行)`, `app/thermo-node/src/shell_extra.h:29 (ほか 2 行)`

### SHELL_STATIC_SUBCMD_SET_CREATE

```c
#define SHELL_STATIC_SUBCMD_SET_CREATE(name, ...)
```

静的なサブコマンドの集まりを作る。

- `name`: 集まりの名前。`...`: `SHELL_CMD_ARG()` などで作ったコマンドの並び。最後に `SHELL_SUBCMD_SET_END` を付ける。

使用箇所: `app/common/cfg_shell.c:228`
