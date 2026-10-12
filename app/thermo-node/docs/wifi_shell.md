# wifi_shell.c の解析

対象: `app/thermo-node/src/wifi_shell.c` (100 行) と `shell_extra.h` (38 行)

ノードのシェルの `thermo` コマンドに、ノード固有の 2 つのサブコマンドを足します。

| コマンド | 役割 | 条件 |
|:---|:---|:---|
| `thermo sync` | WiFi と SNTP で時刻を取って、RTC に設定する | `CONFIG_THERMO_WIFI_TIME` |
| `thermo sd` | SD カードが使えるかを確かめる (使えなければ、マウントし直す) | `CONFIG_THERMO_DIRECT_MQTT` |

設定のコマンド (`thermo set`、`show`、`reset`、`tether`、`restore`、`cred`、`apply` など) は、ゲートウェイと共通で、`app/common/cfg_shell.c` にあります。

## サブコマンドを足す仕組み

```text
common/cfg_shell.c
    #if __has_include("shell_extra.h")        ← ノードにだけある
    #include "shell_extra.h"
    ...
    SHELL_STATIC_SUBCMD_SET_CREATE(thermo_cmds, set, tether, restore, show, ..., reset,
                                   THERMO_SHELL_EXTRA  SHELL_SUBCMD_SET_END);
```

`shell_extra.h` が、`THERMO_SHELL_EXTRA` というマクロ (サブコマンドの定義で、末尾にコンマを含む) と、その関数の宣言を持ちます。ゲートウェイには、このヘッダがないので、`THERMO_SHELL_EXTRA` は、空になります。同じ `cfg_shell.c` を、2 つのアプリが共有しながら、ノードにだけコマンドを足せます。

## thermo sync (node_cmd_sync)

```text
wifi_time_fetch(&unix_s)       失敗 → "Failed to get the time over WiFi (err N)" を出して、エラーを返す
node_time_set(unix_s)          失敗 → "Failed to set the RTC (err N)"
成功 → "time set (UNIX time N)"
```

すぐに実行します。WiFi の接続と SNTP で、最大約 25 秒かかります。その間、シェルのスレッドは、戻りません。メインループは、別のスレッドなので、止まりません。ただし、`main.c` が、1 分ごとに行う、WiFi での時刻の取得と重ねないよう、気を付けてください (どちらも、WiFi を使います)。

## thermo sd (node_cmd_sd)

```text
direct_sd_check(&pending)     失敗 → "SD card is not available (err N, see the log above)"
成功 → "SD card is ready (N not sent)"
direct_init_status() が 0 でなければ → 警告 "Saving is off: direct_init() failed at the start (err N)"
```

- SD カードをマウントできなかったときは、`backlog_init()` が、理由 (`Could not mount the SD card (err ...)`) をログに出します。その直後に、エラーの文が出ます。
- すでにマウントできているときは、マウントし直しません。測定値の保存の途中に、外さないためです。
- `Saving is off` は、SD カードが使えても、測定値を保存していない状態を知らせます。起動のときの `direct_init()` が失敗した (BLE のアドレスが取れない、クラウドのスレッドを作れない、など) と、`direct_sample()` は何もしません。このコマンドは、その原因となった `direct_init()` の戻り値を表示します。

## 関連

- [direct.md](direct.md): `direct_sd_check()`、`direct_init_status()`
- [wifi_time.md](wifi_time.md): `wifi_time_fetch()`
- [node_time.md](node_time.md): `node_time_set()`
- [../../../SD_LOG.md](../../../SD_LOG.md): `thermo sd` を使った SD カードの確認
- [index.md](index.md): このディレクトリの文書の一覧
