#!/usr/bin/env python3
# Copyright (c) 2026 Tetsuya Higashi
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""gcovr の JSON の分岐を, 行の中の順番で数え直す (ファイルを書き換える).

使い方: coverage_merge.py <gcovr の JSON>...

GCC 14 以降の gcov は, 分岐と呼び出しに, ブロックの番号を付ける. gcovr は, その番号で
分岐を見分けるので, 設定 (Kconfig) の違うテストのビルドで, 同じソースの同じ行の分岐が,
別の分岐として数えられる. ブロックの番号を消して, 行の中の順番 (branchno) に置き換えると,
gcovr の --add-tracefile が, GCC 13 までと同じく, 行と順番で合わせる. 呼び出し (calls) は,
gcovr がブロックの番号を必須にしていて, 行と分岐の集計にも使わないので, そのままにする.

scripts/coverage.sh から呼ぶ.
"""

import json
import sys

BLOCK_KEYS = ("source_block_id", "destination_block_id")


def renumber(branches):
    """分岐の並びから, ブロックの番号を消して, 順番を付ける."""
    for index, branch in enumerate(branches):
        for key in BLOCK_KEYS:
            branch.pop(key, None)
        branch["branchno"] = index


def main(paths):
    """JSON のファイルを, 1 つずつ書き換える."""
    for path in paths:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        for filecov in data["files"]:
            for line in filecov["lines"]:
                renumber(line.get("branches", []))
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f)


if __name__ == "__main__":
    main(sys.argv[1:])
