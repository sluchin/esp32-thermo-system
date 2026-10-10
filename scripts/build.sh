#!/bin/bash
# 実機用 (xiao_esp32c3) のファームウェアをビルドする.
#
# 使い方: scripts/build.sh [node|gateway|all]   (省略すると all. node と gateway の両方)
#
# 環境変数 (省略可): THERMO_DEBUG_LOG (OFF), THERMO_DISPLAY (y), THERMO_RTC (y),
# THERMO_DHT_LOCK_IRQS (y), THERMO_BUZZER (y). 括弧の中が, 省略したときの値. node だけが使う
# (THERMO_DEBUG_LOG は, 両方).
#
# docker compose run --rm build / build-thermo-node / build-thermo-gateway から呼ぶ.
set -eu

cd "$(dirname "$0")/.."
sdk=-DZephyr-sdk_DIR=/opt/zephyr-sdk/zephyr-sdk-0.17.4/cmake

build_node() {
    west build -b xiao_esp32c3 app/thermo-node -d build/thermo-node -- \
        "-DTHERMO_DEBUG_LOG=${THERMO_DEBUG_LOG:-OFF}" "-DCONFIG_THERMO_DISPLAY=${THERMO_DISPLAY:-y}" \
        "-DCONFIG_THERMO_RTC=${THERMO_RTC:-y}" "-DCONFIG_DHT_LOCK_IRQS=${THERMO_DHT_LOCK_IRQS:-y}" \
        "-DCONFIG_THERMO_BUZZER=${THERMO_BUZZER:-y}" "$sdk"
}

build_gateway() {
    west build -b xiao_esp32c3 app/thermo-gateway -d build/thermo-gateway -- \
        "-DTHERMO_DEBUG_LOG=${THERMO_DEBUG_LOG:-OFF}" "$sdk"
}

case "${1:-all}" in
node) build_node ;;
gateway) build_gateway ;;
all)
    build_node
    build_gateway
    ;;
*)
    echo "usage: build.sh [node|gateway|all]" >&2
    exit 2
    ;;
esac
