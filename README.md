# Brow6el - Terminal Web Browser with Sixel Support

A full-featured web browser for the terminal using Chromium (CEF) and libsixel for graphics rendering.

WARNING: this is POC code quality, it is known it doesn't work with localized keyboards, it lacks support for accented characters for input. Build process tested in Ubuntu and Debian.

## Screenshots
### Demo
![brow6el web browser demo video](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el_demo.mp4 "brow6el demo video")

### Page view
![brow6el web browser running in linux virtual terminal window showing codeberg website](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el.png "brow6el page view")


### JavaScript console
![brow6el web browser running in linux virtual terminal window showing codeberg website and opended javascript console](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el_js_console.png "brow6el js console")

## Features

- **Sixel Graphics** - Full page rendering with automatic resolution detection
- **Mouse Support** - Click, scroll, and interact with web pages
- **Keyboard Navigation** - Full keyboard support with shortcuts
- **JavaScript Console** - Execute JS commands and view console logs (Ctrl+K)
- **Bookmarks** - Save and organize your favorite pages (Ctrl+D, Ctrl+B)
- **User Scripts** - Inject custom JavaScript into pages (Ctrl+U, Ctrl+Y)
- **Download Manager** - Save files with progress tracking
- **Popup Handling** - Terminal-friendly popup dialogs
- **Multi-Instance** - Run multiple browser windows simultaneously
- **Private Mode** - Each session is isolated, cache cleared on exit
- **Modern Web** - Full HTML5/CSS3/JavaScript support via Chromium

## Keyboard Shortcuts

### Navigation
- `Ctrl+L` - Navigate to URL
- `Ctrl+R` - Reload page
- `Ctrl+Left` - Navigate back
- `Ctrl+Right` - Navigate forward
- `Ctrl+X` - Quit browser

### Features
- `Ctrl+K` - Toggle JavaScript console
- `Ctrl+D` - Add current page to bookmarks
- `Ctrl+B` - Open bookmarks dialog
- `Ctrl+U` - Open user scripts menu
- `Ctrl+Y` - Toggle auto-inject for user scripts

### Dialogs
- `ESC` - Cancel current dialog
- `↑/↓` - Navigate in menus
- `Enter` - Confirm selection

## Quick Start

```bash
# 1. Download CEF binary (~670MB, one-time)
./download_cef.sh #(or ./download_def_arm64.sh)

# 2. Build
./build.sh

# 3. Run
./build/run_brow6el.sh https://example.com

# Try the test page with all features
./build/run_brow6el.sh file://$PWD/../examples/test_dialogs.html

# Multiple instances supported!
# Open additional terminals and run more instances
```

## Examples

The `examples/` directory contains:
- **test_dialogs.html** - Comprehensive test page for all features
- **userscripts/** - Example user scripts (dark mode, Google customization, etc.)

See [examples/README.md](examples/README.md) for details.

## Advanced Features

### Bookmarks
- Press `Ctrl+D` to bookmark the current page
- Press `Ctrl+B` to view and manage bookmarks
- Navigate with ↑/↓, press Enter to open, 'd' to delete
- Bookmarks stored in `~/.brow6el/bookmarks`

### User Scripts
Custom JavaScript injection system similar to Greasemonkey/Tampermonkey.

**Quick Start:**
1. Create script directory: `mkdir -p ~/.brow6el/userscripts`
2. Add `.js` files to the directory
3. Configure URL patterns in `~/.brow6el/userscripts.conf`
4. Press `Ctrl+U` to manually inject or `Ctrl+Y` to toggle auto-inject

**Example config** (`~/.brow6el/userscripts.conf`):
```
auto_inject=true

dark-mode.js|Dark Mode|true|*
google-custom.js|Google Custom|true|*google.com*,*google.co.*
```

See [USERSCRIPTS.md](USERSCRIPTS.md) for detailed documentation.

### JavaScript Console
- Press `Ctrl+K` to open/close the console
- Type JavaScript and press Enter to execute
- Scroll through output with ↑/↓
- All console.log/warn/error messages are captured

### CEF Configuration

Brow6el uses a configuration file for Chromium command-line flags. On first run, a default configuration is created at `~/.brow6el/cef_flags.conf`.

**Features:**
- Enable/disable WebGL support
- Configure rendering options
- Adjust logging verbosity
- Set custom user agent
- And more!

**Edit the config:**
```bash
nano ~/.brow6el/cef_flags.conf
```

Changes take effect on next browser start. See the config file for available options and documentation links.

### Privacy & Data

**What persists:**
- Bookmarks (`~/.brow6el/bookmarks`)
- User scripts (`~/.brow6el/userscripts/`)
- User script config (`~/.brow6el/userscripts.conf`)
- CEF flags config (`~/.brow6el/cef_flags.conf`)

**What doesn't persist (private mode):**
- Cookies (cleared on exit)
- localStorage (cleared on exit)
- Cache (cleared on exit)
- History (not stored)

Each browser instance uses an isolated cache directory `/tmp/brow6el_<PID>` that is automatically deleted when you close the browser.

### Multiple Instances
You can run multiple browser instances simultaneously:
```bash
# Terminal 1
./build/run_brow6el.sh https://github.com

# Terminal 2
./build/run_brow6el.sh https://google.com

# Terminal 3
./build/run_brow6el.sh https://example.com
```

## Requirements

**Sixel-capable terminal**: Any terminal emulator that supports Sixel graphics (e.g. mlterm, xterm -ti vt340, foot, wezterm, etc.)

The browser automatically detects Sixel support via terminal capability queries - no manual configuration needed.

**Build Dependencies**:
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git pkg-config curl \
    libsixel-dev \
    libx11-dev libxcomposite-dev libxdamage-dev libxext-dev libxfixes-dev \
    libxrandr-dev libgbm-dev libxcb1-dev \
    libpango1.0-dev libatk1.0-dev libcups2-dev libasound2-dev \
    libnss3-dev libnspr4-dev libglib2.0-dev

# Arch Linux
sudo pacman -S base-devel cmake git pkg-config curl \
    libsixel \
    libx11 libxcomposite libxdamage libxfixes libxrandr \
    mesa pango atk cups alsa-lib nss nspr glib2
```

**Runtime Dependencies** (automatically satisfied on most systems):
- libsixel, X11 libraries, NSS/NSPR (SSL), GLib, D-Bus, ALSA, Pango, Cairo

## How It Works

CEF renders web pages offscreen → libsixel converts to sixel graphics → Output to terminal

The browser continuously renders frames as pages update, with synchronized input handling for mouse and keyboard events.

## License
MIT

Uses CEF (BSD clause 3 exception) and libsixel (MIT). See respective licenses for details.
