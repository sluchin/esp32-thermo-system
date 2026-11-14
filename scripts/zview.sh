#!/bin/bash
# ZView で, 動いている XIAO ESP32C3 のスレッドとヒープを見る (docs/zview.md).
#
# 使い方: scripts/zview.sh <node|gateway> [PORT] [ZVIEW_COMMAND [ARGS...]]
#   PORT          XIAO ESP32C3 のシリアルポート (既定: /dev/ttyACM0). 2 台つないでいるときに, 台を選ぶ.
#   ZVIEW_COMMAND zview のコマンド (live, record. 既定: live). -e, -r, -t はこのスクリプトが付ける.
#
# 例:
#   scripts/zview.sh node                                  # 画面で見る (q で終わる)
#   scripts/zview.sh gateway /dev/ttyACM1                  # 2 台目のゲートウェイ
#   scripts/zview.sh node /dev/ttyACM0 record -o node.ndjson.gz --duration 60 --period 1
#
# 環境変数 (省略可):
#   OPENOCD       Espressif の OpenOCD (openocd-esp32) の openocd のパス (既定: PATH の openocd)
#   ZVIEW         zview のコマンド (既定: zview)
#   ZVIEW_PERIOD  live で読む間隔 (秒. 既定: 1). 読むたびに CPU を止めるので, 短くしすぎない
#   GDB_PORT      OpenOCD の GDB サーバーのポート (既定: 3333)
#
# OpenOCD を起動して, その GDB サーバーに zview をつなぐ (OpenOCD の返事に合わせる補正をして).
# zview が終わったら, OpenOCD も止める.
# OpenOCD のログは build/openocd-<node|gateway>.log に出す.
set -eu

cd "$(dirname "$0")/.."

usage() {
    sed -n '3,6p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

[ $# -ge 1 ] || usage
case "$1" in
node | gateway) target="$1" ;;
*) usage ;;
esac
shift

port=/dev/ttyACM0
if [ $# -ge 1 ] && [[ "$1" == /dev/* ]]; then
    port="$1"
    shift
fi

openocd="${OPENOCD:-openocd}"
zview="${ZVIEW:-zview}"
period="${ZVIEW_PERIOD:-1}"
gdb_port="${GDB_PORT:-3333}"
build_dir="build/thermo-$target"
elf="$build_dir/zephyr/zephyr.elf"
log="build/openocd-$target.log"

if [ ! -f "$elf" ]; then
    echo "Error: $elf not found. Build it with: THERMO_ZVIEW=ON docker compose run --rm build-$target" >&2
    exit 1
fi
# THERMO_ZVIEW=ON でないビルドでも見られるが, CPU の使用率とヒープは出ない
if ! grep -q '^CONFIG_THREAD_RUNTIME_STATS=y' "$build_dir/zephyr/.config"; then
    echo "Warning: $build_dir is not built with THERMO_ZVIEW=ON (no CPU usage and heap stats)" >&2
fi
if ! command -v "$openocd" > /dev/null; then
    echo "Error: openocd not found. Install openocd-esp32 and set OPENOCD (docs/zview.md)" >&2
    exit 1
fi
if ! command -v "$zview" > /dev/null; then
    echo "Error: zview not found. Install it with: pipx install zview (docs/zview.md)" >&2
    exit 1
fi
if [ ! -e "$port" ]; then
    echo "Error: port $port not found" >&2
    exit 1
fi

# 2 台つないでいるとき, OpenOCD に台を選ばせる. USB のシリアル番号 (MAC アドレス) はポートから調べる
serial=""
if command -v udevadm > /dev/null; then
    serial="$(udevadm info -q property -n "$port" | sed -n 's/^ID_SERIAL_SHORT=//p')"
fi

# ESP_RTOS none: OpenOCD のスレッドの解釈 (既定は FreeRTOS) を止める. Zephyr のスレッドは zview が読む.
# ESP_FLASH_SIZE 0: フラッシュへの書き込みの準備をしない (メモリを読むだけなので要らない)
args=(-c "set ESP_RTOS none" -c "set ESP_FLASH_SIZE 0")
if [ -n "$serial" ]; then
    args+=(-c "adapter serial $serial")
fi
args+=(-f board/esp32c3-builtin.cfg -c "gdb port $gdb_port" -c "telnet port disabled"
    -c "tcl port disabled")

mkdir -p build
"$openocd" "${args[@]}" > "$log" 2>&1 &
openocd_pid=$!
trap 'kill "$openocd_pid" 2> /dev/null || true; wait "$openocd_pid" 2> /dev/null || true' EXIT

# GDB サーバーが待ち受けを始めるまで待つ (最大 10 秒)
for _ in $(seq 1 50); do
    if ! kill -0 "$openocd_pid" 2> /dev/null; then
        echo "Error: OpenOCD exited. See $log" >&2
        tail -n 20 "$log" >&2
        exit 1
    fi
    if (echo > "/dev/tcp/127.0.0.1/$gdb_port") 2> /dev/null; then
        break
    fi
    sleep 0.2
done

command="${1:-live}"
if [ $# -ge 1 ]; then
    shift
fi
extra=()
if [ "$command" = live ] && [[ " $* " != *" --period "* ]]; then
    extra=(--period "$period")
fi

# OpenOCD の返事の順序に合わせる補正 (scripts/zview_openocd.py) を当てて, zview を動かす.
# 補正は zview の Python に読み込ませるので, zview の実行ファイルの 1 行目 (#!) の Python を使う
zview_path="$(command -v "$zview")"
python="$(head -n 1 "$zview_path" | sed -n 's/^#!//p')"
if [ -z "$python" ] || [ ! -x "$python" ]; then
    echo "Error: cannot find the Python of zview ($zview_path)" >&2
    exit 1
fi
"$python" "$(dirname "$0")/zview_openocd.py" "$command" -e "$elf" -r gdb -t "localhost:$gdb_port" \
    ${extra[@]+"${extra[@]}"} "$@"
