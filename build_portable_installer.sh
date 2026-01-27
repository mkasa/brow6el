#!/bin/bash
set -e

echo "Building self-extracting portable Brow6el installer..."

# Build the project first
./build.sh

cd build

# Parse version from version.h
if [ -f "version.h" ]; then
    VERSION=$(grep '#define BROW6EL_VERSION' version.h | cut -d'"' -f2)
    echo "Building installer for Brow6el version: $VERSION"
else
    echo "Warning: version.h not found, version information will not be included"
    VERSION="unknown"
fi

# Create portable package directory
PORTABLE_DIR="brow6el-portable"
rm -rf "$PORTABLE_DIR"
mkdir -p "$PORTABLE_DIR"

# Copy all necessary files
echo "Collecting files..."
cp brow6el "$PORTABLE_DIR/"
cp chrome-sandbox "$PORTABLE_DIR/" 2>/dev/null || true
cp -r locales "$PORTABLE_DIR/"
cp -r scripts "$PORTABLE_DIR/" 2>/dev/null || true
cp *.pak "$PORTABLE_DIR/" 2>/dev/null || true
cp *.bin "$PORTABLE_DIR/" 2>/dev/null || true
cp *.so "$PORTABLE_DIR/" 2>/dev/null || true
cp *.so.* "$PORTABLE_DIR/" 2>/dev/null || true
cp *.dat "$PORTABLE_DIR/" 2>/dev/null || true
cp *.json "$PORTABLE_DIR/" 2>/dev/null || true
cp *.js "$PORTABLE_DIR/" 2>/dev/null || true
cp *.html "$PORTABLE_DIR/" 2>/dev/null || true
chmod +x "$PORTABLE_DIR/chrome-sandbox" 2>/dev/null || true

# Create the run script inside portable dir
cat > "$PORTABLE_DIR/run_brow6el.sh" << 'RUNSCRIPT'
#!/bin/bash
# Resolve symlinks to get the actual script location
SCRIPT="$0"
while [ -L "$SCRIPT" ]; do
    SCRIPT="$(readlink "$SCRIPT")"
done
DIR="$( cd "$( dirname "$SCRIPT" )" && pwd )"
export LD_LIBRARY_PATH="$DIR:$LD_LIBRARY_PATH"
export VK_ICD_FILENAMES="$DIR/vk_swiftshader_icd.json"
"$DIR/brow6el" "$@"
RUNSCRIPT
chmod +x "$PORTABLE_DIR/run_brow6el.sh"

# Create uninstall script
cat > "$PORTABLE_DIR/brow6el-uninstall" << 'UNINSTALL'
#!/bin/bash
set -e

# Resolve symlinks to get the actual script location
SCRIPT="$0"
while [ -L "$SCRIPT" ]; do
    SCRIPT="$(readlink "$SCRIPT")"
done
INSTALL_DIR="$( cd "$( dirname "$SCRIPT" )" && pwd )"

echo "╔════════════════════════════════════════════╗"
echo "║    Brow6el Uninstaller                     ║"
echo "╚════════════════════════════════════════════╝"
echo ""
echo "This will remove:"
echo "  - $INSTALL_DIR"
echo "  - $HOME/.local/bin/brow6el (if exists)"
echo "  - $HOME/.local/bin/brow6el-uninstall (if exists)"
echo ""
read -p "Are you sure you want to uninstall Brow6el? [y/N] " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Uninstall cancelled."
    exit 0
fi

echo "Removing symlinks..."
rm -f "$HOME/.local/bin/brow6el" 2>/dev/null || true
rm -f "$HOME/.local/bin/brow6el-uninstall" 2>/dev/null || true

echo "Removing installation directory..."
rm -rf "$INSTALL_DIR"

echo "✓ Brow6el has been uninstalled"
echo ""
echo "Note: Profile data in ~/.brow6el was NOT removed"
echo "To remove profile data: rm -rf ~/.brow6el"
UNINSTALL
chmod +x "$PORTABLE_DIR/brow6el-uninstall"

# Create tarball for embedding
TAR_NAME="brow6el-portable.tar.gz"
echo "Creating archive..."
tar czf "$TAR_NAME" "$PORTABLE_DIR"

# Create self-extracting installer
INSTALLER="brow6el-installer.sh"
echo "Creating self-extracting installer..."

cat > "$INSTALLER" << 'INSTALLER_HEADER'
#!/bin/bash
set -e

