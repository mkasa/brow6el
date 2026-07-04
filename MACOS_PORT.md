# macOS Port — Working Notes

Status and design notes for porting **brow6el** (a Linux terminal browser built on
CEF offscreen rendering) to macOS. This is a living document — update it as the port
progresses so future sessions can pick up where we left off.

Upstream (`codeberg.org/janantos/brow6el`) targets Linux only (Ubuntu/Debian/Arch).
This work happens on the private fork `github.com/mkasa/brow6el`, branch **`macos-port`**.

## TL;DR — current state

| Piece | Status |
|-------|--------|
| `download_cef.sh` (fetch CEF + build wrapper) | ✅ Done, verified on Apple Silicon |
| Source portability (X11, clipboard, terminal I/O) | ✅ Portable |
| `/proc/self/exe` → macOS exe-path shim | ✅ Done (`src/platform_paths.h`, 4 call sites) |
| `std::__gcd` → `std::gcd` (libc++ portability) | ✅ Done |
| CMake macOS `.app` bundle + Helper processes | ✅ Done, builds `build/brow6el.app` |
| Launch path (`run_brow6el.sh`) | ✅ Done (generated launcher) |
| CEF init + helper subprocesses + page load | ✅ Verified (loads example.com, status 200) |
| End-to-end render in a Sixel/Kitty terminal | ✅ Verified by user (yahoo.co.jp rendered as expected) |
| macOS User-Agent string | ✅ Done (`Macintosh; Intel Mac OS X 10_15_7`) |
| README macOS build instructions | ✅ Done |
| Code signing / distributable .app | ⬜ Out of scope (dev build runs unsigned locally) |

## Why macOS is different from Linux (the core issue)

CEF ships completely different artifacts per OS:

- **Linux:** `Release/libcef.so` — a plain shared library the executable links/loads via
  `LD_LIBRARY_PATH`. Subprocesses reuse the **same executable** (`browser_subprocess_path`
  points at self; `main()` early-returns from `CefExecuteProcess`).
- **macOS:** `Release/Chromium Embedded Framework.framework/` — a framework bundle, no
  `.so`. CEF requires an **`.app` bundle** with the framework in `Contents/Frameworks/`,
  the framework **loaded dynamically** at runtime (`CefScopedLibraryLoader`, compiled from
  `cef_scoped_library_loader_mac.mm`), and **separate Helper `.app` bundles** for the
  render/GPU/etc. subprocesses (the main app cannot be its own helper). This is the
  canonical `cefsimple`-on-macOS layout.

Good news from the source recon: the app logic is largely portable already —
**no X11**, clipboard uses terminal **OSC‑52 escapes**, terminal handling is POSIX
`termios`/`ioctl`. The port is concentrated in **CEF packaging**, not app logic.

## CEF versions (per platform, same Chromium)

The build hash in the CEF version string differs per platform, but all target the **same
Chromium `148.0.7778.96`**, so `CMakeLists.txt` / sources stay consistent.

| Platform slug | CEF version |
|---------------|-------------|
| `linux64` / `linuxarm64` | `148.0.7+g5b12d32+chromium-148.0.7778.96` |
| `macosarm64` | `148.0.8+g18e00ea+chromium-148.0.7778.96` |
| `macosx64` | `148.0.8+g18e00ea+chromium-148.0.7778.96` |

Builds come from `https://cef-builds.spotifycdn.com/` (beta channel, `_minimal`). To find
the matching mac build for a new Chromium version, query
`https://cef-builds.spotifycdn.com/index.json` (keys: `macosarm64`, `macosx64`).

## Work log

### Step 1 — `download_cef.sh` (DONE)
Made the script OS-aware via `uname -s` / `uname -m`:
- Linux path unchanged (same version, `linux64`/`linuxarm64`, `sha1sum`, `nproc`).
- macOS (`Darwin`): selects `macosarm64`/`macosx64`, uses `shasum -a 1` and
  `sysctl -n hw.logicalcpu`, passes `-DPROJECT_ARCH=arm64`/`x86_64` to the wrapper build.
