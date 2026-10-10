#!/bin/bash
# ノードのシェルに, WiFi の SSID とパスワードを, ゆっくり送って, thermo set で設定する.
# 直接入力や貼り付けだと, シェルの受信バッファ (64 バイト) があふれて, 文字が欠けたり, 固まったりするため.
#
# 使い方: scripts/send-wifi.sh <SSID> <パスワード> [ポート]
#   パスワードが - のときは, 画面に出さずに, 入力を求める (シェルの履歴に残さない).
#   パスワードが空 ("") のときは, オープンネットワークとして, パスワードを設定しない.
# 例:     scripts/send-wifi.sh "my home ap" - /dev/ttyACM0
#         SYNC=1 scripts/send-wifi.sh home-ap "" /dev/ttyACM0   (送ったあと, 時刻の取得も試す)
#
# picocom などで, ポートを開いたままにしないこと.

set -e

CHUNK=16         # 一度に送る文字数
CHUNK_DELAY=0.05 # 分けて送るごとの待ち時間 [s]
CMD_DELAY=0.5    # 1 つのコマンドごとの待ち時間 [s]
SYNC_WAIT=30     # SYNC=1 のとき, 時刻の取得を待つ時間 [s] (接続に最大 20 秒, SNTP に最大 5 秒)

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    echo "Usage: $0 <SSID> <PASSWORD|-|\"\"> [PORT]" >&2
    exit 1
fi

SSID="$1"
PSK="$2"
PORT="${3:-/dev/ttyACM0}"

if [ -z "$SSID" ]; then
    echo "Error: SSID is empty" >&2
    exit 1
fi

# パスワードを画面に出さずに入力する
if [ "$PSK" = "-" ]; then
    read -r -s -p "WiFi password: " PSK
    echo
fi

if [ ! -e "$PORT" ]; then
    echo "Error: Port $PORT not found" >&2
    exit 1
fi

# シェルの引数にするため, 値を "" で囲む (中の \ と " は, \ で打ち消す)
quote() {
    local v="${1//\\/\\\\}"
    printf '"%s"' "${v//\"/\\\"}"
}

# 端末の変換 (改行の変換と, エコー) を止める. 通常のファイルのときは, 無視する
stty -F "$PORT" 115200 raw -echo 2>/dev/null || true

exec 3<>"$PORT"

# シェルの出力を表示する
cat <&3 &
READER=$!
trap 'kill $READER 2>/dev/null || true' EXIT

# 1 つのコマンドを, 少しずつ送る. 最後に改行を送って, 実行する
send() {
    local cmd="$1"
    local i

    for ((i = 0; i < ${#cmd}; i += CHUNK)); do
        printf '%s' "${cmd:i:CHUNK}" >&3
        sleep "$CHUNK_DELAY"
    done
    printf '\n' >&3
    sleep "$CMD_DELAY"
}

# 入力途中のコマンドを消す
printf '\n' >&3
sleep 0.5

send "thermo set ssid $(quote "$SSID")"
if [ -n "$PSK" ]; then
    send "thermo set psk $(quote "$PSK")"
else
    # 前のパスワードが残らないように, 空にする
    send 'thermo set psk ""'
fi
send "thermo show"

if [ "${SYNC:-0}" = "1" ]; then
    send "thermo sync"
    sleep "$SYNC_WAIT"
fi

echo
echo "Done. Check that 'ready to get the time: yes' is shown above."
