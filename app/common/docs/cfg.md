# cfg.c の解析

対象: `app/common/cfg.c` (624 行) と `cfg.h` (88 行)

実行時の設定を、フラッシュ (Zephyr の settings、NVS) に保存して読み出します。ゲートウェイとノードが共通で使います。

| 種類 | 内容 | 保存の形 |
|:---|:---|:---|
| 設定の項目 (`enum cfg_key`) | `ssid`、`psk`、`endpoint`、`client_id` | 文字列 (NUL を含めて保存) |
| 証明書 (`enum cfg_cred`) | `ca`、`cert`、`key` | PEM のバイト列 |
| テザリングの退避 | 切り替える前の SSID とパスワード | `tether_bak` (1 つの項目) |

設定は、シェルの `thermo` コマンドで入力します ([cfg_shell.md](cfg_shell.md))。

## 最大の長さ

| 定数 | 値 [byte] | 対象 |
|:---|:---|:---|
| `CFG_SSID_MAX` | 32 | SSID |
| `CFG_PSK_MAX` | 64 | WiFi のパスワード |
| `CFG_ENDPOINT_MAX` | 128 | AWS IoT Core のエンドポイント |
| `CFG_CLIENT_ID_MAX` | 64 | MQTT のクライアント ID |
| `CFG_CERT_MAX` | 1536 | CA とクライアント証明書 (PEM、NUL を含む) |
| `CFG_KEY_PEM_MAX` | 1792 | 秘密鍵 (PEM。RSA 2048 ビットで約 1700) |
| `CFG_BACKUP_MAX` | 98 | テザリングの退避 (SSID + パスワード + NUL 2 つ) |

## 保存の名前

settings の名前は `thermo/<項目>` です (`thermo/ssid`、`thermo/ca`、`thermo/tether_bak` など)。

## 状態を持つデータ

| 変数 | 内容 |
|:---|:---|
| `values[4][129]` | 設定の値 (RAM のコピー)。どの項目の大きさも、最も長い項目に合わせてある |
| `ca_buf`、`cert_buf`、`key_buf` | フラッシュから読んだ証明書の保存先 |
| `save_buf` | 証明書を保存するときの一時的な領域 (1792 バイト) |
| `backup_ssid`、`backup_psk`、`is_backup_present` | テザリングの退避の RAM のコピー |

`tls_credential_add()` は、データを**コピーせずにポインタを保持**します。そのため、フラッシュから読んだ証明書は、登録している間残るように、`static` の配列に置きます。

## 関数

### cfg_init

```text
settings_subsys_init()
settings_register(&handler)       -EEXIST は許す (2 回目以降)
settings_load_subtree("thermo")   <- 保存された項目ごとに settings_set_cb() が呼ばれる
```

ゲートウェイでは `cloud_init()` が呼びます。ノードでは `wifi_time_init()` と `cloud_init()` の両方が呼ぶので、2 回呼ばれます。2 回目のハンドラの登録は `-EEXIST` になりますが、これを許して読み込み直します (コミット `fix(app): Accept the settings handler registered twice`)。許していなかったときは、ノードの起動のときに `direct_init()` が `-17` で失敗して、SD カードへの保存が止まっていました。

### settings_set_cb: 読み込み

保存された項目ごとに呼ばれます (名前は `thermo/` の下)。

| 名前 | 処理 |
|:---|:---|
| `ssid`、`psk`、`endpoint`、`client_id` | `load_value()`: 長さが 0 か、最大長 + 1 を超えたら、警告を出して無視する。それ以外は読み込む |
| `ca`、`cert`、`key` | `load_credential()`: 長さを確かめてバッファに読み、同じ種類を削除してから `tls_credential_add()` で登録する |
| `tether_bak` | `load_backup()`: 「SSID、NUL、パスワード、NUL」の並びを検証して、退避に入れる |
| そのほか | 警告を出して無視 |

読めない項目があってもほかの項目の読み込みを続けるように、常に 0 を返します。

`load_backup()` は、文字列の長さを `bounded_len()` で数えます (`memchr()` で、最大の長さまでの範囲に NUL を探す)。働きは `strnlen()` と同じです。`strnlen()` を使わないのは、native_sim の単体テストの C の規格の設定では宣言が見えず、`-Werror` でビルドが失敗するためです。

### cfg_set

```text
長さ > 最大長 -> -EINVAL
settings_save_one("thermo/<項目>", 値, 長さ + 1)    <- NUL を含めて保存
    成功したときだけ RAM の値を更新する
```

フラッシュに保存できなかったときは、RAM の値も変えません。

### cfg_has_cred と cfg_is_complete

- `cfg_has_cred()`: `tls_credential_get()` に 1 バイトの領域を渡します。登録されていれば、領域が足りないので `-EFBIG` が返ります。登録されていなければ `-ENOENT` が返ります。`-ENOENT` 以外なら、登録されていると判断します。
- `cfg_is_complete()`: SSID、エンドポイント、クライアント ID が空でなく、3 つの証明書が登録されていれば true を返します。オープンネットワークではパスワードが空なので、パスワードは見ません。`cloud.c` は、これが true のときに接続を始めます。

### cfg_save_credentials

`cred add` で TLS の認証情報に登録した 3 つの証明書を、順に取り出してフラッシュに保存します。次の起動からは、`cfg_init()` が自動で登録します。どれかを取り出せなければ、そこで失敗します。それより前に保存した証明書は、そのまま残ります。

### cfg_reset

4 つの項目、3 つの証明書、退避をフラッシュから削除して、RAM と TLS の認証情報からも消します。途中で失敗しても残りの削除を続けて、最初のエラーを返します。

### テザリング (cfg_tether、cfg_restore)

スマートフォンのテザリングに一時的に切り替えるための機能です。

```text
cfg_tether(ssid, psk)
    SSID が空, または長すぎる -> -EINVAL
    退避がまだなければ:
        今の SSID とパスワードを 1 つの項目 "tether_bak" に保存 (保存の途中で電源が切れても片方だけが残ることはない)
    cfg_set(SSID), cfg_set(PSK)

cfg_restore()
    退避がなければ -> -ENOENT
    cfg_set(SSID, 退避), cfg_set(PSK, 退避)
    退避を削除する             <- 戻せたあとで消す. 消すのに失敗してももう一度戻せる
```

続けて `cfg_tether()` を呼んでも退避は上書きされないので、元の設定は失われません。

## 注意点

- PSK と秘密鍵は、フラッシュに平文で保存されます (settings の NVS は暗号化していません)。機器を盗まれると取り出せます。
- `cfg_tether()` で、SSID の保存に成功してパスワードの保存に失敗すると、SSID だけが変わった状態になります (退避は残っているので、`cfg_restore()` で戻せます)。
- `cfg_save_credentials()` は、3 つが揃っていなくても、揃っているところまでを保存します。
- `cfg_get()` が返すのは、`values` の中のポインタです。排他制御はないので、`cfg_set()` が書き換えている途中に別のスレッドが読むと、混ざった値が見えます (`cloud` スレッドが接続の準備で読んでいるときに、シェルで設定を変えると起こり得ます)。
- `cfg_init()` は `settings_subsys_init()` を毎回呼びます。Zephyr では、何度呼んでも安全です。

## 関連

- [cfg_shell.md](cfg_shell.md): 設定のシェルのコマンド
- [cloud.md](cloud.md): `cfg_is_complete()`、`cfg_get()` の利用
- [index.md](index.md): このディレクトリの文書の一覧
