# 共通のコードの解析

`app/common` のソースを、ファイルごとに解析した文書です。thermo-node と thermo-gateway が、共通で使うコードです。

| 文書 | ソース | 内容 |
|:---|:---|:---|
| [cloud.md](cloud.md) | `cloud.c` `cloud.h` | AWS IoT Core への MQTT (TLS) による送信。送信のスレッド、再接続、キュー |
| [cfg.md](cfg.md) | `cfg.c` `cfg.h` | 設定と証明書の、フラッシュへの保存と読み出し。テザリングの退避 |
| [cfg_shell.md](cfg_shell.md) | `cfg_shell.c` | シェルの `thermo` コマンド |
| [wifi_link.md](wifi_link.md) | `wifi_link.c` | WiFi の接続 |
| [ntp.md](ntp.md) | `ntp.c` | SNTP によるシステム時計の同期 |
| [payload.md](payload.md) | `payload.c` | MQTT のトピックと JSON のペイロード |
| [common_headers.md](common_headers.md) | `thermo_ble_uuid.h` `thermo_log.h` `thermo_version.h` `switchbot_sample.h` | UUID、ログのレベル、バージョンと git のハッシュ、SwitchBot の値 |
| [observations.md](observations.md) | (全体) | ノード、ゲートウェイ、共通のコードを読んで気づいた点のまとめ |

## 呼び出しの関係

```text
cfg_shell.c ──▶ cfg.c ◀──── cloud.c ──▶ payload.c
  (thermo)    (設定)          │  ▲
                              │  └── wifi_link.c (WiFi)
                              └───── ntp.c (時計)
```

- `cloud.c` が中心です。設定が揃うのを待ち、WiFi につなぎ、時計を合わせ、MQTT で送ります。
- ゲートウェイは、`cloud_init(true)` で、起動のときから動かします。ノードは、`cloud_init(false)` で作って、ゲートウェイにつながらないときだけ、`cloud_set_enabled(true)` で動かします。
- ノードだけが使う、WiFi での時刻の取得 (`wifi_time.c`) は、`wifi_link.c` と `cfg.c` を直接使います。

## 関連する文書

- `app/thermo-node/docs/index.md`: ノードのソースコードの解析
- `app/thermo-gateway/docs/index.md`: ゲートウェイのソースコードの解析
- [../../../AWS_SETUP.md](../../../AWS_SETUP.md): AWS IoT Core の設定
