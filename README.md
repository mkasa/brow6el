# Brow6el - Terminal Web Browser with Sixel Support

A full-featured web browser for the terminal using Chromium (CEF) and libsixel for graphics rendering.

> **WARNING**: Because of downgrade of CEF, it is needed to delete ~/.brow6el/profile folder when using permament profile and upgrading from versions prior 0.3.2. Otherwise CEF fails with SIGTRAP.

> **INFO**: UTF-8 input is now supported, tested with Czech and Estonian accented characters.

> **WARNING**: Breaking change - keyboard control was switched to vim-like modal control. All in previous versions ctrl+[KEY] shortcuts are not working anymore, please follow reading this document.

> **WARNING**: this is POC code quality. Build process tested in Ubuntu 25.10, Debian 13, Arch Linux. The browser itself was tested in foot terminal, yaft framebuffer terminal and Windows 11 Terminal with sixel support using WSL2.



## Screenshots
### Demo
[![brow6el web browser demo video](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el.png)](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el_demo.webm)

### Page view
![brow6el web browser running in linux virtual terminal window showing codeberg website](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el.png "brow6el page view")


### JavaScript console
![brow6el web browser running in linux virtual terminal window showing codeberg website and opended javascript console](https://codeberg.org/janantos/brow6el/raw/branch/main/screenshots/brow6el_js_console.png "brow6el js console")

## Features

- **Sixel Graphics** - Full page rendering with automatic resolution detection
- **Tiled Rendering** - Optional tile-based rendering for improved responsiveness at cost of some colour artefacts
- **Mouse Support** - Click, scroll, and interact with web pages
- **Vim-Style Modal Control** - Efficient keyboard navigation with three modes (STANDARD, INSERT, MOUSE, VISUAL)
- **Grid Jump Mode** - Fast mouse positioning with recursive grid navigation (3-4 keystrokes to almost any element)
- **Element Inspector** - Browser DevTools-like element inspection in MOUSE mode
- **JavaScript Console** - Execute JS commands and view console logs
- **Bookmarks** - Save and organize your favorite pages
- **User Scripts** - Inject custom JavaScript into pages
- **Download Manager** - Save files with progress tracking
- **Popup Handling** - Terminal-friendly popup dialogs
- **Multi-Instance** - Run multiple browser windows simultaneously
- **Configurable Profiles** - Choose between temporary (private) or persistent (normal) browsing
- **DNS-over-HTTPS (DoH)** - Secure DNS with configurable providers and modes
- **Modern Web** - Full HTML5/CSS3/JavaScript support via Chromium

## Vim-Style Modal Control

Brow6el uses a vim-inspired modal keyboard interface with three modes. The current mode is always shown in the status bar (e.g., [S][T] for Standard mode with Tiled rendering).

### STANDARD Mode [S] - Default

Vim-like navigation with single-key commands (no Ctrl required):

**Navigation:**
- `h/j/k/l` or arrow keys - Navigate (left/down/up/right)
- `t/g` - Scroll up/down
- `p/n` - Back/forward in history

**Actions:**
- `r` - Reload page
- `u` - Navigate to URL
- `U` - Copy current URL to clipboard (Shift+U)
- `c` - Toggle JavaScript console
- `d` - Add bookmark
- `b` - Open bookmarks
- `f` - Hint mode (keyboard link navigation)
- `s` - User scripts menu
- `y` - Toggle auto-inject user scripts
- `z` - Toggle tiled rendering (reduces flicker)
- `Z` - Force next frame to render monolithically
- `m` - Open downloads manager
- `x` - Exit browser

**Mode Switch:**
- `i` - Enter INSERT mode
- `e` - Enter MOUSE mode
- `v` - Enter VISUAL mode (text selection)

### INSERT Mode [I]

All keypresses pass through to the webpage. Use for typing in forms, text areas, etc.

**Exit:** `ESC` - Return to STANDARD mode

### VISUAL Mode [V]

Text selection mode for copying page content. The caret starts at the first visible text, or if coming from MOUSE mode, at the mouse cursor position.

**Caret Positioning (initial):**
- `h` - Move cursor left by character
- `l` - Move cursor right by character
- `w` - Move cursor forward by word
- `b` - Move cursor backward by word
- `j` - Move cursor down by line
- `k` - Move cursor up by line
- `v` - Start selecting from current position

**Selection (after pressing v):**
- `h` - Extend selection left by character
- `l` - Extend selection right by character
- `w` - Extend selection forward by word
- `b` - Extend selection backward by word
- `j` - Extend selection down by line
- `k` - Extend selection up by line

**Actions:**
- `y` - Copy selection to clipboard, exit VISUAL mode
- `ESC` - Cancel selection, exit VISUAL mode

**Tip:** Use MOUSE mode (`e`) to position cursor, then press `ESC` and `v` to start text selection at that exact position!

### MOUSE Mode [M]

Keyboard-driven mouse emulation with visual cursor:

**Movement:**
- `h/j/k/l` or arrow keys - Move mouse (left/down/up/right)
- `q/f` - Toggle precision/fast speed
- `r` - Toggle drag-and-drop
- `g` - Toggle grid jump mode (fast navigation)

**Actions:**
- `SPACE` or `ENTER` - Click at cursor position or drop
- `i` - Toggle inspect mode (show element info on hover)
- `r` - Drop when drag-and-drop is active

**Exit:** `e` or `ESC` - Return to STANDARD mode

#### Grid Jump Mode

Fast mouse positioning using keyboard-driven grid navigation. Press `g` in MOUSE mode to activate:

- **Adaptive Grid Overlay** - A 3x3 grid with labeled cells appears (default keys: `qweasdzxc`)
- **Quick Jump** - Press a key (q/w/e/a/s/d/z/x/c) to jump to that grid cell
- **Recursive Zoom** - Automatically shows sub-grid in selected cell for precision
- **Visual Feedback** - Green grid indicates more zoom levels available, red indicates maximum zoom
- **Auto-Precision_movement** - At maximum zoom, grid switches to cursor movement mode with high precision
- **Navigation** - `Backspace` to zoom out, `ESC` to exit grid mode
- **Configurable Keys** - Customize grid keys in `~/.brow6el/browser.conf` (grid_keys setting)

Grid mode enables precise element selection with just 3-4 keystrokes, combining speed with accuracy.

#### Inspect Mode

While in MOUSE mode, press `i` to toggle inspect mode. This feature works similar to browser DevTools inspector:

- **Element Highlighting** - Cyan border around the element under cursor
- **Info Panel** - Shows element details including:
  - Tag name, ID, and classes
  - Key attributes (href, src, type, name, etc.)
  - Dimensions and position
  - Text content preview
- **Real-time Updates** - Info updates as you move the cursor
- **Toggle Off** - Press `i` again to exit inspect mode

This is useful for debugging web pages, understanding page structure, or finding specific elements.

### Smart Mode Switching

The browser automatically switches modes based on context:
- Clicking an input field in MOUSE mode → Auto-switch to INSERT mode
- Clicking a select box in MOUSE mode → Auto-switch to STANDARD mode
- Page navigation → Auto-reset to STANDARD mode

## Advanced Navigation Modes

### Hint Mode (f key)
Press `f` to show yellow hint labels on all links. Type the hint label (e.g., "a", "ab") and press Enter to navigate. This provides keyboard-only navigation without needing a mouse. Press `ESC` or `f` again to exit.

### Mouse Emulation Mode (e key)
Press `e` to activate a yellow mouse cursor overlay. Use hjkl or arrow keys to move it around the page and press SPACE/Enter to click at that position. `q` toggles precision mode, `f` toggles fast mode. Press `i` to toggle inspect mode which shows detailed element information as you hover. This works on all elements including iframes and consent dialogs. Press `ESC` or `e` again to exit.

## Quick Start

```bash
# 1. Download CEF binary (~670MB, one-time)
./download_cef.sh 

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

**Bundled Scripts:**
The browser comes with several pre-installed scripts in the `scripts/` directory:
- **view-source.js** - View HTML source code with syntax highlighting, formatting, and line numbers (keyboard navigable)
- **reader-mode.js** - Simplifies pages to just article content (like Firefox Reader View)
- **adblock.js** - Basic ad blocking functionality
- **force-light-mode.js** - Forces light color scheme on all pages
- **frameset-redirect.js** - Redirects from frameset pages to actual content

To use bundled scripts, add them to your `~/.brow6el/userscripts.conf`:
```
view-source.js|View Page Source|true|*
```
Then press `s` to open the user scripts menu and select "View Page Source", or enable auto-inject and press the script's trigger key.

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

# Default homepage URL
default_url=https://example.com

# Grid keys for mouse emulation grid jump (must be exactly 9 characters)
# Default: qweasdzxc (3x3 grid matching keyboard layout)
# Alternative: abcdefghi (alphabetical)
grid_keys=qweasdzxc

# DNS-over-HTTPS (DoH) Configuration
# Enable secure DNS to encrypt DNS queries
doh_enabled=false
doh_server=https://cloudflare-dns.com/dns-query
doh_mode=secure
# DoH mode options:
#   secure: Mandatory DoH, no fallback (recommended for blocking)
#   automatic: Use DoH when available, fallback to system DNS
#   off: Disable DoH
# Popular DoH servers:
#   Cloudflare: https://cloudflare-dns.com/dns-query
#   Google: https://dns.google/dns-query
#   Quad9: https://dns.quad9.net/dns-query

# Tiled Rendering
# Enable tile-based sixel rendering (reduces flicker on updates)
# When enabled, only changed screen regions are redrawn
# When disabled, the entire screen is redrawn on every update
tiled_rendering=true

# Terminal Cell Dimensions (optional override)
# Leave at 0 for auto-detection (recommended)
# Only override if auto-detection produces incorrect results
#cell_width=0
#cell_height=0
```

**Examples:**
- Private browsing: `profile_mode=temporary` (default)
- Normal browsing: `profile_mode=persistent`
- Multiple profiles: `profile_mode=custom` with different paths
- Semi-private: `persistent` with `clear_cookies_on_exit=true`
- Secure DNS with blocking: `doh_enabled=true` with `doh_mode=secure`

### DNS-over-HTTPS (DoH)

Brow6el supports DNS-over-HTTPS to encrypt DNS queries and prevent DNS-based tracking or blocking.

**Configuration** (`~/.brow6el/browser.conf`):
```ini
doh_enabled=true
doh_server=https://cloudflare-dns.com/dns-query
doh_mode=secure
```

**DoH Modes:**
- `secure` - Mandatory DoH with no fallback to system DNS (recommended for ad-blocking/privacy)
- `automatic` - Attempts DoH first, falls back to system DNS on failure
- `off` - Disables DoH

**Popular DoH Providers:**
- **Cloudflare:** `https://cloudflare-dns.com/dns-query` (fast, privacy-focused)
- **Google:** `https://dns.google/dns-query` (reliable, global)
- **Quad9:** `https://dns.quad9.net/dns-query` (security-focused, blocks malicious domains)

**Note:** When using `secure` mode, ensure your DoH server resolves `google.com` (used by CEF for connectivity checks) or allows all domains, otherwise DoH may fail to initialize.

### Tiled Rendering

Brow6el supports two rendering modes that can be toggled on-the-fly:

**Tiled Rendering (default: disabled):**
- Only redraws changed screen regions
- Significantly reduces flicker during page updates, scrolling, and video playback
- More efficient for incremental updates
- Tile size automatically adapts to your terminal resolution and cell size

**Monolithic Rendering:**
- Redraws the entire screen on every update
- Simple and reliable
- May show more flicker on dynamic content

**Configuration** (`~/.brow6el/browser.conf`):
```ini
# Enable/disable tiled rendering at startup
tiled_rendering=true

# Optional: Override auto-detected terminal cell dimensions
# Only needed if auto-detection is incorrect
#cell_width=11
#cell_height=25
```

**Runtime Toggle:**
- Press `z` (in STANDARD mode) to toggle between tiled and monolithic rendering
- Press `Z` (in STANDARD mode) to force the next frame to render monolithically (useful for forcing a full screen refresh)
- Current mode shown in status bar: `[S][T]` (tiled) or `[S][M]` (monolithic)
- Useful for comparing rendering quality or troubleshooting display issues

**How It Works:**
- Tiles are dynamically sized based on your terminal resolution (typically ~30-40 tiles per screen)
- CEF provides dirty rectangles indicating what changed
- Only tiles intersecting dirty regions are redrawn
- Tiles are aligned to terminal cell boundaries and sixel band height (6 pixels) for artifact-free rendering

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

**What doesn't persist (default temporary mode, persists in persistent mode):**
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

**Sixel-capable terminal**: Any terminal emulator that supports Sixel graphics (e.g. mlterm, xterm -ti vt340, foot, wezterm, yaft, etc.)

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

## Distributions packages (community)

* Artix Linux, in the Omniverse repository : https://wiki.artixlinux.org/Main/Repositories#Omniverse
* Arch Linux, in the Arch User Repository (AUR) : https://aur.archlinux.org/packages/brow6el-git


## License
MIT

Uses CEF (BSD clause 3 exception) and libsixel (MIT). See respective licenses for details.
