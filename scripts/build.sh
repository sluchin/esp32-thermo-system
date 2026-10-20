#!/bin/bash
# 実機用 (xiao_esp32c3) のファームウェアをビルドする.
#
# 使い方: scripts/build.sh [node|gateway|all]   (省略すると all. node と gateway の両方)
#
# 環境変数 (省略可): THERMO_DEBUG_LOG (OFF), THERMO_DISPLAY (y), THERMO_RTC (y),
# THERMO_SENSOR (aht20_bmp280), THERMO_DHT_LOCK_IRQS (y), THERMO_BUZZER (y). 括弧の中が省略したときの値.
# node だけが使う (THERMO_DEBUG_LOG は両方). THERMO_SENSOR は温湿度センサ (aht20_bmp280: AHT20 と
# BMP280 のモジュール. 気圧も送る, dht11: DHT11. app/thermo-node/sensors/dht11.overlay を足す).
# THERMO_DHT_LOCK_IRQS は THERMO_SENSOR=dht11 のときだけ使う.
#
# docker compose run --rm build / build-node / build-gateway から呼ぶ.
set -eu

cd "$(dirname "$0")/.."
sdk=-DZephyr-sdk_DIR=/opt/zephyr-sdk/zephyr-sdk-0.17.4/cmake

build_node() {
    # センサの overlay と設定 (前のビルドの値が CMake のキャッシュに残らないよう, 既定でも空を渡す.
    # DHT11 のビルドの CONFIG_DHT_LOCK_IRQS も, キャッシュ (CLI_ が付く) から消す)
    local sensor=(-DEXTRA_DTC_OVERLAY_FILE= -DEXTRA_CONF_FILE=)
    case "${THERMO_SENSOR:-aht20_bmp280}" in
    aht20_bmp280) sensor+=(-UCLI_CONFIG_DHT_LOCK_IRQS) ;;
    dht11)
        sensor=(-DEXTRA_DTC_OVERLAY_FILE=sensors/dht11.overlay -DEXTRA_CONF_FILE=sensors/dht11.conf
            "-DCONFIG_DHT_LOCK_IRQS=${THERMO_DHT_LOCK_IRQS:-y}")
        ;;
    *)
        echo "THERMO_SENSOR must be aht20_bmp280 or dht11: ${THERMO_SENSOR}" >&2
        exit 2
        ;;
    esac
    west build -b xiao_esp32c3 app/thermo-node -d build/thermo-node -- \
        "-DTHERMO_DEBUG_LOG=${THERMO_DEBUG_LOG:-OFF}" "-DCONFIG_THERMO_DISPLAY=${THERMO_DISPLAY:-y}" \
        "-DCONFIG_THERMO_RTC=${THERMO_RTC:-y}" "${sensor[@]}" \
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
