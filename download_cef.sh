#!/bin/bash

set -e

# CEF beta channel. The build hash embedded in CEF_VERSION differs per platform,
# but every platform below targets the SAME Chromium version (148.0.7778.96) so
# the rest of the build (CMakeLists, sources) stays consistent across OSes.
CEF_CHANNEL="_beta"

OS="$(uname -s)"
ARCH="$(uname -m)"

# Per-platform settings: tarball platform slug, CEF version string, cmake arch
# flag, and the tools used to count CPUs / verify checksums (these differ between
# Linux and macOS).
case "$OS" in
    Linux)
        SHA1SUM_CMD="sha1sum"
        NPROC="$(nproc)"
        case "$ARCH" in
            aarch64|arm64)
                PLATFORM="linuxarm64"
                CEF_VERSION="148.0.7+g5b12d32+chromium-148.0.7778.96"
                CMAKE_ARCH_FLAG="-DPROJECT_ARCH=arm64"
                ;;
            x86_64)
                PLATFORM="linux64"
                CEF_VERSION="148.0.7+g5b12d32+chromium-148.0.7778.96"
                CMAKE_ARCH_FLAG=""
                ;;
            *)
                echo "Error: Unsupported Linux architecture: $ARCH"
                exit 1
                ;;
        esac
        ;;
    Darwin)
        # macOS: shasum/sysctl replace the Linux-only sha1sum/nproc.
        SHA1SUM_CMD="shasum -a 1"
        NPROC="$(sysctl -n hw.logicalcpu)"
        case "$ARCH" in
            arm64)
                PLATFORM="macosarm64"
                CEF_VERSION="148.0.8+g18e00ea+chromium-148.0.7778.96"
                CMAKE_ARCH_FLAG="-DPROJECT_ARCH=arm64"
                ;;
            x86_64)
                PLATFORM="macosx64"
                CEF_VERSION="148.0.8+g18e00ea+chromium-148.0.7778.96"
                CMAKE_ARCH_FLAG="-DPROJECT_ARCH=x86_64"
                ;;
            *)
                echo "Error: Unsupported macOS architecture: $ARCH"
                exit 1
                ;;
        esac
        ;;
    *)
        echo "Error: Unsupported OS: $OS"
        exit 1
        ;;
esac

echo "Downloading CEF binary distribution..."
echo "OS:           $OS"
echo "Architecture: $ARCH"
echo "Platform:     $PLATFORM"
echo "Version:      $CEF_VERSION"

CEF_TARBALL="cef_binary_${CEF_VERSION}_${PLATFORM}${CEF_CHANNEL:-}_minimal.tar.bz2"
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
ACTUAL_CHECKSUM=$($SHA1SUM_CMD "$CEF_TARBALL" | awk '{print $1}')
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
EXTRACTED_DIR="cef_binary_${CEF_VERSION}_${PLATFORM}${CEF_CHANNEL:-}_minimal"
ln -sf "$EXTRACTED_DIR" cef_binary

echo "Cleaning up tarball..."
rm "$CEF_TARBALL"

echo "Building CEF wrapper library..."
cd cef_binary
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release $CMAKE_ARCH_FLAG ..
make -j"$NPROC" libcef_dll_wrapper

echo ""
echo "CEF binary distribution downloaded and prepared successfully!"
echo "Location: $(pwd)"