- Verified end-to-end on `arm64`: downloads ~121 MB, checksum OK, `libcef_dll_wrapper.a`
  builds cleanly.

### Step 2 — App code + CMake bundle (DONE)
- **`src/platform_paths.h`** (new): `platform::executablePath()` / `executableDir()`
  (`_NSGetExecutablePath` on macOS, `readlink` on Linux) and `resourceDir()`
  (`Contents/Resources` on macOS, exe dir on Linux). Replaced the 4 `/proc/self/exe`
  sites in `main.cpp`, `browser_client.cpp`, `input_handler.cpp`, `user_scripts.cpp`.
- **`main.cpp`**: on macOS, load the framework at the top of `main()` via
  `CefScopedLibraryLoader::LoadInMain()`; skip `browser_subprocess_path` /
  `resources_dir_path` / `locales_dir_path` (macOS finds helpers + resources
  automatically inside the bundle/framework).
- **`src/process_helper_mac.mm`** (new): tiny helper entry point —
  `LoadInHelper()` + `CefExecuteProcess(nullptr)`.
- **`src/sixel_renderer.cpp`**: `std::__gcd` (libstdc++ internal) → `std::gcd`
  (`<numeric>`, C++17) — libc++ has no `__gcd`.
- **`CMakeLists.txt`**: `if(OS_MAC)` branch builds `brow6el.app` (MACOSX_BUNDLE),
  links only the wrapper `.a` + `CEF_STANDARD_LIBS` (framework loaded at runtime),
  runs `COPY_MAC_FRAMEWORK`, copies resources into `Contents/Resources`, and loops
  over `CEF_HELPER_APP_SUFFIXES` to build + embed the 5 Helper `.app`s. Linux path
  unchanged. `link_directories(${SIXEL_LIBRARY_DIRS})` so Homebrew libsixel is found.
- **`mac/Info.plist.in`, `mac/helper-Info.plist.in`, `mac/run_brow6el_mac.sh.in`**
  (new): plist templates + a launcher that execs the in-bundle binary directly (keeps
  the tty attached — required for a terminal app; `open` would detach it).
- **`build.sh`**: OS-aware (portable CPU count, skip Linux `libcef.so` strip,
  pass `-DPROJECT_ARCH` on mac).
- **Verified**: `build/brow6el.app` builds; `--version` loads the framework;
  a forced-graphics run spawns Helper subprocesses, initializes CEF, and loads
  `https://example.com` (status 200) with bundled JS injected.

### Step 3 — Polish (DONE)
- `src/version.h.in`: macOS User-Agent (`Macintosh; Intel Mac OS X 10_15_7`, matching
  Chrome's frozen platform token) behind `#ifdef __APPLE__`; Linux UA unchanged.
- `README.md`: added macOS (Homebrew) build dependencies + notes.
- Render test confirmed by the user on a real terminal (yahoo.co.jp rendered fine).

### Step 4 — Keychain prompt + scroll fixes (DONE)
- **macOS Keychain prompt** (`src/browser_app.h`): Chromium stores its cookie/password
  encryption key in the login Keychain ("Chrome Safe Storage"). Because brow6el is
  ad-hoc code-signed and **re-signed on every build**, macOS treats each build as a new
  app and repeatedly prompts for the Keychain password on startup. Added
  `command_line->AppendSwitch("use-mock-keychain")` under `#ifdef __APPLE__` so no OS
  Keychain access is needed (brow6el defaults to a temporary profile anyway).
  - **Important gotcha:** this password dialog also *blocks* `CefInitialize` in headless
    / background runs — helpers stall at 1 and pages never load. If automated testing
    ever hangs at startup again, suspect a blocking Keychain (or other GUI) prompt.
- **Scrolling** (`src/input_handler.*`): `j`/`k` and the `↑`/`↓` arrows were sending
  arrow key events to the page, which don't scroll in offscreen rendering (they arrive
  as if to an editable field — `sendKeyEvent` hardcodes `focus_on_editable_field = 1`).
  Changed `j`/`k` and STANDARD-mode arrows to `SendMouseWheelEvent` (via a new
  `getLineScrollAmount()`), matching how `g`/`t` already scrolled. This is a shared
  (non-macOS-guarded) fix — it improves Linux too. Verified: `g`, `t`, `j`, `k`, `↑`,
  `↓` all repaint the viewport on a tall page.

