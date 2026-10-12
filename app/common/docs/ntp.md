# ntp.c の解析

対象: `app/common/ntp.c` (151 行) と `ntp.h` (28 行)

SNTP で時刻を取って、**システムの時計** (`SYS_CLOCK_REALTIME`) を合わせます。ゲートウェイと、WiFi で直接送るときのノード (`cloud.c`) が使います。

## 公開する関数

| 関数 | 役割 |
|:---|:---|
| `ntp_sync(server, timeout)` | NTP サーバに問い合わせて、システム時計を合わせる |
| `ntp_is_synced()` | 1 回でも同期したか |
| `ntp_unix_time(&sec)` | 今の UNIX 時刻を返す。同期していなければ `-EAGAIN` |

## ntp_sync

```text
同期済みで、前回の同期から CONFIG_THERMO_NTP_RESYNC_SEC (既定 3600 秒) たっていない → 問い合わせず成功
resolve_server()                 ← IPv4、UDP、ポート 123。失敗 → -EHOSTUNREACH
sntp_init() → sntp_query(timeout) → sntp_close()
sys_clock_settime(REALTIME, 秒)
last_sync_ms = 今の稼働時間、synced = true
```

- 時計に設定するのは秒だけです (小数部は捨てる)。
- 問い合わせは、`cloud_step()` が WiFi につながるたびに呼びます。間隔の確認があるので、接続のたびには、サーバに問い合わせません。
- **TLS のために必要**です。`cloud.c` は、`ntp_is_synced()` が true にならないと、MQTT の接続をしません。

## ntp_unix_time

`synced` でなければ `-EAGAIN`。そうでなければ `sys_clock_gettime()` の秒を返します。ゲートウェイは、`ble_set_time_source(ntp_unix_time)` で、この関数を、ノードに書く時刻の元にします。

## 気づいた点

- `synced` は、一度 true になると、もう戻りません。時計がどれだけずれても (同期から何日たっても)、true のままです。再同期に失敗し続けても、古い時計で続けます。
- ノードの `wifi_time.c` にも、同じ形の `resolve_server()` と SNTP の問い合わせがあります (`wifi_time.c` は、システム時計を合わせずに、秒を返す)。共通の関数にまとめることもできますが、今は重複しています。
- `sys_clock_settime()` の戻り値は、見ていません (時計の種類が違うときしか、失敗しないため)。

## 関連

- [cloud.md](cloud.md): 接続の前の同期
- [index.md](index.md): このディレクトリの文書の一覧
