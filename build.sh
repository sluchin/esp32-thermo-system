#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
    echo "Usage: ./build.sh [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  node              Build Thermo Node"
    echo "  gateway           Build Thermo Gateway"
    echo "  all               Build all applications (default)"
    echo "  clean             Clean all build artifacts"
    echo ""
    echo "Options:"
    echo "  -p, --pristine    Perform a pristine build (clean before build)"
    echo "  -v, --verbose     Enable verbose output"
    echo "  -s, --sim         Build for native_sim (Linux simulation) instead of ESP32C3"
    exit 1
}

PRISTINE=0
VERBOSE=""
COMMAND="all"
SIM=0
BOARD="xiao_esp32c3"

while [[ $# -gt 0 ]]; do
    case $1 in
        node|gateway|all|clean)
            COMMAND="$1"
            shift
            ;;
        -p|--pristine)
            PRISTINE=1
            shift
            ;;
        -v|--verbose)
            VERBOSE="-v"
            shift
            ;;
        -s|--sim)
            SIM=1
            BOARD="native_sim"
            shift
            ;;
        *)
            usage
            ;;
    esac
done

build_app() {
    local app=$1
    local target="ESP32C3"
    local conf_opt=""
    local build_dir="build/$app"

    if [ $SIM -eq 1 ]; then
        target="native_sim"
        conf_opt="-DCONF_FILE=prj-native_sim.conf"
        build_dir="build/${app}-sim"
    fi

    echo "================================"
    echo "Building $app for $target..."
    echo "================================"

    if [ $PRISTINE -eq 1 ]; then
        echo "Cleaning build directory..."
        rm -rf "$build_dir"
    fi

    west build $VERBOSE -b $BOARD "app/$app" -d "$build_dir" $conf_opt

    echo "✓ $app build completed successfully"
    echo ""
}

clean_all() {
    echo "Cleaning all build artifacts..."
    rm -rf build/
    echo "✓ Clean completed"
}

case $COMMAND in
    node)
        build_app "thermo-node"
        ;;
    gateway)
        build_app "thermo-gateway"
        ;;
    all)
        build_app "thermo-node"
        build_app "thermo-gateway"
        echo "================================"
        echo "All builds completed successfully"
        echo "================================"
        ;;
    clean)
        clean_all
        ;;
    *)
        usage
        ;;
esac
