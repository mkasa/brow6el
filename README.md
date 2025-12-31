# Brow6el - Terminal Web Browser with Sixel Support

A full-featured web browser for the terminal using Chromium (CEF) and libsixel for graphics rendering.

> **WARNING**: Breaking change - keyboard control was switched to vim-like modal control. All in previous versions ctrl+[KEY] shortcuts are not working anymore, please follow reading this document.

> **WARNING**: this is POC code quality, it is known it doesn't work with localized keyboards, it lacks support for accented characters for input. Build process tested in Ubuntu 25.10, Debian 13, Arch Linux.

## Screenshots
### Demo
[![brow6el web browser demo video](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el.png)](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el_demo.mp4)

### Page view
![brow6el web browser running in linux virtual terminal window showing codeberg website](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el.png "brow6el page view")


### JavaScript console
![brow6el web browser running in linux virtual terminal window showing codeberg website and opended javascript console](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el_js_console.png "brow6el js console")

## Features

- **Sixel Graphics** - Full page rendering with automatic resolution detection
- **Mouse Support** - Click, scroll, and interact with web pages
- **Vim-Style Modal Control** - Efficient keyboard navigation with three modes (STANDARD, INSERT, MOUSE)
- **JavaScript Console** - Execute JS commands and view console logs
- **Bookmarks** - Save and organize your favorite pages
- **User Scripts** - Inject custom JavaScript into pages
- **Download Manager** - Save files with progress tracking
- **Popup Handling** - Terminal-friendly popup dialogs
- **Multi-Instance** - Run multiple browser windows simultaneously
- **Configurable Profiles** - Choose between temporary (private) or persistent (normal) browsing
- **Modern Web** - Full HTML5/CSS3/JavaScript support via Chromium

## Vim-Style Modal Control

Brow6el uses a vim-inspired modal keyboard interface with three modes. The current mode is always shown in the status bar.

### STANDARD Mode [S] - Default

Vim-like navigation with single-key commands (no Ctrl required):

**Navigation:**
- `h/j/k/l` or arrow keys - Navigate (left/down/up/right)
- `t/g` - Scroll up/down
- `p/n` - Back/forward in history

**Actions:**
- `r` - Reload page
- `u` - Navigate to URL
- `c` - Toggle JavaScript console
- `d` - Add bookmark
- `b` - Open bookmarks
- `f` - Hint mode (keyboard link navigation)
- `s` - User scripts menu
- `y` - Toggle auto-inject user scripts
- `x` - Exit browser

**Mode Switch:**
- `i` - Enter INSERT mode
- `e` - Enter MOUSE mode

### INSERT Mode [I]

All keypresses pass through to the webpage. Use for typing in forms, text areas, etc.

**Exit:** `ESC` - Return to STANDARD mode

### MOUSE Mode [M]

Keyboard-driven mouse emulation with visual cursor:

**Movement:**
- `h/j/k/l` or arrow keys - Move mouse (left/down/up/right)
- `q/f` - Toggle precision/fast speed

**Actions:**
- `SPACE` or `ENTER` - Click at cursor position

**Exit:** `e` or `ESC` - Return to STANDARD mode

### Smart Mode Switching

The browser automatically switches modes based on context:
- Clicking an input field in MOUSE mode → Auto-switch to INSERT mode
- Clicking a select box in MOUSE mode → Auto-switch to STANDARD mode
- Page navigation → Auto-reset to STANDARD mode

## Advanced Navigation Modes

### Hint Mode (f key)
Press `f` to show yellow hint labels on all links. Type the hint label (e.g., "a", "ab") and press Enter to navigate. This provides keyboard-only navigation without needing a mouse. Press `ESC` or `f` again to exit.

### Mouse Emulation Mode (e key)
Press `e` to activate a yellow mouse cursor overlay. Use hjkl or arrow keys to move it around the page and press SPACE/Enter to click at that position. `q` toggles precision mode, `f` toggles fast mode. This works on all elements including iframes and consent dialogs. Press `ESC` or `e` again to exit.

## Quick Start

```bash
# 1. Download CEF binary (~670MB, one-time)
./download_cef.sh #(or ./download_cef_arm64.sh)

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
- Press `d` (in STANDARD mode) to bookmark the current page
- Press `b` (in STANDARD mode) to view and manage bookmarks
- Navigate with ↑/↓ or j/k, press Enter to open, 'd' to delete
- Bookmarks stored in `~/.brow6el/bookmarks`

### User Scripts
Custom JavaScript injection system similar to Greasemonkey/Tampermonkey.

**Quick Start:**
1. Create script directory: `mkdir -p ~/.brow6el/userscripts`
2. Add `.js` files to the directory
3. Configure URL patterns in `~/.brow6el/userscripts.conf`
4. Press `s` to manually inject or `y` to toggle auto-inject

**Example config** (`~/.brow6el/userscripts.conf`):
```
auto_inject=true

dark-mode.js|Dark Mode|true|*
google-custom.js|Google Custom|true|*google.com*,*google.co.*
```

See [USERSCRIPTS.md](USERSCRIPTS.md) for detailed documentation.

### Profile Modes
Brow6el supports different profile modes for different use cases:

**Temporary Mode (Default):**
- Each session uses a new profile
- All data (cookies, cache, history) deleted on exit
- Perfect for private browsing
- No data persists between sessions

**Persistent Mode:**
- Profile saved in `~/.brow6el/profile`
- Cookies and login sessions maintained
- Cache speeds up repeated visits
- Works like a normal browser

**Configuration** (`~/.brow6el/browser.conf`):
```ini
# Choose profile mode: temporary, persistent, or custom
profile_mode=temporary

# Custom profile location (when mode=custom)
profile_path=~/.brow6el/profile

# Cache settings
cache_size_mb=500
clear_cache_on_exit=false

# Privacy options (for persistent/custom)
clear_cookies_on_exit=false
```

**Examples:**
- Private browsing: `profile_mode=temporary` (default)
- Normal browsing: `profile_mode=persistent`
- Multiple profiles: `profile_mode=custom` with different paths
- Semi-private: `persistent` with `clear_cookies_on_exit=true`

### JavaScript Console
- Press `c` (in STANDARD mode) to open/close the console
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