# Self-extracting installer for Brow6el
INSTALL_DIR="$HOME/.local/brow6el"
SKIP_DEPS=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --install-dir)
            INSTALL_DIR="$2"
            shift 2
            ;;
        --skip-deps)
            SKIP_DEPS=true
            shift
            ;;
        --version)
            echo "Brow6el Portable Installer v$VERSION"
            exit 0
            ;;
        --help)
            echo "Brow6el Portable Installer v$VERSION"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --install-dir PATH    Install to custom directory (default: ~/.local/brow6el)"
            echo "  --skip-deps          Skip dependency checking"
            echo "  --version            Show version information"
            echo "  --help               Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo ""
echo "Brow6el Browser - Portable Installer v$VERSION"
echo ""

# Detect distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif [ -f /etc/debian_version ]; then
        echo "debian"
    elif [ -f /etc/redhat-release ]; then
        echo "rhel"
    elif [ -f /etc/arch-release ]; then
        echo "arch"
    else
        echo "unknown"
    fi
}

# Check if distribution is supported by portable installer
DISTRO=$(detect_distro)
if [ "$DISTRO" = "rhel" ] || [ "$DISTRO" = "centos" ] || [ "$DISTRO" = "rocky" ] || [ "$DISTRO" = "almalinux" ]; then
    echo "⚠ RHEL/CentOS/Rocky/AlmaLinux are not supported by the portable installer."
    echo ""
    echo "Due to GLIBC and library compatibility issues, you must build from source:"
    echo "  git clone https://github.com/YOUR_REPO/brow6el.git"
    echo "  cd brow6el"
    echo "  ./build.sh"
    echo ""
    echo "Required packages:"
    echo "  sudo dnf install git gcc gcc-c++ make cmake pkgconfig libX11-devel"
    echo "  sudo dnf install alsa-lib-devel nss-devel glib2-devel dbus-devel"
    echo "  sudo dnf install atk-devel at-spi2-atk-devel cups-devel libXcomposite-devel"
    echo "  sudo dnf install libXdamage-devel libXfixes-devel libXrandr-devel mesa-libgbm-devel"
    echo "  sudo dnf install libxkbcommon-devel cairo-devel pango-devel"
    echo ""
    echo "For libsixel, build from source (see documentation)."
    echo ""
    exit 1
fi

# Detect distribution (again for rest of script)
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif [ -f /etc/debian_version ]; then
        echo "debian"
    elif [ -f /etc/redhat-release ]; then
        echo "rhel"
    elif [ -f /etc/arch-release ]; then
        echo "arch"
    else
        echo "unknown"
    fi
}

