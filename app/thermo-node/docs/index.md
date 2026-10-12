# thermo-node のソースコードの解析

`app/thermo-node` のソースを、ファイルごとに解析した文書です。関数の動き、データの形、他のファイルとのつながり、気づいた点を書いています。

## 文書の一覧

### 測定と通知

| 文書 | ソース | 内容 |
|:---|:---|:---|
| [main.md](main.md) | `main.c` | エントリポイントと、50 ms のメインループ |
| [sensor.md](sensor.md) | `sensor.c` | 温湿度の読み取り (センサ API、ADC、シミュレーション) |
| [ble.md](ble.md) | `ble.c` | アドバタイズ、GATT のサービス、温度の通知、時刻の受信 |

### 保存と送信

| 文書 | ソース | 内容 |
|:---|:---|:---|
| [direct.md](direct.md) | `direct.c` | ゲートウェイにつながらないときの、WiFi での直接の送信と、保存の制御 |
| [backlog.md](backlog.md) | `backlog.c` | SD カードの測定値の記録 (送信済みフラグ、輪) |
| [sdlog.md](sdlog.md) | `sdlog.c` | ログの SD カードへの保存 (ログのバックエンド) |

### 時刻

| 文書 | ソース | 内容 |
|:---|:---|:---|
| [node_time.md](node_time.md) | `node_time.c` | RTC の時刻と UNIX 時刻の換算 |
| [rtc_pcf8563.md](rtc_pcf8563.md) | `drivers/rtc_pcf8563.c` | PCF8563 の RTC ドライバ |
| [wifi_time.md](wifi_time.md) | `wifi_time.c` | WiFi と SNTP による時刻の取得 |
| [wifi_shell.md](wifi_shell.md) | `wifi_shell.c` `shell_extra.h` | シェルの `thermo sync` と `thermo sd` |

### 表示と操作

| 文書 | ソース | 内容 |
|:---|:---|:---|
| [oled.md](oled.md) | `oled.c` | OLED の表示 (現在値、グラフ、起動画面) |
| [history.md](history.md) | `history.c` | グラフの履歴 (区切りごとの平均のリングバッファ) |
| [history_store.md](history_store.md) | `history_store.c` | 起動のときの、履歴の復元 |
| [button.md](button.md) | `button.c` | ボタンの検出 |
| [buzzer.md](buzzer.md) | `buzzer.c` | ブザー (PWM) のメロディ |
| [boot_assets.md](boot_assets.md) | `boot_assets.c` `button_assets.c` `notes.h` | 起動画面と音の素材 |

## 全体の構成

```text
                          ┌────────────────────────── main.c ──────────────────────────┐
                          │  50 ms ごとのループ                                          │
                          └─┬────────┬─────────┬──────────┬──────────┬──────────┬───────┘
                            │        │         │          │          │          │
                       sensor.c   ble.c    direct.c   oled.c     button.c   node_time.c
                       (測定)    (通知)     │          history.c   buzzer.c   │
                                            │          history_store.c        rtc_pcf8563.c
                                            ▼
                                       backlog.c ─── sdlog.c (ログ)
                                       (SD カード)
                                            │
                          cloud.c ◀─────────┘ (WiFi、MQTT。app/common)
```

測定の流れ:

1. `sensor_read()` が温度と湿度を読む (5 秒ごと)。
2. `ble_notify_temperature()` が、つながっているゲートウェイへ通知する。
3. `direct_sample()` が、測定値を SD カードに保存する。ゲートウェイにつながっていれば「送信済み」、つながっていなければ「未送信」で保存する。つながらない測定が続くと、WiFi と MQTT で、AWS IoT Core へ直接送る。
4. `history_add()` が、グラフの履歴に加える。
5. 1 秒ごとに、OLED を更新し、ログを SD カードに書く。

## ビルド構成

ソースは、`CONFIG_*` で入れたり外したりします (`CMakeLists.txt`)。

| `CONFIG_*` | ソース |
|:---|:---|
| `THERMO_DISPLAY` | `oled.c` `history.c` `button.c` |
| `THERMO_BUZZER` | `buzzer.c` |
| `THERMO_RTC` | `node_time.c` `drivers/rtc_pcf8563.c` |
| `THERMO_WIFI_TIME` | `wifi_time.c` `wifi_shell.c` と、`app/common` の `cfg.c` `cfg_shell.c` `cloud.c` `ntp.c` `payload.c` `wifi_link.c` |
| `THERMO_DIRECT_MQTT` | `direct.c` |
| `THERMO_BACKLOG` | `backlog.c` |
| `THERMO_SDLOG` | `sdlog.c` |
| `THERMO_HISTORY_STORE` | `history_store.c` |

`app/common` の `cloud.c`、`cfg.c` などは、ゲートウェイと共通のコードで、この文書の対象外です。

## 関連する文書

- [../../../README.md](../../../README.md): プロジェクトの概要
- [../../../SETUP.md](../../../SETUP.md): 配線と、シェルの使い方
- [../../../SD_LOG.md](../../../SD_LOG.md): SD カードの記録の読み方
- `app/thermo-gateway/docs/index.md`: ゲートウェイのソースコードの解析
