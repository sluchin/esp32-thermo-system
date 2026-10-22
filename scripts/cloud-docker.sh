#!/bin/bash
# クラウドのセッション (claude.ai/code) で, Docker を使うための補助.
#
#   scripts/cloud-docker.sh start        Docker のデーモン (dockerd) を起動する (起動済みなら, 何もしない)
#   scripts/cloud-docker.sh hook         SessionStart フック用. クラウドのときだけ, start する
#   scripts/cloud-docker.sh build        開発用のイメージ (esp32-thermo-system) をプロキシと CA の設定を足して作る
#   scripts/cloud-docker.sh run <引数>   docker compose run --rm <引数> を実行する (イメージがなければ, 先に build する)
#                                        (例: scripts/cloud-docker.sh run build-node)
#
# なぜ必要か: クラウドの VM の外への通信は全て, プロキシ (HTTPS_PROXY=http://127.0.0.1:<ポート>) と,
# そのプロキシの CA 証明書 (SSL_CERT_FILE) を使う. コンテナはどちらも引き継がないので, 通常の
# docker compose build は curl の終了コード 60 (証明書の検証の失敗) で失敗する.
# プロキシのポートはセッションの途中でも変わるので, 実行のたびに, $HTTPS_PROXY から読む.
#
# プロキシは HTTPS (curl, pip, west の git) だけに渡す. HTTP_PROXY は渡さない: apt (Ubuntu のミラーは HTTP) が
# プロキシ経由になって, 拒否されたため (プロキシを通さずに, 直接つながる).
#
# build はベースのイメージ (ubuntu:26.04) を Docker Hub から取る. 429 (回数制限) で失敗したときは
# Google のミラー (mirror.gcr.io) から取って, 元のタグを付ける.
#
# リポジトリの Dockerfile は変えない (一時的なコピーに, CA を取り込む行を足して, ビルドする).
# ローカルや CI では使わない (プロキシの設定がないとき: ビルドは通常どおり).
#
# 環境変数 (省略可): ESP32_IMAGE (イメージの名前. 既定は esp32-thermo-system:latest),
# CLOUD_CA_BUNDLE (プロキシの CA の束. 既定は SSL_CERT_FILE, なければ /root/.ccr/ca-bundle.crt),
# CLOUD_DOCKER_DRYRUN (空でなければ, build は実行するコマンドと Dockerfile の差分を表示するだけ)

set -u

IMAGE="${ESP32_IMAGE:-esp32-thermo-system:latest}"
CA_BUNDLE="${CLOUD_CA_BUNDLE:-${SSL_CERT_FILE:-/root/.ccr/ca-bundle.crt}}"
SYSTEM_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
DOCKERD_LOG=/tmp/dockerd.log
START_WAIT_S=30
WORK=""
# 一時ディレクトリは終了のときに, 必ず消す
trap '[ -z "$WORK" ] || rm -rf "$WORK"' EXIT

repo_root() {
    cd "$(dirname "$0")/.." && pwd
}

# $HTTPS_PROXY (http://host:port) から, ホストとポートを取り出す. なければ, 空にする
PROXY_HOST=""
PROXY_PORT=""
parse_proxy() {
    local hp="${HTTPS_PROXY:-${https_proxy:-}}"
    [ -n "$hp" ] || return 0
    hp="${hp#*://}"
    hp="${hp%%/*}"
    hp="${hp##*@}"
    PROXY_HOST="${hp%:*}"
    PROXY_PORT="${hp##*:}"
}

