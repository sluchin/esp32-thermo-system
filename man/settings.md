# 設定 (Settings)

設定の保存 (`zephyr/settings/settings.h`) の、使っている API。WiFi の SSID、パスワード、AWS IoT Core のエンドポイント、クライアント ID、証明書などを、不揮発の記憶に保存する (`app/common/cfg.c`)。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

## 初期化と登録

### settings_subsys_init

```c
int settings_subsys_init(void);
```

設定の仕組みと、バックエンドを初期化する。アプリの起動のときに呼べる。

- バックエンドがファイルシステムのときは、ファイルシステムをマウントしたあとに呼ぶ。FCB (NVS など) のバックエンドには、この制限はない。
- 戻り値: 成功は 0、失敗は 0 以外。

使用箇所: `app/common/cfg.c:437`

### settings_register

```c
int settings_register(struct settings_handler *cf);
```

RAM にある設定項目のハンドラを、既定のコミット優先度で登録する。

- `cf`: 登録の情報を持つ構造体。
- 戻り値: 成功は 0、失敗は 0 以外。

使用箇所: `app/common/cfg.c:445`

## 読み書き

### settings_save_one

```c
int settings_save_one(const char *name, const void *value, size_t val_len);
```

1 つの値を、永続する記憶に書く (値が変わったときだけ)。

- `name`: 設定項目の名前 (キー)。`value` / `val_len`: 値と、その長さ。値は、`settings_handler::h_export` のハンドラに渡される。
- 戻り値: 成功は 0、失敗は 0 以外。

使用箇所: `app/common/cfg.c:155` (ほか 2 行)

### settings_delete

```c
int settings_delete(const char *name);
```

永続する記憶から、1 つの項目を消す。既存の項目を消すとは、その値を `NULL` にすること。

- `name`: 設定項目の名前 (キー)。
- 戻り値: 成功は 0、失敗は 0 以外。

使用箇所: `app/common/cfg.c:288` (ほか 3 行)

### settings_load_subtree

```c
int settings_load_subtree(const char *subtree);
```

登録済みの永続する記憶から、指定のサブツリーの項目だけを読み込む。先に登録したサブツリーのハンドラが、そのサブツリーの値が見つかるごとに呼ばれる。

- `subtree`: 読み込むサブツリーの名前。
- 戻り値: 成功は 0、失敗は 0 以外。

使用箇所: `app/common/cfg.c:451`
