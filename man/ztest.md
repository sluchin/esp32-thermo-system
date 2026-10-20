# Ztest (単体テスト)

単体テストの書き方に使っている Ztest (Zephyr のテストフレームワーク) の、使っているマクロ。ヘッダは `zephyr/ztest.h` (Zephyr v4.3.0 の `subsys/testsuite/ztest/include/zephyr/ztest_test.h` と `ztest_assert.h`)。`app/*/tests/` の 31 のテストスイートが、全て使っている。

出典: Zephyr v4.3.0 のヘッダの Doxygen コメントを訳して、このプロジェクトでの使い方に絞った。**英語の原文が正**なので、細かい挙動で迷ったら、ヘッダを見る。

Ztest は、テストのスイート (関連するテストの集まり) を登録して、各テストの前後に準備と後始末を呼び、アサーションが失敗したテストを失敗とする。`native_sim` で実行し、twister が結果を集める (`testcase.yaml`)。モックは、FFF を使う ([fff.md](fff.md))。

## テストの構成

1 つのテストファイルは、次の形になる。

```c
#include <zephyr/ztest.h>

static void before(void *fixture) { /* 各テストの前の準備 */ }

ZTEST_SUITE(ntp, NULL, NULL, before, NULL, NULL);

ZTEST(ntp, test_a_resolve_failure)
{
    zassert_equal(ntp_init(), -EHOSTUNREACH);
}
```

スイートは、ファイルの途中に 1 つ定義すれば、あとは `ZTEST` で、そのスイートにテストを足す。テストは、スイートの中で、定義した順に実行される (順序に頼るテストは、名前の頭に `test_a_` `test_b_` を付けて、順を明示している)。

## 定義するマクロ

### ZTEST_SUITE

```c
ZTEST_SUITE(SUITE_NAME, PREDICATE, setup_fn, before_fn, after_fn, teardown_fn);
```

テストのスイートを作って、登録する。

| 引数 | 入出力 | 内容 |
|:---|:---|:---|
| `SUITE_NAME` | in | スイートの名前 (識別子。文字列にしない) |
| `PREDICATE` | in | スイートを実行するかを、状態から決める関数。使わないときは `NULL` |
| `setup_fn` | in | スイートの実行の前に 1 回呼ぶ関数 (`void *(*)(void)`)。戻り値が「フィクスチャ」で、`before_fn` などの引数になる。使わないときは `NULL` |
| `before_fn` | in | 各テストの前に呼ぶ関数 (`void (*)(void *fixture)`)。使わないときは `NULL` |
| `after_fn` | in | 各テストのあとに呼ぶ関数 (`void (*)(void *fixture)`)。使わないときは `NULL` |
| `teardown_fn` | in | スイートの実行のあとに 1 回呼ぶ関数 (`void (*)(void *fixture)`)。使わないときは `NULL` |

注意:

- このプロジェクトは、`PREDICATE` と `teardown_fn` を、使っていない (全て `NULL`)。
- 各テストの初期化 (FFF のフェイクのリセットなど) は、`before_fn` に書く。`setup_fn` は、スイートの間で状態を引き継ぐテスト (`sdlog` の RAM ディスクのマウントなど) だけが使う。
- `main()` は、書かない。Ztest が提供する。

