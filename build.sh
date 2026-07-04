#!/bin/bash

set -e

echo "Building Brow6el..."

if [ ! -d "cef_binary" ]; then
    echo "CEF binary not found. Please run ./download_cef.sh first"
    exit 1
fi

OS="$(uname -s)"
ARCH="$(uname -m)"

# Portable CPU count + per-OS cmake flags.
if [ "$OS" = "Darwin" ]; then
    JOBS="$(sysctl -n hw.logicalcpu)"
    # Match the architecture the CEF wrapper was built for in download_cef.sh.
    if [ "$ARCH" = "arm64" ]; then
        CMAKE_ARCH_FLAG="-DPROJECT_ARCH=arm64"
    else
        CMAKE_ARCH_FLAG="-DPROJECT_ARCH=x86_64"
    fi
else
    JOBS="$(nproc)"
    CMAKE_ARCH_FLAG=""
fi

mkdir -p build
cd build

echo "Running CMake..."
cmake -DCMAKE_BUILD_TYPE=Release $CMAKE_ARCH_FLAG ..

echo "Compiling..."
make -j"$JOBS"

if [ "$OS" != "Darwin" ]; then
    echo "Stripping debug symbols from libcef.so..."
    if [ -f "libcef.so" ]; then
        strip --strip-debug libcef.so
        echo "libcef.so stripped (debug symbols removed)"
    fi
fi

echo ""
echo "Build complete!"
if [ "$OS" = "Darwin" ]; then
    echo "App bundle: build/brow6el.app"
    echo "Run: ./build/run_brow6el.sh [URL]"
    echo "Example: ./build/run_brow6el.sh https://example.com"
else
    echo "Run: cd build && ./run_brow6el.sh [URL]"
    echo "Example: cd build && ./run_brow6el.sh https://example.com"
fi
