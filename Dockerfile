FROM ubuntu:24.04

WORKDIR /workspace

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
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

# Install Python tools
RUN pip install --no-cache-dir --break-system-packages west

# Download and install Zephyr SDK 0.17.4
RUN mkdir -p /opt/zephyr-sdk && \
    cd /opt/zephyr-sdk && \
    curl -fL -O https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.17.4/zephyr-sdk-0.17.4_linux-x86_64.tar.xz && \
    tar xf zephyr-sdk-0.17.4_linux-x86_64.tar.xz && \
    zephyr-sdk-0.17.4/setup.sh -t all -h && \
    rm zephyr-sdk-0.17.4_linux-x86_64.tar.xz

ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk/zephyr-sdk-0.17.4
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV PIP_ROOT_USER_ACTION=ignore

# Containers run as the host user, who does not own the baked-in /workspace
RUN git config --system --add safe.directory '*'

# Bake the Zephyr workspace into the image so builds do not re-fetch it.
# Re-run `docker compose build` after changing west.yml.
COPY west.yml /workspace/esp32-thermo-system/west.yml
RUN cd /workspace && \
    west init -l esp32-thermo-system && \
    west update --narrow -o=--depth=1 && \
    pip install --no-cache-dir --break-system-packages \
        -r zephyr/scripts/requirements.txt 'esptool>=5.0.2' && \
    west blobs fetch hal_espressif

# Zephyr only uses $HOME/.cache (HOME=/tmp in compose) if it already exists
RUN mkdir -p /tmp/.cache && chmod 1777 /tmp/.cache

CMD ["/bin/bash"]
