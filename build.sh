#!/bin/bash

set -e

echo "Building Brow6el..."

if [ ! -d "cef_binary" ]; then
    echo "CEF binary not found. Please run ./download_cef.sh first"
    exit 1
fi

mkdir -p build
cd build

echo "Running CMake..."
cmake -DCMAKE_BUILD_TYPE=Release ..

echo "Compiling..."
make -j$(nproc)

echo "Stripping debug symbols from libcef.so..."
if [ -f "libcef.so" ]; then
    strip --strip-debug libcef.so
    echo "libcef.so stripped (debug symbols removed)"
fi

echo ""
echo "Build complete!"
echo "Run: cd build && ./run_brow6el.sh [URL]"
echo "Example: cd build && ./run_brow6el.sh https://example.com"