### Step 5 — macOS special-key fix (Backspace/Enter/Tab/arrows in forms) (DONE)
- **Symptom:** in a text field, typing worked but **Backspace and Ctrl-H did nothing**.
- **Cause** (`src/input_handler.cpp`, `sendKeyEvent`): `native_key_code` was set to the
  **Windows** virtual-key code. On macOS `CefKeyEvent.native_key_code` is a **macOS**
  (Carbon `kVK_*`) keycode. `VK_BACK` is `0x08`, but macOS keycode `8` is the physical
  **'C'** key — so Chromium saw Backspace as `keyCode=67, key=Unidentified` and never ran
  delete-backward. All synthesized special keys were mis-mapped (`VK_UP 0x26` → macOS 'j',
  etc.).
- **Fix:** added `MacNativeKeyCode()` (VK → macOS keycode) and, under `#ifdef __APPLE__`,
  set `native_key_code` from it in the special-key path. Verified: a focused field goes
  `HELLO → HELL → HEL → HE` on Backspace/Ctrl-H, with `key=Backspace keyCode=8`. Also
  repairs Enter/Tab/Escape/Delete/arrow keys in forms on macOS. Character typing is
  unaffected (it uses CHAR events + the `character` field).

### Step 6 — Distribution via Homebrew tap (DONE)
Users install with:

```sh
brew install mkasa/brow6el/brow6el
```

- Tap repo: **`github.com/mkasa/homebrew-brow6el`** (`Formula/brow6el.rb`).
- **Build-from-source** formula: declares `libsixel` (+ `cmake`, `pkg-config`) as
  deps, downloads the arch-matched CEF as a checksummed `resource` (arm64 + Intel),
  builds the wrapper + `brow6el.app`, installs a `brow6el` launcher on `PATH`.
  Building locally means **no notarization** is needed (Gatekeeper doesn't quarantine
  locally-built binaries).
- The `mkasa/brow6el` repo was made **public** and tagged **`v0.4.0`** (the formula
  pulls that tag tarball).
- macOS packaging gotchas solved in the formula:
  - Stage CEF under its real chromium-versioned dir name so CMake's
    `cef_binary*chromium*` glob reports the right Chromium version (else the UA
    falls back to `143`).
  - `-DBROW6EL_VERSION_OVERRIDE=#{version}` (source tarballs have no `.git`).
  - `preserve_rpath` + re-signing CEF's bundled dylibs (`libEGL`/`libGLESv2`/
    `libcef_sandbox`): their install IDs are relative (`./libEGL.dylib`) and
    Homebrew's linkage fixer can't rewrite them to the too-long opt path. We set
    their IDs to `@rpath/...`, re-sign ad-hoc, and `preserve_rpath` tells Homebrew
    to leave `@rpath` IDs alone.
- Verified end-to-end from the published tap (fresh `brew untap`/`tap`/`install`):
  exit 0, no linkage errors, renders pages.

## Remaining work

The port and its distribution are complete. Optional polish only:

1. **Universal binary** — currently built for the host arch only.
2. **Merge `macos-port` → `main`** so the public repo's default branch shows the
   macOS support (currently the work lives on the `macos-port` branch + `v0.4.0` tag).

## Build (macOS)

```bash
./download_cef.sh                       # OS-aware; fetches macOS CEF + builds wrapper
./build.sh                              # produces build/brow6el.app
./build/run_brow6el.sh https://example.com   # run in a Sixel/Kitty terminal
```

## Keeping in sync with upstream

```bash
git fetch upstream               # codeberg original
git merge upstream/main          # or rebase
```
Remotes: `origin` = GitHub fork (push here), `upstream` = Codeberg original.
