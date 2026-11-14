#!/usr/bin/env python3
#
# Copyright (c) 2026 Tetsuya Higashi
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""OpenOCD の GDB サーバーに, ZView (zview の gdb ランナー) をつなぐための補正をして, zview を動かす.

ZView の gdb ランナーは, 返事を 1 つずつ読んで, 次の命令に対応させる. OpenOCD は CPU を止めたとき,
停止の通知 (T02) の前に, 文字の出力 (O パケット. "Target halted, PC=...") を送る. そのため, ZView は
O パケットを停止の通知と取り違え, 本物の T02 が, 次のメモリの読み出しの返事に紛れ込む
("Malformed hex response ... T02", "No threads found on kernel.").

この補正は, 返事を読む関数 (GDBScraper._read_response) だけを差し替える.

- O パケット (文字の出力) は, 読み捨てる
- 1 回に受け取った複数のパケットは, 捨てずに, 次の読み取りに回す

使い方: zview と同じ (zview のコマンドの引数をそのまま渡す). zview を入れた Python で動かす.
  python3 scripts/zview_openocd.py live -e build/thermo-node/zephyr/zephyr.elf -r gdb -t localhost:3333
"""

import sys

from zview.backend.gdb import GDBScraper
from zview.cli import main

_HEX = frozenset(b"0123456789abcdefABCDEF")

_original_connect = GDBScraper.connect


def _is_console_output(payload: bytes) -> bool:
    """O パケット ('O' と, 文字を 16 進にしたもの. 文字が空の 'O' だけのものも) かを返す.

    :param payload: パケットの中身
    :return: O パケットなら True
    """
    return payload[:1] == b"O" and all(c in _HEX for c in payload[1:])


def _read_response(self) -> bytes:
    """返事のパケットを 1 つ読んで, 中身を返す (O パケットは読み捨てる).

    :return: パケットの中身
    :raises ConnectionError: 接続が閉じたとき
    """
    if self.sock is None:
        raise ConnectionError("No GDB server connected.")
    buffer = getattr(self, "_rx_buffer", b"")
    try:
        while True:
            while buffer.startswith(b"+"):
                buffer = buffer[1:]
            start = buffer.find(b"$")
            end = buffer.find(b"#", start) if start >= 0 else -1
            if end >= 0 and len(buffer) >= end + 3:
                payload = buffer[start + 1 : end]
                buffer = buffer[end + 3 :]
                if self._is_acking:
                    self.sock.sendall(b"+")
                if _is_console_output(payload):
                    continue
                return payload
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("GDB server closed the connection.")
            buffer += chunk
    finally:
        self._rx_buffer = buffer


def _connect(self) -> None:
    """つなぐ (前の接続の読み残しは捨てる).

    :raises ProbeConnectFailure: つなげなかったとき
    """
    self._rx_buffer = b""
    _original_connect(self)
    self._rx_buffer = b""


GDBScraper._read_response = _read_response
GDBScraper.connect = _connect

if __name__ == "__main__":
    sys.exit(main())
