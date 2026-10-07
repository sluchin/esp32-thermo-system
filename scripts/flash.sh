#!/bin/bash
# ビルドした thermo-node, または thermo-gateway を, ESP32C3 に書き込む.
#
# 使い方: scripts/flash.sh <node|gateway> [PORT]
# 例:     scripts/flash.sh node /dev/ttyACM0
#         scripts/flash.sh gateway /dev/ttyACM1
#
# 2 台つないでいるときは, 台ごとにポートが違うので, PORT で指定する.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

usage() {
    echo "Usage: scripts/flash.sh <TARGET> [PORT]"
    echo ""
    echo "Arguments:"
    echo "  TARGET     Target to flash: node or gateway"
    echo "  PORT       Serial port (default: /dev/ttyACM0)"
    echo ""
    echo "Examples:"
    echo "  scripts/flash.sh node                    # Flash node to /dev/ttyACM0"
    echo "  scripts/flash.sh gateway /dev/ttyACM1    # Flash gateway to /dev/ttyACM1"
    exit 1
}

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    usage
fi

case $1 in
    node|gateway)
        TARGET="$1"
        ;;
    *)
        usage
        ;;
esac

PORT="${2:-/dev/ttyACM0}"

# ポートの存在を確認
if [ ! -e "$PORT" ]; then
    echo "Error: Port $PORT not found"
    echo ""
    echo "Available ports:"
    PORTS="$(ls /dev/ttyACM* /dev/ttyUSB* /dev/tty.usbmodem* /dev/tty.usbserial* 2>/dev/null || true)"
    echo "${PORTS:-No serial ports found}"
    exit 1
fi

# esptool が利用可能か確認
if ! command -v esptool &> /dev/null; then
    echo "Error: esptool not found"
    echo "Install it with: pipx install esptool"
    exit 1
fi

APP="thermo-$TARGET"
BUILD_DIR="build/$APP/zephyr"

if [ ! -f "$BUILD_DIR/zephyr.bin" ]; then
    echo "Error: Build artifact not found for $APP"
    echo "Run 'docker compose run --rm build-$APP' first"
    exit 1
fi

echo "================================"
echo "Flashing $APP to $PORT"
echo "================================"

esptool -p "$PORT" write-flash 0x0 "$BUILD_DIR/zephyr.bin"

echo "✓ $APP flashed successfully"
echo ""
echo "To monitor serial output, run:"
echo "  picocom -b 115200 $PORT"
