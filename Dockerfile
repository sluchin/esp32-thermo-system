FROM ubuntu:22.04

WORKDIR /workspace

# Install dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    python3-pip \
    python3-venv \
    git \
    wget \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Python tools
RUN pip install --no-cache-dir west

# Download and install Zephyr SDK
RUN mkdir -p /opt/zephyr-sdk && \
    cd /opt/zephyr-sdk && \
    wget -q https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.5/zephyr-sdk-0.16.5_linux-x86_64.tar.xz && \
    tar xf zephyr-sdk-0.16.5_linux-x86_64.tar.xz && \
    zephyr-sdk-0.16.5/setup.sh -t all -h && \
    rm zephyr-sdk-0.16.5_linux-x86_64.tar.xz

ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk/zephyr-sdk-0.16.5
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr

CMD ["/bin/bash"]
