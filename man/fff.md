# FFF (モック)

単体テストのモックに使っている FFF (Fake Function Framework) の、使っているマクロとメンバー。ヘッダは `zephyr/fff.h` (Zephyr v4.3.0 の `subsys/testsuite/include/zephyr/fff.h`)。`app/*/tests/` の 21 のテストが、全て使っている。

出典: Zephyr v4.3.0 に入っている `fff.h` (Michael Long 作。MIT ライセンス) のマクロの定義を読んで、このプロジェクトでの使い方に絞って書いた。ヘッダには Doxygen コメントがないので、他の `man/` のファイルと違って、翻訳ではなく、マクロの動作の説明になっている。英語の原文が正なので、細かい挙動で迷ったら、ヘッダを見る。

FFF は、関数の代わりになる偽の関数 (フェイク) を作る。フェイクは、呼ばれた回数と引数を記録して、決めておいた値を返す。実物の関数が、ハードウェアやネットワークに頼るときに、テストでその部分を置き換えるために使う。

## 仕組み

`FAKE_VALUE_FUNC(int, bt_enable, bt_ready_cb_t)` と書くと、次の 3 つができる。

- 偽の関数 `bt_enable()`。実物の代わりに、テストにリンクされる。
- 構造体の変数 `bt_enable_fake`。呼び出しの記録と、返す値の設定が入っている (「フェイクのメンバー」)。
- リセットの関数 `bt_enable_reset()`。`RESET_FAKE(bt_enable)` が呼ぶ。

`bt_enable_fake` の宣言は、リポジトリのどこにも書かれていない。マクロが作る。名前は、関数名に `_fake` を付けた形。

## 定義するマクロ

### DEFINE_FFF_GLOBALS

```c
DEFINE_FFF_GLOBALS
```

FFF が全てのフェイクで共有する変数 `fff` (呼び出しの順序の記録) を定義する。末尾に `;` は付けない。テストのアプリ (1 つの実行ファイル) につき、1 回だけ、ファイルの先頭に書く。

