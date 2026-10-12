# thermo-gateway のソースコードの解析

`app/thermo-gateway/src` のソースを、ファイルごとに解析した文書です。関数の動き、データの形、他のファイルとのつながり、気づいた点を書いています。

| 文書 | ソース | 内容 |
|:---|:---|:---|
| [main.md](main.md) | `main.c` | エントリポイント。初期化の順序と、受信した値の扱い |
| [ble.md](ble.md) | `ble.c` `ble.h` | ノードのスキャンと接続、GATT の探索、温度の購読、時刻の書き込み |
| [switchbot.md](switchbot.md) | `switchbot.c` `switchbot.h` | SwitchBot のアドバタイズの解析と、送信の間引き |

## 全体の構成

```text
        ┌────────────┐  通知 (温度, 湿度)   ┌────────────┐
        │ thermo-node │ ───────────────────▶ │            │
        └────────────┘ ◀─────────────────── │            │  MQTT over TLS
          時刻の書き込み (GATT write)          │  ble.c     │ ─────────────▶ AWS IoT Core
        ┌────────────┐  アドバタイズ          │  main.c    │   (common/cloud.c)
        │  SwitchBot  │ ───────────────────▶ │  switchbot.c│
        └────────────┘                       └────────────┘
                                                   ▲
                                      SNTP (common/ntp.c) で合わせた時刻
```

- ノードとは、GATT で接続します。ゲートウェイが中央機器 (Central)、ノードが周辺機器 (Peripheral) です。
- SwitchBot の温湿度計とは、接続しません。アドバタイズだけを受け取ります。
- AWS IoT Core への送信 (`cloud.c`)、WiFi (`wifi_link.c`)、SNTP (`ntp.c`)、設定とシェル (`cfg.c`、`cfg_shell.c`) は、ノードと共通のコードで、`app/common/` にあります。この文書の対象外です。

## 関連する文書

- [../../../README.md](../../../README.md): プロジェクトの概要
- [../../../SETUP.md](../../../SETUP.md): 配線と、シェルの使い方
- `app/thermo-node/docs/index.md`: ノードのソースコードの解析
