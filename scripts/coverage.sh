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
west twister -T "$tests" -p native_sim/native/64 -O "$out" --coverage --coverage-basedir "$out" \
    -x=Zephyr-sdk_DIR=/opt/zephyr-sdk/zephyr-sdk-0.17.4/cmake > "$out.log" 2>&1 || {
    tail -30 "$out.log"
    exit 1
}

# テストのビルドごとに gcovr の JSON を作り, 分岐を行の中の順番で数え直してから, 合わせて集計する.
# (GCC 14 以降は, 分岐をブロックの番号で見分けるので, そのままでは, 設定の違うビルドの同じ行の
# 分岐が合わない. scripts/coverage_merge.py)
# gcov の出力は, 書き込める $out に置く (--gcov-object-directory .. ソースは読み取り専用).
cd "$out"
rm -f cov-*.json
tracefiles=()
n=0
while read -r dir; do
    gcovr "$dir" --root "$project" --gcov-executable gcov --gcov-object-directory . \
        --gcov-ignore-errors=no_working_dir_found "${filters[@]}" \
        --exclude-branches-by-pattern '.*LOG_(ERR|WRN|INF|DBG|HEXDUMP_(ERR|WRN|INF|DBG)).*' \
        --json "cov-$n.json"
    tracefiles+=(--add-tracefile "cov-$n.json")
    n=$((n + 1))
done < <(find . -name CMakeCache.txt -printf '%h\n' | sort)
python3 "$project/scripts/coverage_merge.py" cov-*.json

opts=(--root "$project" "${tracefiles[@]}")

echo "Line coverage:"
gcovr "${opts[@]}" --txt --fail-under-line 100
echo "Branch coverage:"
gcovr "${opts[@]}" --txt-metric branch --txt --fail-under-branch 100
