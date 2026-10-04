#!/bin/bash
# C ソース (app/ 配下) を, Zephyr の checkpatch.pl で検査する.
#
# Zephyr の設定 (.checkpatch.conf) に従う. このプロジェクトは, インデントが空白 4 つで,
# Zephyr (タブ 8) と違うので, インデントに関する 3 種類だけ無視する (CODING_STYLE.md を参照).
# インデントは, clang-format (scripts/format.sh) が確認する.
#
# docker compose run --rm lint-thermo から呼ぶ (checkpatch.pl は, Docker イメージの Zephyr にある).
set -eu

project=$(cd "$(dirname "$0")/.." && pwd)
files=$(cd "$project" && find app -name '*.c' -o -name '*.h' | sort)
status=0

# checkpatch.pl は, Zephyr のディレクトリで実行する (.checkpatch.conf と typedefsfile を読むため)
cd "${ZEPHYR_BASE:-/workspace/zephyr}"
for f in $files; do
    ./scripts/checkpatch.pl --no-tree --quiet \
        --ignore LEADING_SPACE,CODE_INDENT,SUSPECT_CODE_INDENT \
        -f "$project/$f" || status=1
done

if [ "$status" -eq 0 ]; then
    echo "lint: ok ($(echo "$files" | wc -l) files)"
fi
exit "$status"
