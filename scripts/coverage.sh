#!/bin/bash
# 単体テストのカバレッジ (行と分岐) を計測して, 100% 未満なら失敗する.
#
# 使い方: scripts/coverage.sh node|gateway
#
# twister (native_sim) でテストを実行して, gcovr で, app/thermo-<名前>/src のソースだけを集計する.
# twister の --coverage は, Zephyr 本体も含めた集計になるので, gcovr を直接実行している.
# 分岐は, LOG_* マクロの内部 (ログレベルで変わる) を除く.
#
# docker compose run coverage-thermo-node / coverage-thermo-gateway から呼ぶ.
set -eu

app=${1:?usage: coverage.sh node|gateway}
project=$(cd "$(dirname "$0")/.." && pwd)
out=${COVERAGE_OUT:-/tmp/twister-coverage}

cd "$project"
west twister -T "app/thermo-$app/tests" -p native_sim/native/64 -O "$out" --coverage \
    -x=Zephyr-sdk_DIR=/opt/zephyr-sdk/zephyr-sdk-0.17.4/cmake > "$out.log" 2>&1 || {
    tail -30 "$out.log"
    exit 1
}

cd "$out"
opts=(. --root "$project" --gcov-executable gcov --gcov-ignore-errors=no_working_dir_found
    --filter "$project/app/thermo-$app/src/"
    --exclude-branches-by-pattern '.*LOG_(ERR|WRN|INF|DBG).*')

echo "Line coverage:"
gcovr "${opts[@]}" --txt --fail-under-line 100
echo "Branch coverage:"
gcovr "${opts[@]}" --txt-metric branch --txt --fail-under-branch 100
