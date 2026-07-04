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
| Source portability (X11, clipboard, terminal I/O) | ✅ Mostly portable — see below |
| `/proc/self/exe` → macOS exe-path shim | ⬜ TODO (4 call sites) |
| CMake macOS `.app` bundle + Helper processes | ⬜ TODO (the big one) |
| Launch path (`run_brow6el.sh`) | ⬜ TODO |
| End-to-end run in a Sixel/Kitty terminal | ⬜ Not yet attempted |

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

## Remaining work (next steps)

1. **Exe-path shim** — `/proc/self/exe` is Linux-only. Used in `main.cpp`,
   `browser_client.cpp`, `input_handler.cpp`, `user_scripts.cpp`. Add one helper
   (`_NSGetExecutablePath()` on macOS, `readlink` on Linux) and replace the 4 call sites.
2. **CMake macOS branch** — build `brow6el.app`: link the framework, generate a small
   helper `main()` and produce the Helper `.app` bundles, write Info.plists, copy the
   framework into `Contents/Frameworks/`. Guard all of this behind `if(APPLE)` so Linux
   stays on its current `libcef.so` path.
3. **Launch** — on macOS run the app binary directly (no `LD_LIBRARY_PATH`); keep the
   Linux `run_brow6el.sh` behavior intact.
4. **Run test** — launch in a Sixel/Kitty-capable macOS terminal (iTerm2, WezTerm, kitty,
   Ghostty) and iterate on runtime issues.

## Build (once the port is further along)

```bash
./download_cef.sh    # now macOS-aware
./build.sh           # macOS branch: TODO
# run: TODO (app bundle)
```

## Keeping in sync with upstream

```bash
git fetch upstream               # codeberg original
git merge upstream/main          # or rebase
```
Remotes: `origin` = GitHub fork (push here), `upstream` = Codeberg original.
