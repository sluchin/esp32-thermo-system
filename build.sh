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
    exit 1
}

PRISTINE=0
VERBOSE=""
COMMAND="all"

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
        *)
            usage
            ;;
    esac
done

build_app() {
    local app=$1
    echo "================================"
    echo "Building $app..."
    echo "================================"

    if [ $PRISTINE -eq 1 ]; then
        echo "Cleaning build directory..."
        rm -rf "build/$app"
    fi

    west build $VERBOSE -b xiao_esp32c3 "app/$app" -d "build/$app"

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
