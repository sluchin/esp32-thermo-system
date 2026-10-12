# cfg_shell.c の解析

対象: `app/common/cfg_shell.c` (247 行)

シェルの `thermo` コマンドです。`cfg.c` の関数を呼んで、設定を入力、表示、保存します。ゲートウェイとノードが共通で使います。

## サブコマンド

| コマンド | 引数 | 役割 | 呼ぶ関数 |
|:---|:---|:---|:---|
| `thermo set` | 項目名 (ssid、psk、endpoint、client_id のどれか) と、値 | 設定の値をフラッシュに保存 | `cfg_set()` |
| `thermo tether` | `<ssid> [psk]` | テザリングに切り替える (元の設定を残す) | `cfg_tether()` |
| `thermo restore` | なし | テザリングの前に戻す | `cfg_restore()` |
| `thermo show` | なし | 設定の状態を表示 (パスワードは `********`) | `cfg_get()` など |
| `thermo save-certs` | なし | 登録した証明書をフラッシュに保存 | `cfg_save_credentials()` |
| `thermo apply` | なし | 設定を反映 (接続をやり直す) | `cloud_reconnect()` |
| `thermo reset` | なし | 設定と証明書を全て消す | `cfg_reset()` |
| `thermo sync`、`thermo sd` | なし | ノードだけ。`wifi_shell.c` が足す | |

`set` は引数が 3 つ (必須)、`tether` は 2 つ必須と 1 つ任意です。

## ノード固有のコマンドを足す仕組み

```c
#if __has_include("shell_extra.h")
#include "shell_extra.h"
#endif
#ifndef THERMO_SHELL_EXTRA
#define THERMO_SHELL_EXTRA
#endif
...
SHELL_STATIC_SUBCMD_SET_CREATE(thermo_cmds, ..., THERMO_SHELL_EXTRA SHELL_SUBCMD_SET_END);
```

ノードのビルドだけが、`shell_extra.h` を持ちます。`THERMO_SHELL_EXTRA` は、ノードでは `sync` (と `sd`) のコマンドの定義になり、ゲートウェイでは空になります。

## 設定の手順 (典型)

```text
thermo set ssid <SSID>
thermo set psk <パスワード>
thermo set endpoint <xxxx-ats.iot.<リージョン>.amazonaws.com>
thermo set client_id <名前>
cred add ...                (Zephyr の cred コマンドで ca、cert、key を、セキュリティタグ 1 で登録)
thermo save-certs
thermo show
thermo apply
```

`thermo show` は、`ready to connect: yes` になれば、接続の条件が揃っています。

## 補足

- `save-certs` の名前のハイフンは、clang-format が前後に空白を入れて別の名前にしてしまうので、そのコマンドの行だけ、整形を止めています。
- `thermo set` の値に空白を含めるときは、引用符で囲みます (Zephyr のシェルの規則)。
- `thermo show` は、パスワードを `********` (空なら `(empty)`) で隠します。ただし、`thermo set psk` の入力そのものは、シェルの履歴とログに残ります。

## 気づいた点

- `thermo apply` は `cloud_reconnect()` を呼ぶだけです。ノードで直接送信を使っていないとき (`cloud_init()` が呼ばれていないとき) は、スレッドがありません。
- 詳しい処理は `cfg.c` にあり、このファイルは、入出力だけです。

## 関連

- [cfg.md](cfg.md): 設定の保存
- [cloud.md](cloud.md): `cloud_reconnect()`
- [index.md](index.md): このディレクトリの文書の一覧