使用箇所: [`app/thermo-gateway/tests/ble/src/test_ble.c:35`](../app/thermo-gateway/tests/ble/src/test_ble.c#L35) (ほか 20 のテスト)

### FAKE_VALUE_FUNC

```c
FAKE_VALUE_FUNC(RETURN_TYPE, FUNCNAME, ARG0_TYPE, ARG1_TYPE, ...)
```

値を返す関数のフェイクを作る。引数の型を、順に並べる。引数のない関数は、型を書かない (`FAKE_VALUE_FUNC(int, bt_le_scan_stop)`)。引数の数は、マクロが数える (最大 20 個。`FFF_MAX_ARGS`)。

| 引数 | 入出力 | 内容 |
|:---|:---|:---|
| `RETURN_TYPE` | in | 戻り値の型 |
| `FUNCNAME` | in | 偽の関数の名前。実物の関数と同じにする |
| `ARG0_TYPE` ... | in | 引数の型 (順に) |

注意:

- 実物の関数がヘッダで宣言されているときは、フェイクが同じ型で、もう一度宣言する。そのため、テストの `CMakeLists.txt` で `-Wno-redundant-decls` を付けている。
- 実物の関数は、リンクしない (テストの `CMakeLists.txt` に、そのソースを入れない)。

使用箇所: [`app/thermo-gateway/tests/ble/src/test_ble.c:72`](../app/thermo-gateway/tests/ble/src/test_ble.c#L72) (`app/*/tests/` の全体で、194 か所)

### FAKE_VOID_FUNC

```c
FAKE_VOID_FUNC(FUNCNAME, ARG0_TYPE, ARG1_TYPE, ...)
```

戻り値のない関数のフェイクを作る。引数は `FAKE_VALUE_FUNC` と同じ。フェイクのメンバーのうち、戻り値に関するもの (`return_val` など) は、ない。

使用箇所: [`app/thermo-gateway/tests/ble/src/test_ble.c:81`](../app/thermo-gateway/tests/ble/src/test_ble.c#L81) (`app/*/tests/` の全体で、30 か所)

## フェイクのメンバー

`FUNCNAME_fake` (`FUNCNAME` は偽の関数の名前) の、メンバー。

| メンバー | 型 | 内容 |
|:---|:---|:---|
| `call_count` | `unsigned int` | 呼ばれた回数 |
| `arg0_val`, `arg1_val` ... | 引数の型 | 最後に呼ばれたときの、引数の値 |
| `arg0_history[]`, `arg1_history[]` ... | 引数の型の配列 | 呼ばれた順の、引数の値。`arg0_history[0]` が 1 回目。長さは 50 (`FFF_ARG_HISTORY_LEN`)。それより多く呼ばれた分は、記録されず、`arg_histories_dropped` が増える |
| `arg_history_len` | `unsigned int` | 引数の履歴の長さ (50) |
| `arg_histories_dropped` | `unsigned int` | 履歴に入らなかった呼び出しの回数 |
| `return_val` | 戻り値の型 | 返す値 (値を返すフェイクだけ) |
| `return_val_seq`, `return_val_seq_len` | 配列、`int` | 返す値の列 (`SET_RETURN_SEQ` が設定する) |
| `return_val_history[]` | 戻り値の型の配列 | 返した値の履歴 (値を返すフェイクだけ) |
| `custom_fake` | 関数ポインタ | 実物の代わりに呼ぶ関数。戻り値と引数は、偽の関数と同じ |
| `custom_fake_seq`, `custom_fake_seq_len` | 関数ポインタの配列、`int` | `custom_fake` の列 (`SET_CUSTOM_FAKE_SEQ` が設定する) |

### 呼ばれたときの動作

偽の関数が呼ばれると、次の順に動く。

1. 引数を、`argN_val` に保存する。履歴に空きがあれば、`argN_history` にも保存する。
2. `call_count` を 1 増やす。呼び出しの順序 (`fff.call_history`) に、この関数を加える。
3. 返す値 (値を返すフェイク) を、次の優先順位で決める。上にあるほうが優先される。
   1. `custom_fake_seq` (設定されているとき)。順に 1 つずつ呼んで、最後まで来たら、最後の関数を呼び続ける。
   2. `custom_fake` (`NULL` でないとき)。その戻り値を返す。
   3. `return_val_seq` (設定されているとき)。順に 1 つずつ返して、最後まで来たら、最後の値を返し続ける。
   4. `return_val`。
4. 戻り値のないフェイクは、`custom_fake_seq` と `custom_fake` の両方が設定されていれば、両方を呼ぶ。

何も設定しないと、値を返すフェイクは 0 を返す (変数が 0 で初期化されているため)。

### call_count

呼ばれた回数。呼ばれなかったことの確認 (`0U`) にも使う。

使用箇所: [`app/common/tests/shell/src/test_shell.c:195`](../app/common/tests/shell/src/test_shell.c#L195) (ほか、約 700 か所)

### return_val

偽の関数が返す値。失敗の経路を試すときに、`-EIO` などを入れる。

使用箇所: [`app/common/tests/ntp/src/test_ntp.c:117`](../app/common/tests/ntp/src/test_ntp.c#L117) (ほか、約 270 か所)

### arg0_val

最後の呼び出しの、最初の引数。ポインタ (文字列など) も、値のまま、ポインタが入る。

使用箇所: [`app/common/tests/cloud/src/test_cloud.c:447`](../app/common/tests/cloud/src/test_cloud.c#L447) (ほか、`arg1_val` などを含めて、約 140 か所)

### arg0_history

呼ばれた順の、最初の引数の履歴。同じ関数が何度も呼ばれて、途中の引数を確認したいときに使う。

使用箇所: [`app/common/tests/wifi/src/test_wifi.c:137`](../app/common/tests/wifi/src/test_wifi.c#L137)、[`app/thermo-node/tests/oled/src/test_oled.c:434`](../app/thermo-node/tests/oled/src/test_oled.c#L434) (`arg2_history`)

### custom_fake

```c
bt_enable_fake.custom_fake = fake_bt_enable;
```

実物の代わりに呼ぶ関数を設定する。出力の引数 (`out`) に値を書き込む関数や、呼ばれた回数によって返す値を変える関数を、テストで書いて入れる。関数の型は、偽の関数と同じ。

使用箇所: [`app/common/tests/cloud/src/test_cloud.c:343`](../app/common/tests/cloud/src/test_cloud.c#L343) (関数は 158 行目。ほか、約 180 か所)

## リセットと列の設定

### RESET_FAKE

```c
RESET_FAKE(FUNCNAME);
```

フェイクのメンバーを全て 0 (`custom_fake` と `custom_fake_seq` は `NULL`) に戻す。前のテストの設定と記録が残らないように、テストの前 (`before` の関数) で、使うフェイクごとに呼ぶ。`fff` の呼び出しの順序は、戻さない (`FFF_RESET_HISTORY` で戻す)。

使用箇所: [`app/common/tests/cfg/src/test_cfg.c:263`](../app/common/tests/cfg/src/test_cfg.c#L263) (ほか、約 250 か所)

### FFF_RESET_HISTORY

```c
FFF_RESET_HISTORY();
```

呼び出しの順序 (`fff.call_history` と `fff.call_history_idx`) を空にする。

使用箇所: [`app/common/tests/ntp/src/test_ntp.c:102`](../app/common/tests/ntp/src/test_ntp.c#L102) (ほか、20 か所)

### SET_RETURN_SEQ

```c
SET_RETURN_SEQ(FUNCNAME, ARRAY_POINTER, ARRAY_LEN);
```

呼ばれるたびに、配列の値を順に返すようにする。`return_val_seq` と `return_val_seq_len` に、配列とその長さを入れる。配列の最後まで来たら、最後の値を返し続ける。配列は、テストが終わるまで、有効でなければならない (ローカル変数で足りる)。`RESET_FAKE` のあとに設定する (リセットが、列を消すため)。

| 引数 | 入出力 | 内容 |
|:---|:---|:---|
| `FUNCNAME` | in | 偽の関数の名前 |
| `ARRAY_POINTER` | in | 返す値の配列 |
| `ARRAY_LEN` | in | 配列の要素の数 (`ARRAY_SIZE()`) |

使用箇所: [`app/common/tests/cfg/src/test_cfg.c:515`](../app/common/tests/cfg/src/test_cfg.c#L515) (ほか、14 か所)

### SET_CUSTOM_FAKE_SEQ

```c
SET_CUSTOM_FAKE_SEQ(FUNCNAME, ARRAY_POINTER, ARRAY_LEN);
```

`SET_RETURN_SEQ` の、`custom_fake` 版。呼ばれるたびに、関数の配列の関数を順に呼ぶ。このプロジェクトでは、使っていない。

## 呼び出しの順序

### fff.call_history

```c
extern fff_globals_t fff;   /* fff.call_history[], fff.call_history_idx */
```

どの関数が、どの順に呼ばれたかの記録。全てのフェイクで共通で、呼ばれるたびに、関数のアドレスが 1 つ加わる。長さは 50 (`FFF_CALL_HISTORY_LEN`)。`fff.call_history_idx` が、記録した数。

要素は関数ポインタなので、比べるときは、`(void *)(uintptr_t)(f)` に直す (関数ポインタをオブジェクトポインタに直接キャストすることは、ISO C で許されないため)。このプロジェクトでは、`FUNCTION_ADDRESS()` で包んでいる。

使用箇所: [`app/thermo-gateway/tests/main/src/test_main.c:222`](../app/thermo-gateway/tests/main/src/test_main.c#L222) (`ble_init` と `cloud_init` の呼ばれた順の確認)

## 使っていない機能

このプロジェクトのテストは、次の機能を使っていない。

- `SET_CUSTOM_FAKE_SEQ` と `custom_fake_seq`。
- `return_val_history`。
- 可変個引数の関数のフェイク (`FAKE_VALUE_FUNC_VARARG` と `FAKE_VOID_FUNC_VARARG`)。
- 履歴の長さを変えるマクロ (`FFF_ARG_HISTORY_LEN` と `FFF_CALL_HISTORY_LEN`。既定は、どちらも 50)。
