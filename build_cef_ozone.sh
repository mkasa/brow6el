#!/bin/bash

set -e

# CEF build script with Ozone headless platform (no X11 dependencies)
# This script builds CEF from source with Ozone platform enabled

CHROMIUM_VERSION="143.0.7499.147"
CEF_BRANCH="7499"  # CEF branch for Chromium 143
PLATFORM=$(uname -m)

echo "=========================================="
echo "Building CEF with Ozone (headless mode)"
echo "Chromium Version: $CHROMIUM_VERSION"
echo "CEF Branch: $CEF_BRANCH"
echo "Platform: $PLATFORM"
echo "=========================================="
echo ""
echo "To check for newer versions:"
echo "  https://bitbucket.org/chromiumembedded/cef/raw/master/CHROMIUM_BUILD_COMPATIBILITY.txt"
echo ""
echo "WARNING: This is a full Chromium/CEF build from source."
echo "Requirements:"
echo "  - ~100GB disk space"
echo "  - ~16GB RAM (32GB recommended)"
echo "  - 4+ CPU cores (more = faster)"
echo "  - Several hours build time"
echo ""
read -p "Continue? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 1
fi

# Install dependencies
echo ""
echo "Installing build dependencies..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    git \
    python3 \
    python3-pip \
    curl \
    ninja-build \
    pkg-config \
    cmake \
    libglib2.0-dev \
    libpango1.0-dev \
    libatk1.0-dev \
    libcairo2-dev \
    libcups2-dev \
    libdrm-dev \
    libgbm-dev \
    libnss3-dev \
    libnspr4-dev \
    libasound2-dev \
    libpulse-dev \
    libdbus-1-dev \
    libudev-dev \
    libxkbcommon-dev \
    libwayland-dev

# Set up build directory
WORK_DIR="${PWD}/cef_build_ozone"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# Download depot_tools
if [ ! -d "depot_tools" ]; then
    echo ""
    echo "Downloading depot_tools..."
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
fi
export PATH="$WORK_DIR/depot_tools:$PATH"

# Download automate-git.py
if [ ! -f "automate-git.py" ]; then
    echo ""
    echo "Downloading CEF build script..."
    curl -o automate-git.py https://bitbucket.org/chromiumembedded/cef/raw/master/tools/automate/automate-git.py
fi

# Set architecture
if [ "$PLATFORM" = "aarch64" ] || [ "$PLATFORM" = "arm64" ]; then
    ARCH="arm64"
    TARGET_CPU="arm64"
else
    ARCH="x64"
    TARGET_CPU="x64"
fi

# Create GN args file for Ozone build
mkdir -p chromium_git
cat > chromium_git/cef.gn_args << 'EOF'
# Ozone platform configuration
use_ozone=true
ozone_auto_platforms=false
ozone_platform_headless=true
ozone_platform="headless"
ozone_platform_wayland=false
ozone_platform_x11=false

# Disable X11
use_x11=false
use_xkbcommon=false

# Build configuration
is_official_build=true
is_debug=false
dcheck_always_on=false
symbol_level=1
enable_nacl=false
enable_remoting=false
use_gtk=false

# Performance
use_thin_lto=false
chrome_pgo_phase=0

# CEF specific
use_allocator_shim=false
is_cfi=false
EOF

# Add target CPU if cross-compiling
echo "target_cpu=\"${TARGET_CPU}\"" >> chromium_git/cef.gn_args

echo ""
echo "GN build arguments:"
cat chromium_git/cef.gn_args
echo ""

# Run CEF build
echo ""
echo "Starting CEF build (this will take several hours)..."
echo "Build log: $WORK_DIR/build.log"
python3 automate-git.py \
    --download-dir="$WORK_DIR/chromium_git" \
    --depot-tools-dir="$WORK_DIR/depot_tools" \
    --branch=$CEF_BRANCH \
    --minimal-distrib \
    --client-distrib \
    --force-clean \
    --release-build \
    --build-target=cefsimple \
    --x64-build \
    --with-pgo-profiles 2>&1 | tee build.log

# Find the built distribution
DISTRIB_DIR=$(find chromium_git/chromium/src/cef/binary_distrib -name "cef_binary_*" -type d | head -n 1)

if [ -z "$DISTRIB_DIR" ]; then
    echo ""
    echo "ERROR: Could not find built CEF distribution"
    exit 1
fi

echo ""
echo "CEF built successfully!"
echo "Location: $DISTRIB_DIR"

# Create symlink in project directory
cd "${PWD}/../.."
if [ -d "cef_binary" ] || [ -L "cef_binary" ]; then
    rm -rf cef_binary
fi
ln -sf "$DISTRIB_DIR" cef_binary

# Build libcef_dll_wrapper
echo ""
echo "Building CEF wrapper library..."
cd cef_binary
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) libcef_dll_wrapper

echo ""
echo "=========================================="
echo "CEF Ozone build complete!"
echo "Location: $(pwd)"
echo "=========================================="
echo ""
echo "Verify no X11 dependencies with:"
echo "  ldd cef_binary/Release/libcef.so | grep -i x11"
echo ""
