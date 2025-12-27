#!/bin/bash
set -e

echo "Building portable Brow6el archve..."

# Build the project first
./build.sh

cd build


# Create portable package directory
PORTABLE_DIR="brow6el-portable"
rm -rf "$PORTABLE_DIR"
mkdir -p "$PORTABLE_DIR"

# Copy all necessary files
cp brow6el "$PORTABLE_DIR/"
cp run_brow6el.sh "$PORTABLE_DIR/"
cp -r locales "$PORTABLE_DIR/"
cp -r scripts "$PORTABLE_DIR/"
cp *.pak "$PORTABLE_DIR/" 2>/dev/null || true
cp *.bin "$PORTABLE_DIR/" 2>/dev/null || true
cp *.so "$PORTABLE_DIR/" 2>/dev/null || true
cp *.so.1 "$PORTABLE_DIR/" 2>/dev/null || true
cp *.dat "$PORTABLE_DIR/" 2>/dev/null || true
cp *.json "$PORTABLE_DIR/" 2>/dev/null || true
cp *.js "$PORTABLE_DIR/" 2>/dev/null || true


# Create launcher script
cat > "$PORTABLE_DIR/brow6el-launch" << 'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"
exec ./run_brow6el.sh "$@"
EOF
chmod +x "$PORTABLE_DIR/brow6el-launch"

# Create tarball
TAR_NAME="brow6el-portable.tar.gz"
tar czf "$TAR_NAME" "$PORTABLE_DIR"

echo "✓ Portable package created: build/$TAR_NAME"
echo ""
echo "To use:"
echo "  tar xzf $TAR_NAME"
echo "  cd $PORTABLE_DIR"
echo "  ./brow6el-launch https://example.com"
