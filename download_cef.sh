#!/bin/bash

set -e

CEF_VERSION="143.0.11+g1e6e84d+chromium-143.0.7499.147"
PLATFORM="linux64"

echo "Downloading CEF binary distribution..."
echo "Version: $CEF_VERSION"
echo "Platform: $PLATFORM"

CEF_TARBALL="cef_binary_${CEF_VERSION}_${PLATFORM}.tar.bz2"
CEF_URL="https://cef-builds.spotifycdn.com/${CEF_TARBALL}"

if [ -d "cef_binary" ]; then
    echo "CEF directory already exists. Removing..."
    rm -rf cef_binary
fi

echo "Downloading from: $CEF_URL"
curl -L -o "$CEF_TARBALL" "$CEF_URL"

echo "Extracting..."
tar xjf "$CEF_TARBALL"

# Create symlink to extracted directory
EXTRACTED_DIR=$(ls -d cef_binary_* | head -n 1)
ln -sf "$EXTRACTED_DIR" cef_binary

echo "Cleaning up tarball..."
rm "$CEF_TARBALL"

echo "Building CEF wrapper library..."
cd cef_binary
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) libcef_dll_wrapper

echo ""
echo "CEF binary distribution downloaded and prepared successfully!"
echo "Location: $(pwd)"