使用箇所: [`app/thermo-gateway/tests/main/src/test_main.c:318`](../app/thermo-gateway/tests/main/src/test_main.c#L318) (`app/*/tests/` の全体で、31 か所。`setup_fn` を使うのは [`app/thermo-node/tests/sdlog/src/test_sdlog.c:296`](../app/thermo-node/tests/sdlog/src/test_sdlog.c#L296) など 3 か所)

### ZTEST

```c
ZTEST(suite, fn)
{
    /* テストの本体 */
}
```

テストを作って、スイート `suite` に足す。`fn` がテストの名前で、本体は、引数のない関数と同じに書く。スイートは、同じファイルで定義していなくてもよい。

| 引数 | 入出力 | 内容 |
|:---|:---|:---|
| `suite` | in | 足す先のスイートの名前 (`ZTEST_SUITE` の `SUITE_NAME`) |
| `fn` | in | テストの名前。このプロジェクトでは、`test_<対象の関数名>_<条件>` |

使用箇所: [`app/thermo-gateway/tests/ble/src/test_ble.c:433`](../app/thermo-gateway/tests/ble/src/test_ble.c#L433) (`app/*/tests/` の全体で、518 か所)

## アサーション

どれも、条件が成り立たなければ、テストを失敗にして、そのテストを止める。最後の `...` には、失敗のときに表示する、メッセージ (`printf` の書式) と引数を書ける (省略できる)。

| マクロ | 成り立つべき条件 | 使用箇所 (最初) | 数 |
|:---|:---|:---|---:|
| `zassert_equal(a, b, ...)` | `a == b` (型は変換せず、そのまま比べる) | [`test_main.c:171`](../app/thermo-gateway/tests/main/src/test_main.c#L171) | 1573 |
| `zassert_not_equal(a, b, ...)` | `a != b` | [`test_cloud.c:410`](../app/common/tests/cloud/src/test_cloud.c#L410) | 5 |
| `zassert_true(cond, ...)` | `cond` が真 | [`test_ble.c:475`](../app/thermo-gateway/tests/ble/src/test_ble.c#L475) | 146 |
| `zassert_false(cond, ...)` | `cond` が偽 | [`test_switchbot.c:155`](../app/thermo-gateway/tests/switchbot/src/test_switchbot.c#L155) | 95 |
| `zassert_is_null(ptr, ...)` | `ptr` が `NULL` | [`test_ble.c:436`](../app/thermo-gateway/tests/ble/src/test_ble.c#L436) | 9 |
| `zassert_not_null(ptr, ...)` | `ptr` が `NULL` でない | [`test_main.c:218`](../app/thermo-gateway/tests/main/src/test_main.c#L218) | 78 |
| `zassert_str_equal(s1, s2, ...)` | `strcmp(s1, s2) == 0` | [`test_cloud.c:446`](../app/common/tests/cloud/src/test_cloud.c#L446) | 85 |
| `zassert_mem_equal(buf, exp, size, ...)` | `memcmp(buf, exp, size) == 0` | [`test_ble.c:874`](../app/thermo-gateway/tests/ble/src/test_ble.c#L874) | 11 |
| `zassert_within(a, b, d, ...)` | `b - d <= a <= b + d` | [`test_ntp.c:152`](../app/common/tests/ntp/src/test_ntp.c#L152) | 4 |

(使用箇所は、全て `app/` の下のテスト。数は、`app/` 全体の呼び出しの数。)

引数:

| 引数 | 入出力 | 内容 |
|:---|:---|:---|
| `a` `b` | in | 比べる値 |
| `cond` | in | 調べる条件 |
| `ptr` | in | 調べるポインタ |
| `s1` `s2` | in | 比べる文字列 (終端の `\0` がある) |
| `buf` | in | 調べるバッファ |
| `exp` | in | 期待する内容のバッファ |
| `size` | in | 比べるバイト数 |
| `d` | in | 許す差 (`zassert_within`) |

注意:

- `zassert_equal` は、`a` と `b` を変換せずに比べるので、符号付きと符号なしを混ぜると、警告になることがある。型をそろえて書く (`zassert_equal(x, 5U)`)。
- `zassert_mem_equal` の引数は、マクロを 2 回展開してから、評価する。
- `zassert_str_equal` は、`strcmp()` なので、`NULL` を渡すと落ちる。先に `zassert_not_null` で確かめる。
- コーディング規約の「条件式は bool だけ」などは、単体テストには、適用しない (`CODING_STYLE.md`)。

## 使っていない機能

このプロジェクトのテストは、次の機能を使っていない。

- 失敗しても続ける `zexpect_*` と、前提が成り立たないとき、テストを飛ばす `zassume_*`。
- `zassert_ok` `zassert_not_ok` `zassert_equal_ptr` `zassert_between_inclusive` `zassert_unreachable`。
- 構造体のフィクスチャを使う `ZTEST_F` `ZTEST_SUITE` の `PREDICATE`。
- ユーザースレッドで実行する `ZTEST_USER`。
- 期待するエラーを検査する `ztest_set_fault_valid` `ztest_test_expect_fail`。
- Ztest のモック (`ztest_mock.h`。モックは FFF を使う)。
