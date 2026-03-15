# droid48-mac

A macOS port of [droid48](https://github.com/shagr4th/droid48), an HP-48GX calculator emulator. The original droid48 is an Android app based on the classic [x48](http://x48.berlios.de/) emulator by Eddie C. Dost.

This port compiles the C emulator core natively for macOS and replaces the Android JNI/UI layer with an SDL2-based application.

![HP-48GX Emulator](https://img.shields.io/badge/HP--48GX-Emulator-teal)

## Features

- Full HP-48GX Saturn CPU emulation (unchanged C core from x48/droid48)
- SDL2 window styled after the real HP-48GX calculator face
- Labeled buttons with left-shift (green), right-shift (purple), and alpha (green) annotations
- Mouse click and keyboard input
- Sound output (speaker beeps and tones via SDL2 audio)
- HiDPI/Retina display support
- Calculator state saved to `~/.droid48/` on quit
- SIGALRM-based emulator timing (50 Hz)
- Automated test suite (100 tests) with headless and visual modes

## Prerequisites

- macOS (tested on macOS Sonoma)
- Xcode Command Line Tools (`xcode-select --install`)
- [Homebrew](https://brew.sh)
- SDL2 and SDL2_ttf:

```bash
brew install sdl2 sdl2_ttf
```

## Building

```bash
cd macos
make
```

The binary is built to `macos/build/droid48-mac`. ROM and RAM files are automatically copied next to the binary.

### CMake (alternative)

```bash
cd macos
mkdir cmake-build && cd cmake-build
cmake ..
make
```

## Running

```bash
./macos/build/droid48-mac
```

On first launch, ROM and RAM files are copied to `~/.droid48/`. If no ROM is found, you will need to place a valid HP-48 ROM image at `~/.droid48/rom`.

## Testing

### Headless (no GUI, for CI)

```bash
cd macos
make test
```

Runs 100 automated tests covering arithmetic, trig, inverse trig, logarithms, exponentials, powers, roots, polar conversions, atan2, stack operations, and edge cases. No SDL2 dependency — links only against the emulator core + pthread.

### Visual (with calculator GUI)

```bash
./macos/build/droid48-mac --test
```

Runs the same test suite with the calculator window visible, so you can watch button presses and stack results in real time. The window title shows "TEST MODE" and the app exits when tests complete.

## Keyboard Shortcuts

| Key | Calculator Button |
|-----|-------------------|
| `0`-`9` | Number keys |
| `+` `-` `*` `/` | Arithmetic |
| `Enter` | ENTER |
| `Backspace` | Backspace |
| `Delete` | DEL |
| `Escape` | ON |
| Arrow keys | Cursor keys |
| `F1`-`F6` | Menu keys A-F |
| `s` `c` `t` | SIN, COS, TAN |
| `a` | ALPHA |
| `e` | EEX |
| `n` | +/- (negate) |
| `Space` | SPC |
| `.` | Decimal point |

## Architecture

```
macos/
├── main_sdl.c      SDL2 main loop, timing, rendering, audio, event handling
├── x48_sdl.c       Button table, thread-safe event queue, GetEvent()
├── lcd_mac.c       LCD display buffer management (lcd.c without JNI)
├── test_harness.c  Headless test runner (no SDL2 dependency)
├── test_cases.h    Shared test functions (used by both test modes)
├── Makefile        Build system (targets: all, test, clean)
└── CMakeLists.txt  Alternative CMake build

app/src/main/jni/   Emulator core (from droid48, compiled unchanged)
├── emulate.c       Saturn CPU instruction execution
├── actions.c       CPU actions (SHUTDN, interrupts, etc.)
├── memory.c        Memory management and I/O dispatch
├── device.c        Hardware device simulation + speaker
├── lcd.c           LCD controller (Android version, not used on Mac)
├── init.c          State file loading/saving
├── timer.c         Hardware timer emulation
├── rpl.c           RPL object decoding (used by test harness)
└── ...             Other core files
```

### Threading Model

- **Main thread**: SDL2 event loop, rendering (~60 fps), and audio update
- **Emulator thread**: Runs the Saturn CPU via `emulate()` in a tight loop
- **Audio thread**: SDL2 audio callback generates square wave from speaker state
- **Test thread** (--test mode): Sends button events and reads RPL stack results
- **Communication**: Thread-safe event queue for button presses; emulator thread processes events in `GetEvent()` to avoid race conditions on CPU state
- **Timing**: `SIGALRM` at 50 Hz sets `got_alarm` flag; `blockConditionVariable()` uses `pthread_cond_timedwait` (20 ms timeout) for the SHUTDN idle handler

### Key Design Decisions

- Button events are **only processed in the emulator thread** via the event queue. Direct manipulation of `saturn.PC` from the SDL thread caused race conditions with the SHUTDN handler.
- The SIGALRM handler only sets a flag (`got_alarm = 1`). It does **not** call `pthread_mutex_lock` (which is not async-signal-safe).
- The LCD buffer (`disp_buf_short[]`, RGB565 format) is written by the emulator thread and read by the render thread without locking — safe because writes are atomic at the pixel level and tearing is acceptable.
- Sound is generated via an SDL2 audio callback that reads a volatile frequency delta set by the main thread from `device.speaker_counter`.

## ROM Files

This emulator requires an HP-48 ROM image. The ROM included in the repository (`app/src/main/jni/rom`) is from the original droid48 project. HP-48 ROM images can be obtained from:

- Dumping your own HP-48 calculator
- The [HP Museum](https://www.hpmuseum.org/)

## Credits and Acknowledgements

This project builds on the work of several authors and open-source projects:

### x48 — The Original Emulator
- **Eddie C. Dost** (ecd@dressler.de) — Author of [x48](http://x48.berlios.de/), the original HP-48 emulator for X11, first released in 1994. The Saturn CPU emulation core, memory management, device simulation, LCD rendering, RPL object decoding, and debugger are all his work. Copyright (C) 1994-2005 Eddie C. Dost. Licensed under the GNU General Public License v2 or later.
- **G. Allen Morris III** — Maintained x48 and contributed build system updates (config.h, autotools).

### droid48 — The Android Port
- **Arnaud Brochard** (shagr4th) — Author of [droid48](https://github.com/shagr4th/droid48), the Android port of x48. Adapted the X11 display and input layer to Android via JNI, added touch input, LCD rendering to Android canvas, and audio support. Licensed under GPL-3.0.
- **Denis Bernard** — Contributor to droid48.

### droid48-mac — This macOS Port
- Replaces the Android JNI/UI layer with SDL2 for macOS.
- All new code in `macos/` is also licensed under GPL-3.0.

### Other Acknowledgements
- **Jamie Zawinski** (jwz@lucid.com) — X resource handling code in `resources.c`, from xscreensaver. Copyright (c) 1992, used with permission (MIT-style license).
- **SDL2** by the [SDL Project](https://www.libsdl.org/) — Cross-platform multimedia library used for display, input, and audio. Licensed under the zlib license.
- **SDL2_ttf** — TrueType font rendering for SDL2. Licensed under the zlib license.

## License

This project is licensed under the **GNU General Public License v3.0** (GPL-3.0), consistent with the droid48 upstream project. The original x48 emulator core is licensed under GPL v2 or later.

See [COPYING](COPYING) for the full GPL-3.0 license text.

The emulator core source files in `app/src/main/jni/` retain their original copyright headers from Eddie C. Dost (GPL v2+). The macOS port files in `macos/` are new code licensed under GPL-3.0.
