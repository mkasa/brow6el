#!/bin/bash

set -e

#CEF_VERSION="138.0.55+g7acdb77+chromium-138.0.7204.300"
CEF_VERSION="143.0.14+gdd46a37+chromium-143.0.7499.193"

# Determine architecture
ARCH=$(uname -m)

if [ "$ARCH" = "aarch64" ]; then
    PLATFORM="linuxarm64"
    CMAKE_ARCH_FLAG="-DPROJECT_ARCH=arm64"
elif [ "$ARCH" = "x86_64" ]; then
    PLATFORM="linux64"
    CMAKE_ARCH_FLAG=""
else
    echo "Error: Unsupported architecture: $ARCH"
    exit 1
fi

echo "Downloading CEF binary distribution for Linux..."
echo "Version: $CEF_VERSION"
echo "Platform: $PLATFORM"
echo "Architecture: $ARCH"

CEF_TARBALL="cef_binary_${CEF_VERSION}_${PLATFORM}_minimal.tar.bz2"
CEF_URL="https://cef-builds.spotifycdn.com/${CEF_TARBALL}"

if [ -d "cef_binary" ]; then
    echo "CEF directory already exists. Removing..."
    rm -rf cef_binary
fi

echo "Downloading from: $CEF_URL"
curl -L -o "$CEF_TARBALL" "$CEF_URL"

echo "Downloading checksum file..."
CHECKSUM_URL="${CEF_URL}.sha1"
curl -L -o "${CEF_TARBALL}.sha1" "$CHECKSUM_URL"

echo "Verifying checksum..."
EXPECTED_CHECKSUM=$(cat "${CEF_TARBALL}.sha1")
ACTUAL_CHECKSUM=$(sha1sum "$CEF_TARBALL" | awk '{print $1}')
if [ "$ACTUAL_CHECKSUM" != "$EXPECTED_CHECKSUM" ]; then
    echo "Error: Checksum verification failed!"
    echo "Expected: $EXPECTED_CHECKSUM"
    echo "Got:      $ACTUAL_CHECKSUM"
    rm "$CEF_TARBALL" "${CEF_TARBALL}.sha1"
    exit 1
fi
echo "Checksum verified successfully."
rm "${CEF_TARBALL}.sha1"

echo "Extracting..."
tar xjf "$CEF_TARBALL"

# Create symlink to extracted directory
EXTRACTED_DIR="cef_binary_${CEF_VERSION}_${PLATFORM}_minimal"
ln -sf "$EXTRACTED_DIR" cef_binary

echo "Cleaning up tarball..."
rm "$CEF_TARBALL"

echo "Building CEF wrapper library..."
cd cef_binary
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release $CMAKE_ARCH_FLAG ..
make -j$(nproc) libcef_dll_wrapper

echo ""
echo "CEF binary distribution downloaded and prepared successfully!"
echo "Location: $(pwd)"
