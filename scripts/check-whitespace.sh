#!/bin/bash
# リポジトリで管理している全てのテキストファイルに, 行末の空白 (スペース, タブ) がないことを確認する.
#
# clang-format (scripts/format.sh) が確認するのは, app/ 配下の C のソースとヘッダだけなので,
# Markdown, YAML, CMake, シェルスクリプトなどは, このスクリプトで確認する.
# .editorconfig の trim_trailing_whitespace = true に対応する (エディタが, 保存のときに削る).
#
# 対象外:
#   - バイナリファイル
#   - *.patch, *.diff (patch の空行の文脈は, 空白 1 つの行なので, 削ると壊れる)
#   - LICENSE (ライセンスの全文)
#
# docker compose run --rm whitespace-thermo から呼ぶ.
set -eu

cd "$(dirname "$0")/.."

# git のコマンドは, コンテナの中でも, 所有者の違いで拒否されないようにする
files=$(git -c safe.directory='*' ls-files -z --cached --others --exclude-standard |
    tr '\0' '\n' | grep -vE '(^LICENSE$|\.patch$|\.diff$)' || true)

# shellcheck disable=SC2086
if matches=$(printf '%s\n' $files | xargs grep -InE '[[:space:]]+$' --); then
    echo "$matches"
    echo "whitespace: trailing whitespace found (see above)" >&2
    exit 1
fi
echo "whitespace: ok ($(printf '%s\n' "$files" | wc -l) files)"
