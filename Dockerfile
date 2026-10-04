FROM ubuntu:24.04

WORKDIR /workspace

ENV DEBIAN_FRONTEND=noninteractive

# 依存パッケージをインストール
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    python3-pip \
    python3-venv \
    python3-dev \
    python3-setuptools \
    python3-wheel \
    git \
    wget \
    curl \
    ca-certificates \
    gperf \
    dfu-util \
    file \
    libncurses-dev \
    libffi-dev \
    libssl-dev \
    libsdl2-dev \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Python ツールをインストール
RUN pip install --no-cache-dir --break-system-packages west

# Zephyr SDK 0.17.4 をダウンロードしてインストール
RUN mkdir -p /opt/zephyr-sdk && \
    cd /opt/zephyr-sdk && \
    curl -fL -O https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.17.4/zephyr-sdk-0.17.4_linux-x86_64.tar.xz && \
    tar xf zephyr-sdk-0.17.4_linux-x86_64.tar.xz && \
    zephyr-sdk-0.17.4/setup.sh -t all -h && \
    rm zephyr-sdk-0.17.4_linux-x86_64.tar.xz

ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk/zephyr-sdk-0.17.4
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV PIP_ROOT_USER_ACTION=ignore

# コンテナはホストユーザーで実行されるが、イメージ内の /workspace はそのユーザーの所有ではないため
RUN git config --system --add safe.directory '*'

# ビルド時に再取得しないよう、Zephyr ワークスペースをイメージに含める。
# west.yml を変更した場合は `docker compose build` を再実行すること。
COPY west.yml /workspace/esp32-thermo-system/west.yml
RUN cd /workspace && \
    west init -l esp32-thermo-system && \
    west update --narrow -o=--depth=1 && \
    pip install --no-cache-dir --break-system-packages \
        -r zephyr/scripts/requirements.txt 'esptool>=5.0.2' && \
    west blobs fetch hal_espressif

# btvirt は native_sim の BLE シミュレーション用に仮想 Bluetooth コントローラを提供する
RUN apt-get update && apt-get install -y --no-install-recommends bluez-test-tools \
    && rm -rf /var/lib/apt/lists/*

# ドキュメントの生成 (Doxygen。呼び出しグラフなどの図は Graphviz)
RUN apt-get update && apt-get install -y --no-install-recommends doxygen graphviz \
    && rm -rf /var/lib/apt/lists/*

# Zephyr は $HOME/.cache (compose では HOME=/tmp) が既に存在する場合のみ使用する
RUN mkdir -p /tmp/.cache && chmod 1777 /tmp/.cache

CMD ["/bin/bash"]
