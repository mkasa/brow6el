# Brow6el - Terminal Web Browser with Sixel Support

A full-featured web browser for the terminal using Chromium (CEF) and libsixel for graphics rendering.

## Features

- **Sixel Graphics** - Full page rendering with automatic resolution detection
- **Mouse Support** - Click, scroll, and interact with web pages
- **Keyboard Navigation** - Full keyboard support with shortcuts
- **JavaScript Console** - Execute JS commands and view console logs
- **Download Manager** - Save files with progress tracking
- **Popup Handling** - Terminal-friendly popup dialogs
- **Modern Web** - Full HTML5/CSS3/JavaScript support via Chromium

## Keyboard Shortcuts

- `Ctrl+L` - Navigate to URL
- `Ctrl+J` - Toggle JavaScript console
- `Ctrl+R` - Reload page
- `Ctrl+Left` - Navigate back
- `Ctrl+Right` - Navigate forward
- `Ctrl+X` - Quit browser
- `ESC` - Cancel current dialog

## Quick Start

```bash
# 1. Download CEF binary (~670MB, one-time)
./download_cef.sh

# 2. Build
./build.sh

# 3. Run
./build/run_brow6el.sh https://example.com
```

## Requirements

**Sixel-capable terminal**: mlterm, xterm, foot, wezterm, or any terminal with sixel support

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

## Project Structure

```
brow6el/
├── src/
│   ├── main.cpp           # Browser core and CEF integration
│   ├── sixel_renderer.cpp # Sixel graphics output
│   ├── input_handler.cpp  # Mouse & keyboard handling
│   └── select_detector.js # Form element detection
├── CMakeLists.txt         # Build configuration
├── download_cef.sh        # CEF download script
└── build.sh              # Build script
```

## License
MIT

Uses CEF (BSD-style) and libsixel (MIT). See respective licenses for details.
