#!/bin/bash
# ゲートウェイのシェルに, PEM を 1 行ずつ, ゆっくり送って, cred add で登録する.
# 貼り付けだと, シェルの受信バッファ (64 バイト) があふれて, 文字が欠けるため.
#
# 使い方: scripts/send-cred.sh <CA|CLIENT|PK> <PEM のファイル> [ポート]
# 例:     scripts/send-cred.sh CA AmazonRootCA1.pem /dev/ttyACM0
#
# picocom などで, ポートを開いたままにしないこと.

set -e

TAG=1            # セキュリティタグ (app/thermo-gateway/src/cfg.h の CFG_TLS_SEC_TAG)
CHUNK=16         # 一度に送る文字数
CHUNK_DELAY=0.05 # 分けて送るごとの待ち時間 [s]
LINE_DELAY=0.2   # 1 行ごとの待ち時間 [s]

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    echo "Usage: $0 <CA|CLIENT|PK> <PEM file> [PORT]" >&2
    exit 1
fi

TYPE="$1"
FILE="$2"
PORT="${3:-/dev/ttyACM0}"

case "$TYPE" in
    CA|CLIENT|PK) ;;
    *)
        echo "Error: TYPE must be CA, CLIENT or PK" >&2
        exit 1
        ;;
esac

if [ ! -f "$FILE" ]; then
    echo "Error: $FILE not found" >&2
    exit 1
fi

if [ ! -e "$PORT" ]; then
    echo "Error: Port $PORT not found" >&2
    exit 1
fi

# 端末の変換 (改行の変換と, エコー) を止める. 通常のファイルのときは, 無視する
stty -F "$PORT" 115200 raw -echo 2>/dev/null || true

exec 3<>"$PORT"

# シェルの出力を表示する
cat <&3 &
READER=$!
trap 'kill $READER 2>/dev/null || true' EXIT

# 入力途中のコマンドを消す
printf '\n' >&3
sleep 0.5

printf 'cred buf load\n' >&3
sleep 0.5

# PEM の改行は, LF だけにする (CR が混ざると, mbedTLS が読めない)
# 1 行 (最大 64 文字 + LF) を, 一度に送ると, 受信バッファ (64 バイト) があふれるので, 分けて送る
while IFS= read -r line || [ -n "$line" ]; do
    line="${line%$'\r'}"
    for ((i = 0; i < ${#line}; i += CHUNK)); do
        printf '%s' "${line:i:CHUNK}" >&3
        sleep "$CHUNK_DELAY"
    done
    printf '\n' >&3
    sleep "$LINE_DELAY"
done < "$FILE"

# Ctrl-C (0x03) で, バッファへの入力を終える
printf '\003' >&3
sleep 0.5

printf 'cred add %s %s default strt\n' "$TAG" "$TYPE" >&3
sleep 1

echo
echo "Done. Compare 'Stored N bytes.' with: wc -c $FILE"
