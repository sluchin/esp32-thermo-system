#!/bin/bash
# 静的解析 (gcc -fanalyzer) を, app のソースに対して実行する. 指摘 (警告) があれば失敗する.
#
# 使い方: scripts/analyze.sh [node|gateway|all]   (省略すると all. node と gateway の両方)
#
# 通常のビルドとは分けてある (build/analyze-thermo-node, build/analyze-thermo-gateway).
#
# docker compose run --rm analyze / analyze-thermo-node / analyze-thermo-gateway から呼ぶ.
set -eu

cd "$(dirname "$0")/.."
sdk=-DZephyr-sdk_DIR=/opt/zephyr-sdk/zephyr-sdk-0.17.4/cmake

analyze() {
    west build -p always -b xiao_esp32c3 "app/thermo-$1" -d "build/analyze-thermo-$1" -- \
        -DANALYZE=ON "$sdk"
}

case "${1:-all}" in
node | gateway) analyze "$1" ;;
all)
    analyze node
    analyze gateway
    ;;
*)
    echo "usage: analyze.sh [node|gateway|all]" >&2
    exit 2
    ;;
esac
