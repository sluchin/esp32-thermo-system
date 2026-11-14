# ZView でスレッドとヒープを見る

[ZView](https://github.com/wkhadgar/zview) は、動いている Zephyr のスレッドとヒープを、ホストの端末に表示するツールです (htop の Zephyr 版)。スレッドごとのスタックの最大の使用量 (ウォーターマーク)、CPU の使用率、ヒープの使用量と断片化を、止めずに見られます。

XIAO ESP32C3 には、USB の JTAG が入っています (書き込みに使う USB-C と同じ口)。ホストで Espressif の OpenOCD を動かし、その GDB サーバーに ZView をつなぎます。

```text
ZView ──GDB リモートプロトコル (localhost:3333)──> OpenOCD (openocd-esp32) ──USB JTAG──> XIAO ESP32C3
```

ZView が公式に対応するのは ARM の SWD のプローブ (J-Link、pyOCD) で、RISC-V の ESP32-C3 は対応の一覧にありません。ZView の `gdb` ランナーは、GDB サーバーからメモリを読むだけなので、OpenOCD を通せば ESP32-C3 でも動きます。

## 1. ホストの準備 (最初の 1 回)

ZView と OpenOCD は、USB の機器を直接使うので、Docker ではなくホストで動かします。

### ZView

```bash
pipx install zview
```

### OpenOCD (openocd-esp32)

ESP32-C3 には、Espressif 版の OpenOCD が要ります (Zephyr SDK の OpenOCD には ESP32-C3 の設定がありません)。[リリース](https://github.com/espressif/openocd-esp32/releases) から、ホストに合うものを取ってきて展開します。

```bash
mkdir -p ~/.local/opt
curl -fL https://github.com/espressif/openocd-esp32/releases/download/v0.12.0-esp32-20260831/openocd-esp32-linux-amd64-0.12.0-esp32-20260831.tar.gz \
    | tar -xz -C ~/.local/opt
# scripts/zview.sh に場所を教える (~/.bashrc などに書く)
export OPENOCD=~/.local/opt/openocd-esp32/bin/openocd
```

### udev のルール (Linux)

USB の JTAG を、root でなくても開けるようにします。ルールは openocd-esp32 に入っています。

```bash
sudo cp ~/.local/opt/openocd-esp32/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

ルールは、`plugdev` グループに読み書きを許します (`id` で、自分が `plugdev` に入っているかを確かめます)。ルールがないと、OpenOCD が `LIBUSB_ERROR_ACCESS` で失敗します。

## 2. ビルドと書き込み

環境変数 `THERMO_ZVIEW=ON` を付けてビルドします。ZView のための設定 ([app/zview.conf](../app/zview.conf)) が足されます。

```bash
THERMO_ZVIEW=ON docker compose run --rm build-node
scripts/flash.sh node /dev/ttyACM0
```

| 設定 | ZView で見られるもの |
| :--- | :--- |
| `CONFIG_THREAD_MONITOR`、`CONFIG_THREAD_NAME` | スレッドの一覧と名前 |
| `CONFIG_THREAD_STACK_INFO`、`CONFIG_INIT_STACKS` | スタックの大きさと、最大の使用量 |
| `CONFIG_THREAD_RUNTIME_STATS` | スレッドごとの CPU の使用率 |
| `CONFIG_SYS_HEAP_RUNTIME_STATS` | ヒープの使用量と断片化 |
| `CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION` | メモリスラブの最大の使用量 |

この設定は、メモリと CPU を少し余分に使います。そのため、通常のビルドには入れません。node の RAM はほぼ埋まっているので、ESP32 のタイマーのスレッド (esp_timer) と、Bluetooth のコントローラのスレッドのスタックを、通常のビルドから縮めてあります ([5. スタックの大きさ](#5-スタックの大きさ))。

`THERMO_ZVIEW=ON` を付けないビルドでも、スレッドとスタックは見られます。CPU の使用率とヒープは出ません。

通常のビルドに戻すときは、`THERMO_ZVIEW=ON` を付けずにビルドし直して、書き込みます。

## 3. 見る

```bash
# node を画面で見る (q で終わる)
scripts/zview.sh node
# 2 台つないでいるときは, ポートで台を選ぶ
scripts/zview.sh gateway /dev/ttyACM1
# 60 秒の記録をファイルに残して, あとで再生する
scripts/zview.sh node /dev/ttyACM0 record -o node.ndjson.gz --duration 60 --period 1
zview replay -i node.ndjson.gz -e build/thermo-node/zephyr/zephyr.elf
```

`scripts/zview.sh` は、OpenOCD を起動して、`zview` をその GDB サーバー (`localhost:3333`) につなぎます。`zview` を終えると、OpenOCD も止めます。OpenOCD のログは `build/openocd-<node|gateway>.log` に出ます。

- ELF は `build/thermo-<node|gateway>/zephyr/zephyr.elf` を使います。書き込んだファームウェアと同じビルドでなければなりません (違うと、ZView が止まります)。
- 2 台つないでいるときは、ポート (`/dev/ttyACM1` など) から USB のシリアル番号を調べて、OpenOCD にその台を選ばせます。
- シリアルコンソール (`picocom`) は、同じ USB のまま、並べて使えます。

| 環境変数 | 既定 | 内容 |
| :--- | :--- | :--- |
| `OPENOCD` | `PATH` の `openocd` | openocd-esp32 の `openocd` |
| `ZVIEW` | `zview` | ZView のコマンド |
| `ZVIEW_PERIOD` | `1` | `live` で読む間隔 (秒) |
| `GDB_PORT` | `3333` | OpenOCD の GDB サーバーのポート |

## 4. 注意

- **読むたびに CPU を止めます。** ZView の `gdb` ランナーは、メモリを読む間、CPU を止めます (GDB サーバーを通すため)。止める間隔が短いと、BLE と WiFi の通信が乱れることがあります。そのため、`scripts/zview.sh` の既定の間隔は 1 秒です (ZView の既定は 0.1 秒)。
- CPU の使用率は、読む間隔ごとの割合です。止まっている間の時間は数えません。
- ZView を終えると、OpenOCD が CPU を動かしてから切り離します。ファームウェアはそのまま動き続けます。
- 書き込みやリセットの直後は、OpenOCD が ESP32-C3 を調べられない (`Examination failed`)、または Zephyr のカーネルが起動していない (`Unable to read kernel thread list.`) ことがあります。数秒待って、もう一度実行します。
- OpenOCD は、CPU を止めたときに、停止の通知 (`T02`) の前に、文字の出力 (`O` のパケット) を送ります。ZView の `gdb` ランナーは、これを停止の通知と取り違えて、`No threads found on kernel.` で失敗します。`scripts/zview.sh` は、返事を読む関数だけを差し替える補正 ([scripts/zview_openocd.py](../scripts/zview_openocd.py)) を当てて、ZView を動かします。`zview` を直接動かすと、この補正は効きません。

## 5. スタックの大きさ

ZView の画面と、シェルの `kernel thread stacks` (node のシェル) で、スレッドごとのスタックの最大の使用量を見られます。`CONFIG_INIT_STACKS` で、スタックを決まった値で埋めてあり、使った分だけがその値でなくなるためです。

実機で測ったところ、次の 3 つのスタックが 100 % 使われていました。あふれていた可能性があります。

| スレッド | 大きさ (前) | 大きさ (今) | 設定 |
| :--- | :--- | :--- | :--- |
| `BT RX WQ` (node と gateway) | 1200 | 2048 | `CONFIG_BT_RX_STACK_SIZE` |
| `sysworkq` (node と gateway) | 1024 | 2048 | `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` |

広げた分の RAM は、使用量の少ないスレッドから縮めて作りました (node の RAM は、広げる前に 112 バイトしか空いていませんでした)。

| スレッド | 大きさ (前) | 大きさ (今) | 使用量の最大 | 設定 |
| :--- | :--- | :--- | :--- | :--- |
| `esp_timer` | 4096 | 2048 | 約 500 | `CONFIG_ESP32_TIMER_TASK_STACK_SIZE` |
| `btController` | 4096 | 3072 | 約 930 | `CONFIG_ESP32_BT_CONTROLLER_STACK_SIZE` |

`IRQ 00` (割り込みのスタック。`CONFIG_ISR_STACK_SIZE`) は、2048 バイトのうち 2048 バイトを使っているように出ることがあります。広げても、使用量が大きさと一緒に増える (node は常に 768 バイトが空き、gateway は 4096 バイトでも空きが 0) ので、実際の使用量ではなく、測り方の癖と見て、そのままにしました。
