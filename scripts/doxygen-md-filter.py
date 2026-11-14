#!/usr/bin/env python3
"""Doxygen の入力の絞り込み (FILTER_PATTERNS): Markdown の, ソースなどへの相対リンクを GitHub の URL にする.

Markdown (GitHub で読む) の相対リンクは, そのままにしておく. 生成した HTML にはリンク先のソース,
LICENSE, 画像が入らないので, Doxygen に読ませるときだけ GitHub の URL (blob と raw) に書き換える.
*.md へのリンク (Doxygen がページ同士のリンクにする), 外のリンク, ページの中のリンクは変えない.

使い方: doxygen-md-filter.py ファイル  (書き換えた内容を標準出力に出す)
"""
import os
import re
import sys

REPO = "https://github.com/sluchin/esp32-thermo-system"
BRANCH = "main"
IMAGES = (".png", ".jpg", ".jpeg", ".gif", ".svg")

path = os.path.abspath(sys.argv[1])
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
base = os.path.dirname(path)


def rewrite(target):
    """リンク先 (相対) を GitHub の URL にする. 変えないなら None."""
    if re.match(r"([a-z][a-z0-9+.-]*:|#|/)", target, re.I):
        return None
    name, sep, fragment = target.partition("#")
    if not name or name.lower().endswith(".md"):
        return None
    resolved = os.path.normpath(os.path.join(base, name))
    relative = os.path.relpath(resolved, root)
    if relative.startswith("..") or not os.path.exists(resolved):
        return None
    kind = "raw" if name.lower().endswith(IMAGES) else "blob"
    return f"{REPO}/{kind}/{BRANCH}/{relative}{sep}{fragment}"


def replace(match):
    new = rewrite(match.group(2))
    return match.group(0) if new is None else match.group(1) + new + match.group(3)


text = open(path, encoding="utf-8").read()
# Markdown のリンクと画像: ](目標)
text = re.sub(r"(\]\()([^)\s]+)(\))", replace, text)
# HTML の属性: href="目標", src="目標"
text = re.sub(r'((?:href|src)=")([^"]+)(")', replace, text)
sys.stdout.write(text)
