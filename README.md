# MAC48GX

A macOS calculator emulator based on [droid48](https://github.com/shagr4th/droid48) and the classic [x48](http://x48.berlios.de/) Saturn CPU emulator by Eddie C. Dost.

This port compiles the C emulator core natively for macOS and replaces the Android JNI/UI layer with an SDL2-based application styled after the original calculator.

## Features

- Full Saturn CPU emulation (unchanged C core from x48/droid48)
- SDL2 window with calculator face — rounded buttons, proper math symbols (÷ × ∂ ∫ Σ π ∠ →)
- Left-shift (purple) and right-shift (green) overlay labels using Asana-Math font
- Mouse click and keyboard input with hand cursor on buttons
- Sound output (speaker beeps and tones via SDL2 audio)
- Button press effect (physical 2px depression)
- Copy/paste: Cmd+C copies stack to clipboard, Cmd+V pastes numbers
- Keyboard shortcut overlay (Cmd+K) and About screen (Cmd+I)
- HiDPI/Retina display support
- Universal binary (x86_64 + arm64) — runs natively on Intel and Apple Silicon
- Native `.app` bundle and DMG installer
- Calculator state saved to `~/.droid48/` on quit
- 148 automated tests with headless and visual modes

## Prerequisites

- macOS (tested on macOS Sonoma)
- Xcode Command Line Tools (`xcode-select --install`)

## Building

### Native build (current architecture only)

Install SDL2 via [Homebrew](https://brew.sh):

```bash
brew install sdl2 sdl2_ttf
cd macos
make              # build binary
make bundle       # create MAC48GX.app
make dmg          # create DMG installer
make test         # run 148 headless tests
```

### Universal build (x86_64 + arm64)

Download the official prebuilt universal SDL2 framework DMGs from the SDL GitHub releases, then build with both architecture slices:

```bash
# Download official universal SDL2 frameworks
curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.dmg
curl -LO https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-2.24.0.dmg
hdiutil attach SDL2-2.32.10.dmg -mountpoint /Volumes/SDL2
hdiutil attach SDL2_ttf-2.24.0.dmg -mountpoint /Volumes/SDL2_ttf

# Build universal DMG
cd macos
make dmg \
  ARCH="-arch x86_64 -arch arm64" \
  "CFLAGS=-std=gnu11 -O2 -arch x86_64 -arch arm64 -I. -I../app/src/main/jni \
   -DMAC_BUILD=1 -DLINUX=1 -DSYSV_TIME=1 \
   -DAPP_VERSION='\"$(git describe --tags --abbrev=0)\"' \
   -I/Volumes/SDL2/SDL2.framework/Headers \
   -Wno-implicit-function-declaration -Wno-incompatible-pointer-types \
   -Wno-int-conversion -Wno-unused-variable -Wno-unused-function \
   -Wno-deprecated-non-prototype" \
  "LDFLAGS=-arch x86_64 -arch arm64 \
   -F/Volumes/SDL2 -framework SDL2 \
   -F/Volumes/SDL2_ttf -framework SDL2_ttf -lpthread"

hdiutil detach /Volumes/SDL2
hdiutil detach /Volumes/SDL2_ttf
```

Binary: `macos/build/MAC48GX`
App bundle: `macos/build/MAC48GX.app`
DMG: `macos/build/MAC48GX-<version>.dmg`

## Running

```bash
./macos/build/MAC48GX           # run directly
open macos/build/MAC48GX.app    # run as native app (no terminal)
```

On first launch, ROM and RAM files are copied to `~/.droid48/`.

## Testing

```bash
make test                              # headless (CI)
./macos/build/MAC48GX --test           # visual (watch tests run)
./macos/build/MAC48GX --test --delay 30  # visual with countdown
```

148 automated tests covering arithmetic, trig, inverse trig, hyperbolic (via menus), logarithms, exponentials, powers, roots, polar conversions, atan2, angle modes (DEG/RAD), EEX notation, stack manipulation (SWAP/DROP/DUP/CLEAR), probability (COMB/PERM/factorial via MTH menu), π constant, and edge cases.

See [MANUAL_TESTS.md](MANUAL_TESTS.md) for manual tests covering vectors, matrices, complex numbers, base modes, and display modes.

## Keyboard Shortcuts

| Key | Function | | Key | Function |
|-----|----------|---|-----|----------|
| `0`-`9` `.` `SPC` | Number entry | | `Cmd+C` | Copy stack to clipboard |
| `+ - * /` | Arithmetic | | `Cmd+V` | Paste number |
| `Enter` | ENTER / DUP | | `Cmd+K` | Keyboard help overlay |
| `Backspace` | Backspace / DROP | | `Cmd+I` | About overlay |
| `Delete` | DEL | | `Cmd+Q` | Quit |
| `Escape` | ON / CANCEL | | | |
| Arrow keys | Cursor / nav | | | |
| `F1`-`F6` | Menu keys A-F | | | |
| `s` `c` `t` | SIN COS TAN | | | |
| `a` `e` `n` | ALPHA / EEX / ±  | | | |

## Architecture

```
macos/
├── main_sdl.c      SDL2 main, rendering, audio, events, overlays
├── x48_sdl.c       Buttons, thread-safe event queue, GetEvent()
├── lcd_mac.c       LCD display buffer (lcd.c without JNI)
├── test_harness.c  Headless test runner (no SDL2)
├── test_cases.h    Shared test functions (148 tests)
├── Info.plist      macOS app bundle metadata
├── MAC48GX.icns    App icon
├── Makefile        Build: all, bundle, dmg, test, clean
└── CMakeLists.txt  Alternative CMake build

app/src/main/jni/   Emulator core (from droid48)
├── emulate.c       Saturn CPU instruction execution
├── actions.c       SHUTDN, interrupts, keyboard
├── memory.c        Memory management and I/O
├── device.c        Hardware + speaker
├── init.c          State file loading/saving
├── rpl.c           RPL object decoding (used by tests)
└── ...

app/src/main/assets/
├── Asana-Math.ttf  Math symbol font for overlay labels
└── ...
```

### Threading Model

- **Main thread**: SDL2 event loop, rendering (~60 fps), audio update
- **Emulator thread**: Saturn CPU via `emulate()` in a tight loop
- **Audio thread**: SDL2 callback generates square wave from speaker
- **Test thread** (--test): sends button events, reads RPL stack

### Key Design Decisions

- Button events **only processed in the emulator thread** via event queue — prevents race conditions with SHUTDN handler
- SIGALRM handler only sets `got_alarm = 1` — async-signal-safe
- `blockConditionVariable()` uses `pthread_cond_timedwait` (20ms)
- LCD buffer only copied to texture when `flipable` is set

## Credits and Acknowledgements

### x48 — The Original Emulator
- **Eddie C. Dost** (ecd@dressler.de) — x48, the original emulator for X11 (1994-2005). Saturn CPU core, memory, devices, LCD, RPL, debugger. GPL v2+.
- **G. Allen Morris III** — x48 build system.

### droid48 — The Android Port
- **Arnaud Brochard** (shagr4th) — [droid48](https://github.com/shagr4th/droid48), Android port. JNI bridge, touch input, audio. GPL-3.0.
- **Denis Bernard** — droid48 contributor.

### MAC48GX — This macOS Port
- SDL2-based display, input, audio. Native .app bundle. 148 automated tests.
- All new code in `macos/` licensed under GPL-3.0.

### Other
- **Jamie Zawinski** — xscreensaver resource code (MIT).
- **SDL2** / **SDL2_ttf** — zlib license.
- **Asana-Math** — math symbol font from droid48 assets.

## License

**GNU General Public License v3.0** (GPL-3.0). Core emulator: GPL v2+.
See [COPYING](COPYING).
