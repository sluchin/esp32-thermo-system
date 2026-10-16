#!/bin/bash
# 単体テストのカバレッジ (行と分岐) を計測して, 100% 未満なら失敗する.
#
# 使い方: scripts/coverage.sh node|gateway|common
#
# twister (native_sim) でテストを実行して, gcovr で, 次のソースだけを集計する.
#   node, gateway: app/thermo-<名前>/src と drivers
#   common: 共有のソース (app/common. テストは app/common/tests)
# twister の --coverage は Zephyr 本体も含めた集計になるので, gcovr を直接実行している.
# 分岐は LOG_* マクロの内部 (ログレベルで変わる) を除く.
#
# docker compose run --rm coverage-node / coverage-gateway / coverage-common から呼ぶ.
set -eu

app=${1:?usage: coverage.sh node|gateway|common}
project=$(cd "$(dirname "$0")/.." && pwd)
out=${COVERAGE_OUT:-/tmp/twister-coverage}

# common は app/common/tests, node と gateway は app/thermo-<名前>/tests
if [ "$app" = common ]; then
    tests="app/common/tests"
    filters=(--filter "$project/app/common/" --exclude "$project/app/common/tests/")
else
    tests="app/thermo-$app/tests"
    filters=(--filter "$project/app/thermo-$app/src/" --filter "$project/app/thermo-$app/drivers/")
fi

cd "$project"
west twister -T "$tests" -p native_sim/native/64 -O "$out" --coverage \
    -x=Zephyr-sdk_DIR=/opt/zephyr-sdk/zephyr-sdk-0.17.4/cmake > "$out.log" 2>&1 || {
    tail -30 "$out.log"
    exit 1
}

cd "$out"
opts=(. --root "$project" --gcov-executable gcov --gcov-ignore-errors=no_working_dir_found
    "${filters[@]}"
    --exclude-branches-by-pattern '.*LOG_(ERR|WRN|INF|DBG|HEXDUMP_(ERR|WRN|INF|DBG)).*')

echo "Line coverage:"
gcovr "${opts[@]}" --txt --fail-under-line 100
echo "Branch coverage:"
gcovr "${opts[@]}" --txt-metric branch --txt --fail-under-branch 100
