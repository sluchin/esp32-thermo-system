#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
    echo "Usage: ./flash.sh [TARGET] [PORT]"
    echo ""
    echo "Arguments:"
    echo "  TARGET     Target to flash: node, gateway, or all (default: all)"
    echo "  PORT       Serial port (default: /dev/ttyUSB0)"
    echo ""
    echo "Examples:"
    echo "  ./flash.sh node              # Flash node to /dev/ttyUSB0"
    echo "  ./flash.sh gateway /dev/ttyUSB1  # Flash gateway to /dev/ttyUSB1"
    exit 1
}

TARGET="all"
PORT="/dev/ttyUSB0"

if [ $# -gt 0 ]; then
    case $1 in
        node|gateway|all)
            TARGET="$1"
            ;;
        -h|--help)
            usage
            ;;
        *)
            usage
            ;;
    esac
fi

if [ $# -gt 1 ]; then
    PORT="$2"
fi

# Check if port exists
if [ ! -e "$PORT" ]; then
    echo "Error: Port $PORT not found"
    echo ""
    echo "Available ports:"
    ls /dev/ttyUSB* /dev/tty.usbserial* 2>/dev/null || echo "No serial ports found"
    exit 1
fi

# Check if esptool is available
if ! command -v esptool.py &> /dev/null; then
    echo "Error: esptool.py not found"
    echo "Install it with: pip install esptool"
    exit 1
fi

flash_app() {
    local app=$1
    local port=$2

    local build_dir="build/$app/zephyr"

    if [ ! -f "$build_dir/zephyr.bin" ]; then
        echo "Error: Build artifact not found for $app"
        echo "Run './build.sh $app' first"
        exit 1
    fi

    echo "================================"
    echo "Flashing $app to $port"
    echo "================================"

    esptool.py -p "$port" write_flash 0x0 "$build_dir/zephyr.bin"

    echo "✓ $app flashed successfully"
    echo ""
}

case $TARGET in
    node)
        flash_app "thermo-node" "$PORT"
        ;;
    gateway)
        flash_app "thermo-gateway" "$PORT"
        ;;
    all)
        flash_app "thermo-node" "$PORT"
        echo "Connect the next device and press Enter..."
        read
        flash_app "thermo-gateway" "$PORT"
        echo "================================"
        echo "All devices flashed successfully"
        echo "================================"
        ;;
esac

echo ""
echo "To monitor serial output, run:"
echo "  picocom -b 115200 $PORT"
