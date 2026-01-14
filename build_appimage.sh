#!/bin/bash
set -e

echo "Building portable Brow6el archve..."

# Build the project first
./build.sh

cd build


# Create portable package directory
APP_DIR="AppDir"
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR"/usr/share/applications "$APP_DIR"/usr/share/icons/hicolor/scalable/apps

# Download linuxdeploy
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x ./linuxdeploy-x86_64.AppImage

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
cp /lib/x86_64-linux-gnu/libsixel.so.1 "$APP_DIR/usr" 2>/dev/null || \
  cp /usr/lib/x86_64-linux-gnu/libsixel.so.1 "$APP_DIR/usr" 2>/dev/null || \
  echo "Warning: libsixel.so.1 not found"

# Copy CEF system dependencies
mkdir -p "$APP_DIR/usr/lib"
for lib in libasound.so.2 libX11.so.6 libXcomposite.so.1 libXdamage.so.1 \
           libXext.so.6 libXfixes.so.3 libXrandr.so.2 libgbm.so.1 \
           libxcb.so.1 libxkbcommon.so.0 libnss3.so libnssutil3.so \
           libnspr4.so libsmime3.so libdbus-1.so.3 libcups.so.2 \
           libdrm.so.2 libexpat.so.1 libatk-1.0.so.0 libatk-bridge-2.0.so.0; do
  cp /lib/x86_64-linux-gnu/$lib "$APP_DIR/usr/lib/" 2>/dev/null || \
    cp /usr/lib/x86_64-linux-gnu/$lib "$APP_DIR/usr/lib/" 2>/dev/null || true
done

# Minimal placeholder icon
cat > "$APP_DIR/usr/share/icons/hicolor/scalable/apps/brow6el.svg" << 'EOF'
<svg width="256" height="256" xmlns="http://www.w3.org/2000/svg">
    <rect width="256" height="256" fill="#ffffff"/>
    <text x="128" y="128" font-size="60" text-anchor="middle" dominant-baseline="middle" fill="#000000">B</text>
</svg>
EOF

# Minimal .desktop file
cat > "$APP_DIR/usr/share/applications/brow6el.desktop" << EOF
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
export LD_LIBRARY_PATH="$HERE/usr:$LD_LIBRARY_PATH"
cd "$HERE/usr"
exec ./brow6el "$@"
EOF
chmod +x "$APP_DIR/AppRun"

./linuxdeploy-x86_64.AppImage --appimage-extract-and-run --appdir AppDir --output appimage \
--desktop-file AppDir/usr/share/applications/brow6el.desktop \
--icon-file AppDir/usr/share/icons/hicolor/scalable/apps/brow6el.svg

echo "✓ AppImage created: build/brow6el-x86_64.AppImage"