# Dockerfile のベースのイメージ (最初の FROM) がなければ, 取得する. Docker Hub が 429 (回数制限) を
# 返すときは Google のミラー (mirror.gcr.io) から取って, 元の名前のタグを付ける
ensure_base_image() {
    local root="$1" base mirror
    base="$(awk '/^FROM /{print $2; exit}' "$root/Dockerfile")"
    [ -n "$base" ] || return 0
    docker image inspect "$base" > /dev/null 2>&1 && return 0
    echo "Pulling $base"
    docker pull "$base" && return 0
    # 公式のイメージ (名前に / がない) は library/ の下にある
    case "$base" in
        */*) mirror="mirror.gcr.io/$base" ;;
        *) mirror="mirror.gcr.io/library/$base" ;;
    esac
    echo "Pulling $base failed. Trying the mirror: $mirror"
    docker pull "$mirror" && docker tag "$mirror" "$base"
}

docker_ready() {
    docker info > /dev/null 2>&1
}

cmd_start() {
    if docker_ready; then
        echo "dockerd is running"
        return 0
    fi
    if ! command -v dockerd > /dev/null 2>&1; then
        echo "dockerd was not found" >&2
        return 1
    fi
    # フックの終了で止められないよう, 新しいセッションで, 切り離して起動する
    setsid nohup dockerd > "$DOCKERD_LOG" 2>&1 &
    local i
    for ((i = 0; i < START_WAIT_S; i++)); do
        if docker_ready; then
            echo "dockerd started"
            return 0
        fi
        sleep 1
    done
    echo "dockerd did not start within ${START_WAIT_S} s. See $DOCKERD_LOG" >&2
    return 1
}

# SessionStart フック: クラウドのときだけ起動する. 失敗しても, セッションの開始は止めない
cmd_hook() {
    if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
        return 0
    fi
    cmd_start || true
    # SessionStart フックの出力は Claude の文脈に入る. 通常の docker compose build を使わせないための注意
    echo "Cloud session: build the Docker image with scripts/cloud-docker.sh build (not docker compose build),"
    echo "and run the services (build-node, test-node, ...) with scripts/cloud-docker.sh run <service>."
    echo "See .claude/CLAUDE.md."
    return 0
}

# CA の束を証明書ごとに 1 ファイルに分ける (1 ファイルのまま入れると, 信頼ストアには
# 最初の 1 枚しか入らないことがある)
split_ca() {
    local bundle="$1" dir="$2"
    mkdir -p "$dir"
    awk -v dir="$dir" '
        /-----BEGIN CERTIFICATE-----/ { n++; f = sprintf("%s/ca-%03d.crt", dir, n) }
        f != "" { print > f }
        /-----END CERTIFICATE-----/ { f = "" }
    ' "$bundle"
}

# Dockerfile のコピーを作る. 最初の apt の段落 (&& rm -rf /var/lib/apt/lists/*) の直後に,
# CA を取り込む行と, Python (pip, west) に CA を教える引数 (ARG なので, イメージには残らない) を足す
make_dockerfile() {
    local src="$1" dst="$2"
    awk '
        { print }
        !done && $0 ~ /&& rm -rf \/var\/lib\/apt\/lists\/\*/ {
            print ""
            print "# クラウド用: プロキシの CA を信頼ストアに入れる. Python (pip, requests) は別に, CA の束を教える"
            print "COPY ca/ /usr/local/share/ca-certificates/cloud/"
            print "RUN update-ca-certificates"
            print "ARG PIP_CERT"
            print "ARG REQUESTS_CA_BUNDLE"
            print "ARG SSL_CERT_FILE"
            done = 1
        }
        END { if (!done) exit 1 }
    ' "$src" > "$dst"
}

cmd_build() {
    local root work
    root="$(repo_root)" || return 1
    if [ ! -f "$CA_BUNDLE" ]; then
        echo "The CA bundle was not found: $CA_BUNDLE" >&2
        return 1
    fi
    cmd_start || return 1
    if [ -z "${CLOUD_DOCKER_DRYRUN:-}" ]; then
        ensure_base_image "$root" || return 1
    fi
    parse_proxy

    work="$(mktemp -d)"
    WORK="$work"
    split_ca "$CA_BUNDLE" "$work/ca"
    if ! ls "$work"/ca/*.crt > /dev/null 2>&1; then
        echo "No certificate was found in $CA_BUNDLE" >&2
        return 1
    fi
    if ! make_dockerfile "$root/Dockerfile" "$work/Dockerfile"; then
        echo "The place to add the CA was not found in the Dockerfile" >&2
        return 1
    fi
    # Dockerfile の COPY が使うファイル (ビルドのコンテキストは west.yml だけ)
    cp "$root/west.yml" "$work/west.yml"

    local args=(--network host -t "$IMAGE" -f "$work/Dockerfile")
    if [ -n "$PROXY_HOST" ]; then
        args+=(
            --build-arg "HTTPS_PROXY=${HTTPS_PROXY:-$https_proxy}"
            --build-arg "https_proxy=${HTTPS_PROXY:-$https_proxy}"
            --build-arg "NO_PROXY=localhost,127.0.0.1"
            --build-arg "PIP_CERT=$SYSTEM_CA_BUNDLE"
            --build-arg "REQUESTS_CA_BUNDLE=$SYSTEM_CA_BUNDLE"
            --build-arg "SSL_CERT_FILE=$SYSTEM_CA_BUNDLE"
        )
    fi
    echo "Building $IMAGE (proxy: ${PROXY_HOST:-none}:${PROXY_PORT:-none}, CA files: $(ls "$work"/ca | wc -l))"
    # 確認用: ビルドせずに, 実行するコマンドと, 作った Dockerfile の差分を表示する
    if [ -n "${CLOUD_DOCKER_DRYRUN:-}" ]; then
        printf '%q ' docker build "${args[@]}" "$work"
        echo
        diff "$root/Dockerfile" "$work/Dockerfile"
        return 0
    fi
    docker build "${args[@]}" "$work"
}

# docker compose run --rm <引数>. イメージがなければ, 先に build する (通常の docker compose がプロキシなしで
# ビルドを始めて, 失敗するのを避ける)
cmd_run() {
    local root
    root="$(repo_root)" || return 1
    cmd_start || return 1
    if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
        echo "The image $IMAGE does not exist. Building it first."
        cmd_build || return 1
    fi
    (cd "$root" && docker compose run --rm "$@")
}

case "${1:-}" in
    start) cmd_start ;;
    hook) cmd_hook ;;
    build) cmd_build ;;
    run)
        shift
        cmd_run "$@"
        ;;
    *)
        echo "Usage: $0 start|hook|build|run <docker compose run の引数>" >&2
        exit 2
        ;;
esac
