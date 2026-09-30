#!/bin/bash

set -e

echo "================================"
echo "Zephyr Development Environment Setup"
echo "================================"
echo ""

# Detect OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
else
    echo "Unsupported OS: $OSTYPE"
    exit 1
fi

echo "Detected OS: $OS"
echo ""

# Install system dependencies
echo "Installing system dependencies..."
if [ "$OS" = "linux" ]; then
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
        python3-pip \
        python3-venv \
        git \
        wget \
        curl \
        libusb-1.0-0-dev \
        libusb-1.0-0 \
        picocom
fi

echo "✓ System dependencies installed"
echo ""

# Create Python virtual environment
echo "Creating Python virtual environment..."
if [ ! -d "venv" ]; then
    python3 -m venv venv
    source venv/bin/activate
    pip install --upgrade pip
    pip install west
    echo "✓ Virtual environment created and configured"
else
    echo "✓ Virtual environment already exists"
fi

echo ""
echo "Setup completed!"
echo ""
echo "Next steps:"
echo "1. Activate virtual environment: source venv/bin/activate"
echo "2. Initialize West workspace: west init --mr main ."
echo "3. Update dependencies: west update"
echo "4. Review SETUP.md for Zephyr SDK installation"
echo ""
