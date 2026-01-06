#!/bin/bash
set -e

echo "Building ARM64 AppImage using podman with Ubuntu (with QEMU emulation)..."

# Note: QEMU ARM64 emulation required. If build fails with "Exec format error", run:
#   sudo podman run --rm --privileged multiarch/qemu-user-static --reset -p yes

# Create temporary build script
cat > /tmp/build_in_container_arm64_$$.sh << 'INNER_EOF'
set -e

# Clean workspace to avoid architecture conflicts
rm -rf build cef_binary
mkdir -p build

# Install dependencies
apt-get update && apt-get install -y \
  wget curl cmake g++ libgtk-3-dev libx11-dev libxrandr-dev \
  libsixel-dev libxss-dev libasound2-dev libxcomposite-dev \
  libxdamage-dev libxext-dev libxfixes-dev libgbm-dev \
  libxcb1-dev libxkbcommon-dev libnss3-dev libnspr4-dev \
  libdbus-1-dev libcups2-dev libdrm-dev libexpat1-dev \
  libatk1.0-dev libatk-bridge2.0-dev file

# Clean CEF build cache if exists
rm -rf cef_binary/build/CMakeCache.txt cef_binary/build/CMakeFiles

# Download and build CEF (auto-detects architecture)
./download_cef.sh

# Clean build directory to avoid CMake cache conflicts
rm -rf build/CMakeCache.txt build/CMakeFiles

# Build the project
./build.sh

cd build

# Create portable package directory
APP_DIR="AppDir"
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR"/usr/share/applications "$APP_DIR"/usr/share/icons/hicolor/scalable/apps

# Download linuxdeploy
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-aarch64.AppImage
chmod +x ./linuxdeploy-aarch64.AppImage

# Copy all necessary files
cp brow6el "$APP_DIR/usr"
cp run_brow6el.sh "$APP_DIR/usr"
cp -r locales "$APP_DIR/usr"
cp -r scripts "$APP_DIR/usr"
cp *.pak "$APP_DIR/usr" 2>/dev/null || true
cp *.bin "$APP_DIR/usr" 2>/dev/null || true
cp *.so "$APP_DIR/usr" 2>/dev/null || true
cp *.so.1 "$APP_DIR/usr" 2>/dev/null || true
cp *.dat "$APP_DIR/usr" 2>/dev/null || true
cp *.json "$APP_DIR/usr" 2>/dev/null || true
cp *.js "$APP_DIR/usr" 2>/dev/null || true

# Copy libsixel library
cp /lib/aarch64-linux-gnu/libsixel.so.1 "$APP_DIR/usr" 2>/dev/null || \
  cp /usr/lib/aarch64-linux-gnu/libsixel.so.1 "$APP_DIR/usr" 2>/dev/null || \
  echo "Warning: libsixel.so.1 not found"

# Copy CEF system dependencies
mkdir -p "$APP_DIR/usr/lib"
for lib in libasound.so.2 libX11.so.6 libXcomposite.so.1 libXdamage.so.1 \
           libXext.so.6 libXfixes.so.3 libXrandr.so.2 libgbm.so.1 \
           libxcb.so.1 libxkbcommon.so.0 libnss3.so libnssutil3.so \
           libnspr4.so libsmime3.so libdbus-1.so.3 libcups.so.2 \
           libdrm.so.2 libexpat.so.1 libatk-1.0.so.0 libatk-bridge-2.0.so.0; do
  cp /lib/aarch64-linux-gnu/$lib "$APP_DIR/usr/lib/" 2>/dev/null || \
    cp /usr/lib/aarch64-linux-gnu/$lib "$APP_DIR/usr/lib/" 2>/dev/null || true
done

# Minimal placeholder icon
cat > "$APP_DIR/usr/share/icons/hicolor/scalable/apps/brow6el.svg" << 'EOF'
<svg width="256" height="256" xmlns="http://www.w3.org/2000/svg">
    <rect width="256" height="256" fill="#ffffff"/>
    <text x="128" y="128" font-size="60" text-anchor="middle" dominant-baseline="middle" fill="#000000">B</text>
</svg>
EOF

# Minimal .desktop file
cat > "$APP_DIR/usr/share/applications/brow6el.desktop" << 'EOF'
[Desktop Entry]
Name=brow6el
Comment=sixel-based web browser (terminal only)
Exec=brow6el %U
Icon=brow6el
Type=Application
Categories=Network;WebBrowser;
Terminal=true
EOF

# AppRun wrapper
cat > "$APP_DIR/AppRun" << 'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "${0}")")"
cd "$HERE/usr"
exec ./brow6el "$@"
EOF
chmod +x "$APP_DIR/AppRun"

./linuxdeploy-aarch64.AppImage --appdir AppDir --output appimage \
--desktop-file AppDir/usr/share/applications/brow6el.desktop \
--icon-file AppDir/usr/share/icons/hicolor/scalable/apps/brow6el.svg
INNER_EOF

# Run the build inside a Ubuntu ARM64 container using podman with emulation and FUSE enabled
# Note: Using sudo because QEMU binfmt registration requires root access
sudo podman run --rm --platform linux/arm64 --device /dev/fuse --cap-add SYS_ADMIN --security-opt apparmor=unconfined \
  -v "$(pwd)":/workspace -v /tmp/build_in_container_arm64_$$.sh:/build_script.sh \
  -w /workspace ubuntu:22.04 bash /build_script.sh

# Cleanup
rm -f /tmp/build_in_container_arm64_$$.sh

echo "✓ AppImage created: build/brow6el-aarch64.AppImage"
