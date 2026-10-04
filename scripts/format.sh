#!/bin/bash
# C ソース (app/ 配下) の整形を, clang-format (.clang-format) で確認する / 直す.
#
#   scripts/format.sh        整形が必要なファイルがあれば, 失敗する (確認だけ. ファイルは変えない)
#   scripts/format.sh --fix  整形して, ファイルを書き換える
#
# docker compose run format-thermo / format-thermo-fix から呼ぶ (clang-format は, Docker イメージにある).
set -eu

cd "$(dirname "$0")/.."
files=$(find app -name '*.c' -o -name '*.h' | sort)

if [ "${1:-}" = "--fix" ]; then
    # shellcheck disable=SC2086
    clang-format -i $files
    echo "format: fixed $(echo "$files" | wc -l) files"
else
    # shellcheck disable=SC2086
    clang-format --dry-run --Werror $files
    echo "format: ok ($(echo "$files" | wc -l) files)"
fi
