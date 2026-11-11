# 設定 (Settings)

設定の保存 (`zephyr/settings/settings.h`) の、使っている API。WiFi の SSID、パスワード、AWS IoT Core のエンドポイント、クライアント ID、証明書などを、不揮発の記憶に保存する (`app/common/cfg.c`)。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## 初期化と登録

### settings_subsys_init

```c
int settings_subsys_init(void);
```

設定の仕組みと、バックエンドを初期化する。アプリの起動のときに呼べる。

戻り値:

- 成功は 0。
- 失敗は、保存先 (バックエンド。NVS、FCB、ファイル) の初期化の、負の errno (`-EIO`、`-ENOMEM`、`-ENODEV` など)。

注意:

- バックエンドがファイルシステムのときは、ファイルシステムをマウントしたあとに呼ぶ。FCB (NVS など) のバックエンドには、この制限はない。

使用箇所: [`app/common/cfg.c:451`](../../app/common/cfg.c#L451)

### settings_register

```c
int settings_register(struct settings_handler *cf);
```

RAM にある設定項目のハンドラを、既定のコミット優先度で登録する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `cf` | in,out | 登録の情報を持つ構造体 |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。

エラー:

- `EEXIST`: 同じハンドラを、すでに登録している。

使用箇所: [`app/common/cfg.c:459`](../../app/common/cfg.c#L459)

## 読み書き

### settings_save_one

```c
int settings_save_one(const char *name, const void *value, size_t val_len);
```

1 つの値を、永続する記憶に書く (値が変わったときだけ)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `name` | in | 設定項目の名前 (キー) |
| `value` | in | 設定項目の値。`settings_handler::h_export` のハンドラに渡される |
| `val_len` | in | `value` の長さ |

戻り値:

- 成功は 0。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は、保存先の負の errno (`-EIO` など)。

エラー:

- `ENOENT`: 保存先が設定されていない。
- `EINVAL`: `name` が不正 (長すぎる、など)。
- `ENOSPC`: 保存先に空きがない。

使用箇所: [`app/common/cfg.c:156`](../../app/common/cfg.c#L156) (ほか 2 行)

### settings_delete

```c
int settings_delete(const char *name);
```

永続する記憶から、1 つの項目を消す。既存の項目を消すとは、その値を `NULL` にすること。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `name` | in | 設定項目の名前 (キー) |

戻り値:

- 成功は 0 (項目がなくても、成功)。
- 失敗のときは、「エラー」の番号の符号を変えた負の値 (`-EINVAL` など) を返す。
- ほかの失敗は、`settings_save_one(name, NULL, 0)` と同じ。

エラー:

- `ENOENT`: 保存先が設定されていない。

使用箇所: [`app/common/cfg.c:290`](../../app/common/cfg.c#L290) (ほか 3 行)

### settings_load_subtree

```c
int settings_load_subtree(const char *subtree);
```

登録済みの永続する記憶から、指定のサブツリーの項目だけを読み込む。先に登録したサブツリーのハンドラが、そのサブツリーの値が見つかるごとに呼ばれる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `subtree` | in | 読み込むサブツリーの名前 |

戻り値:

- 成功は 0。
- 失敗は、保存先の読み出しの負の errno (`-EIO` など)。ハンドラの `set` が返した負の値も、そのまま返る。

使用箇所: [`app/common/cfg.c:465`](../../app/common/cfg.c#L465)

## 構造体

メンバーを持つ構造体と、その説明。各 API の引数に、この構造体が出てくる。

### settings_handler

```c
struct settings_handler
```

設定のサブツリーを扱う関数の表。`settings_register()` で登録する。

| メンバー | 型 | 説明 |
|:---|:---|:---|
| `name` | `const char *` | サブツリーの名前 |
| `h_get` | `int (*)(const char *key, char *val, int val_len_max)` | キーに合う値を返す |
| `h_set` | `int (*)(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)` | 読み込んだ値を受け取る。`read_cb` で、値を読み出す |
| `h_commit` | `int (*)(void)` | すべて読み込んだあとに呼ばれる。設定を反映するために使う |
| `h_export` | `int (*)(int (*export_func)(...))` | `settings_save()` のとき、現在の設定をすべて書き出す |
| `cprio` | `int` | コミットの優先度。小さいほど先 |

注意:

- このリポジトリが設定するのは、`name` と `h_set` だけ。

使用箇所: [`app/common/cfg.c:445`](../../app/common/cfg.c#L445)
