# wifi_time.c の解析

対象: `app/thermo-node/src/wifi_time.c` (154 行) と `wifi_time.h` (30 行)

ゲートウェイから BLE で時刻が届かないときの、**予備の時刻の取得**です。WiFi に接続して、SNTP で UTC の UNIX 時刻を 1 回だけ取り、すぐに WiFi を切ります。取った時刻を RTC に設定するのは、呼び出し側です (`main.c` の `check_time()` と、シェルの `thermo sync`)。`CONFIG_THERMO_WIFI_TIME` が有効なときだけ、ビルドされます。

WiFi の接続は、`app/common/wifi_link.c`、設定 (SSID、パスワード) は、`app/common/cfg.c` が受け持ちます。このファイルは、それらを順に呼ぶ、薄い層です。

## 公開する関数と定数

| 項目 | 内容 |
|:---|:---|
| `wifi_time_init()` | 設定を読み込んで (`cfg_init()`)、WiFi の準備をする (`wifi_link_init()`) |
| `wifi_time_fetch(&unix_s)` | WiFi に接続して、SNTP で時刻を取り、WiFi を切る |
| `WIFI_TIME_CONNECT_S` | WiFi の接続を待つ時間 20 秒 |
| `WIFI_TIME_SNTP_S` | SNTP の応答を待つ時間 5 秒 |

## wifi_time_fetch

```text
SSID が空 (cfg_get(CFG_KEY_SSID)[0] == '\0')  → -ENOENT (thermo set ssid の案内をログに出す)
wifi_link_connect(SSID, パスワード, 20 秒)
    成功 → query_sntp(CONFIG_THERMO_NTP_SERVER, 5 秒, &unix_s)
    失敗 → "Failed to connect to WiFi" をログに出す
wifi_link_disconnect()                        ← 成功しても失敗しても、必ず切る
```

接続の途中で失敗したときも、`wifi_link_disconnect()` を呼びます。つなぎっぱなしで、電力と、ほかの用途 (直接の送信) を、塞がないためです。

### 戻り値

| 値 | 意味 |
|:---|:---|
| `EXIT_SUCCESS` | 成功 |
| `-ENOENT` | SSID が設定されていない |
| `-ETIMEDOUT` | WiFi の接続か、SNTP の応答が、時間内に済まなかった |
| `-ECONNREFUSED` | WiFi の接続に失敗した (SSID やパスワードの誤りなど) |
| `-EHOSTUNREACH` | NTP サーバの名前を解決できなかった |
| そのほかの負の値 | 失敗 (負の errno) |

## query_sntp と resolve_server

1. `resolve_server()`: `zsock_getaddrinfo()` で、サーバ名を、IPv4 (`AF_INET`) の UDP のアドレスにします (ポート 123)。失敗したら `-EHOSTUNREACH`。
2. `sntp_init()` でコンテキストを作り、`sntp_query()` で問い合わせます。待つ時間は、`k_timeout_t` をミリ秒にして渡します。
3. `sntp_close()` で閉じます (成功しても、失敗しても)。
4. 応答の秒数 (`ts.seconds`) が、UNIX 時刻 (UTC) です。

**システムの時計は合わせません。** ゲートウェイの `common/ntp.c` は、システムの時計を合わせて、後から参照しますが、ノードは、1 回取って RTC に書くだけなので、時計を持ちません。

## 時間の上限

接続に最大 20 秒、SNTP の応答に最大 5 秒で、**約 25 秒**です。サーバ名の解決の時間は、これに含まれません (`zsock_getaddrinfo()` の待ち時間は、ネットワークの設定による)。この間、呼び出したスレッド (メインスレッド) は、戻りません。

## 気づいた点

- `main.c` は、この関数をメインループの中で呼ぶので、その間は、測定、ボタン、OLED が止まります。時刻がないときの、1 分に 1 回、最大 5 回だけの動作です。
- `Kconfig` の `THERMO_NTP_RESYNC_SEC` は、このファイルでは使いません (`common/ntp.c` で、WiFi での直接の送信のために、使われます)。
- 時刻の精度は、SNTP の往復の遅れ (数十ミリ秒) 程度です。RTC には、秒単位で設定するので、問題になりません。

## 関連

- [main.md](main.md): `check_time()` が呼ぶ
- [wifi_shell.md](wifi_shell.md): `thermo sync` が呼ぶ
- [node_time.md](node_time.md): 取った時刻の RTC への設定
- [index.md](index.md): このディレクトリの文書の一覧