# Check and install dependencies
check_dependencies() {
    local DISTRO=$(detect_distro)
    local MISSING_DEPS=""
    
    echo "Detected distribution: $DISTRO"
    echo "Checking dependencies..."
    
    case "$DISTRO" in
        debian|ubuntu|linuxmint)
            # List of base package names that we need
            DEPS="libx11-6 libasound2 libsixel-bin libnss3 libglib2.0-bin libdbus-1-3 libatk1.0-0 libatk-bridge2.0-0 libcups2 libxcomposite1 libxdamage1 libxfixes3 libxrandr2 libgbm1 libxkbcommon-x11-0 libcairo2 libpango-1.0-0"
            PKG_MGR="apt"
            for dep in $DEPS; do
                FOUND=false
                # Try exact package name first with strict check
                if dpkg-query -W -f='${db:Status-Status}' "$dep" 2>/dev/null | grep -q "^installed$"; then
                    FOUND=true
                else
                    # Try with common suffixes (t64, etc.) for Debian transitions
                    for suffix in "t64" "t32"; do
                        test_pkg="${dep}${suffix}"
                        if dpkg-query -W -f='${db:Status-Status}' "$test_pkg" 2>/dev/null | grep -q "^installed$"; then
                            FOUND=true
                            break
                        fi
                    done
                fi
                if [ "$FOUND" = false ]; then
                    MISSING_DEPS="$MISSING_DEPS $dep"
                fi
            done
            ;;
        arch|manjaro|endeavouros)
            DEPS="libx11 alsa-lib libsixel nss glib2 dbus atk at-spi2-core cups libxcomposite libxdamage libxfixes libxrandr mesa libxkbcommon-x11 cairo pango"
            PKG_MGR="pacman"
            for dep in $DEPS; do
                if ! pacman -Q "$dep" &>/dev/null; then
                    MISSING_DEPS="$MISSING_DEPS $dep"
                fi
            done
            ;;
        fedora|rhel|centos|rocky|almalinux)
            DEPS="libX11 alsa-lib nss glib2 dbus-libs atk at-spi2-atk cups-libs libXcomposite libXdamage libXfixes libXrandr mesa-libgbm libxkbcommon cairo pango"
            PKG_MGR="dnf"
            IS_FEDORA=false
            if [ "$DISTRO" = "fedora" ]; then
                IS_FEDORA=true
                # Check libsixel as RPM package on Fedora
                if ! rpm -q libsixel &>/dev/null; then
                    MISSING_DEPS="$MISSING_DEPS libsixel"
                fi
            else
                # On RHEL, check if libsixel is available in system library cache (might be built from source)
                if ! ldconfig -p 2>/dev/null | grep -q "libsixel.so"; then
                    MISSING_DEPS="$MISSING_DEPS libsixel"
                fi
            fi
            
            # Check other dependencies
            for dep in $DEPS; do
                if ! rpm -q "$dep" &>/dev/null; then
                    MISSING_DEPS="$MISSING_DEPS $dep"
                fi
            done
            
            # Add note about libsixel for RHEL-based systems only if it's missing
            if echo "$MISSING_DEPS" | grep -q "libsixel"; then
                if [ "$IS_FEDORA" = false ]; then
                    echo ""
                    echo "⚠ Note: libsixel is not available in RHEL/CentOS repositories."
                    echo "   You need to build it from source:"
                    echo ""
                    echo "   sudo dnf install git gcc make meson pkgconfig gd-devel libjpeg-devel libpng-devel"
                    echo "   git clone https://github.com/libsixel/libsixel.git"
                    echo "   cd libsixel"
                    echo "   meson setup build"
                    echo "   meson compile -C build"
                    echo "   sudo meson install -C build"
                    echo "   echo '/usr/local/lib64' | sudo tee /etc/ld.so.conf.d/local.conf"
                    echo "   sudo ldconfig"
                    echo ""
                fi
            fi
            ;;
        opensuse*|suse|sles)
            DEPS="libX11-6 alsa mozilla-nss glib2-tools dbus-1 libatk-1_0-0 libatk-bridge-2_0-0 libcups2 libXcomposite1 libXdamage1 libXfixes3 libXrandr2 libgbm1 libxkbcommon-x11-0 libcairo2 libpango-1_0-0 libsixel1"
            PKG_MGR="zypper"
            for dep in $DEPS; do
                if ! rpm -q "$dep" &>/dev/null; then
                    MISSING_DEPS="$MISSING_DEPS $dep"
                fi
            done
            ;;
        *)
            echo "⚠ Unknown distribution, skipping dependency check"
            echo "  Required libraries: libX11, ALSA, NSS, GTK/ATK, Cups, Cairo, Pango, libsixel"
            echo "  For terminal graphics support, install a sixel or kitty-capable terminal"
            return
            ;;
    esac
    
    if [ -n "$MISSING_DEPS" ]; then
        echo ""
        echo "⚠ Missing dependencies detected:"
        
        # For RHEL-based systems, filter out libsixel from the install command
        INSTALL_DEPS="$MISSING_DEPS"
        if [ "$DISTRO" = "rhel" ] || [ "$DISTRO" = "centos" ] || [ "$DISTRO" = "rocky" ] || [ "$DISTRO" = "almalinux" ]; then
            INSTALL_DEPS=$(echo "$MISSING_DEPS" | sed 's/libsixel//g')
        fi
        
        echo "  $MISSING_DEPS"
        echo ""
        
        if [ "$EUID" -eq 0 ]; then
            read -p "Install missing dependencies? [Y/n] " -n 1 -r
            echo ""
            if [[ ! $REPLY =~ ^[Nn]$ ]]; then
                case "$PKG_MGR" in
                    apt)
                        apt update && apt install -y $INSTALL_DEPS
                        ;;
                    pacman)
                        pacman -S --noconfirm $INSTALL_DEPS
                        ;;
                    dnf)
                        if [ -n "$INSTALL_DEPS" ]; then
                            dnf install -y $INSTALL_DEPS
                        fi
                        ;;
                    zypper)
                        zypper install -y $INSTALL_DEPS
                        ;;
                esac
                echo "✓ Dependencies installed"
            fi
        else
            echo "To install dependencies, run:"
            case "$PKG_MGR" in
                apt)
                    echo "  sudo apt install $INSTALL_DEPS"
                    ;;
                pacman)
                    echo "  sudo pacman -S $INSTALL_DEPS"
                    ;;
                dnf)
                    if [ -n "$INSTALL_DEPS" ]; then
                        echo "  sudo dnf install $INSTALL_DEPS"
                    fi
                    ;;
                zypper)
                    echo "  sudo zypper install $INSTALL_DEPS"
                    ;;
            esac
            echo ""
            read -p "Continue installation anyway? [y/N] " -n 1 -r
            echo ""
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                echo "Installation cancelled."
                exit 1
            fi
        fi
    else
        echo "✓ All dependencies satisfied"
    fi
    echo ""
}

# Check dependencies first
SKIP_DEPS=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --install-dir)
            INSTALL_DIR="$2"
            shift 2
            ;;
        --skip-deps)
            SKIP_DEPS=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --install-dir DIR    Install to DIR (default: ~/.local/brow6el)"
            echo "  --skip-deps          Skip dependency checking"
            echo "  --help, -h           Show this help message"
            echo ""
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

if [ "$SKIP_DEPS" = false ]; then
    check_dependencies
fi

echo "Installation directory: $INSTALL_DIR"
echo ""

# Check if already installed
if [ -d "$INSTALL_DIR" ]; then
    echo "⚠ Brow6el is already installed at $INSTALL_DIR"
    read -p "Do you want to overwrite it? [y/N] " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Installation cancelled."
        exit 0
    fi
    echo "Removing old installation..."
    rm -rf "$INSTALL_DIR"
fi

# Create parent directory
mkdir -p "$(dirname "$INSTALL_DIR")"

# Extract the archive
echo "Extracting files..."
ARCHIVE_LINE=$(awk '/^__ARCHIVE_BELOW__/ {print NR + 1; exit 0; }' "$0")
tail -n +$ARCHIVE_LINE "$0" | tar xz -C "$(dirname "$INSTALL_DIR")"

# Move extracted folder to final location
EXTRACTED_DIR="$(dirname "$INSTALL_DIR")/brow6el-portable"
if [ "$EXTRACTED_DIR" != "$INSTALL_DIR" ]; then
    mv "$EXTRACTED_DIR" "$INSTALL_DIR"
fi

echo "✓ Installation complete!"
echo ""

# Offer to create symlinks
BIN_DIR="$HOME/.local/bin"
SYMLINK="$BIN_DIR/brow6el"
UNINSTALL_SYMLINK="$BIN_DIR/brow6el-uninstall"

# Create .local/bin if it doesn't exist
if [ ! -d "$BIN_DIR" ]; then
    mkdir -p "$BIN_DIR"
    echo "Created directory: $BIN_DIR"
fi

# Create brow6el symlink
if [ -L "$SYMLINK" ] || [ -f "$SYMLINK" ]; then
    echo "⚠ $SYMLINK already exists"
    read -p "Do you want to overwrite it? [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -f "$SYMLINK"
        ln -s "$INSTALL_DIR/run_brow6el.sh" "$SYMLINK"
        echo "✓ Created symlink: $SYMLINK"
    fi
else
    ln -s "$INSTALL_DIR/run_brow6el.sh" "$SYMLINK"
    echo "✓ Created symlink: $SYMLINK"
fi

# Create uninstall symlink
if [ -L "$UNINSTALL_SYMLINK" ] || [ -f "$UNINSTALL_SYMLINK" ]; then
    rm -f "$UNINSTALL_SYMLINK"
fi
ln -s "$INSTALL_DIR/brow6el-uninstall" "$UNINSTALL_SYMLINK"
echo "✓ Created symlink: $UNINSTALL_SYMLINK"

echo ""
echo "═══════════════════════════════════════════"
echo "To run Brow6el:"
if [ -L "$SYMLINK" ]; then
    # Check if ~/.local/bin is in PATH
    if [[ ":$PATH:" == *":$BIN_DIR:"* ]]; then
        echo "  brow6el [URL]"
    else
        echo "  $SYMLINK [URL]"
        echo ""
        echo "Note: Add ~/.local/bin to PATH for easier access:"
        echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    fi
else
    echo "  $INSTALL_DIR/run_brow6el.sh [URL]"
fi
echo ""
echo "To uninstall:"
if [ -L "$UNINSTALL_SYMLINK" ]; then
    if [[ ":$PATH:" == *":$BIN_DIR:"* ]]; then
        echo "  brow6el-uninstall"
    else
        echo "  $UNINSTALL_SYMLINK"
    fi
else
    echo "  $INSTALL_DIR/brow6el-uninstall"
fi
echo "═══════════════════════════════════════════"
echo ""

exit 0

__ARCHIVE_BELOW__
INSTALLER_HEADER

# Replace VERSION placeholder with actual version (only in echo statements)
sed -i "s/v\\\$VERSION/v$VERSION/g" "$INSTALLER"

# Append the tarball to the installer
cat "$TAR_NAME" >> "$INSTALLER"
chmod +x "$INSTALLER"

echo ""
echo "✓ Self-extracting installer created: build/$INSTALLER"
echo ""
echo "To distribute and install:"
echo "  ./build/$INSTALLER"
echo ""
echo "Installation options:"
echo "  ./build/$INSTALLER --install-dir ~/my-custom-path"
echo ""

# Clean up
rm -rf "$PORTABLE_DIR"
