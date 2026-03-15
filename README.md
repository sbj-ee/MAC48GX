# droid48-mac

A macOS port of [droid48](https://github.com/shagr4th/droid48), an HP-48GX calculator emulator. The original droid48 is an Android app based on the classic [x48](http://x48.berlios.de/) emulator by Eddie C. Dost.

This port compiles the C emulator core natively for macOS and replaces the Android JNI/UI layer with an SDL2-based application.

![HP-48GX Emulator](https://img.shields.io/badge/HP--48GX-Emulator-teal)

## Features

- Full HP-48GX Saturn CPU emulation (unchanged C core from x48/droid48)
- SDL2 window with HP-48GX calculator face
- Labeled buttons with left-shift (green), right-shift (purple), and alpha (green) annotations
- Mouse click and keyboard input
- HiDPI/Retina display support
- Calculator state saved to `~/.droid48/` on quit
- SIGALRM-based emulator timing (50 Hz)

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

## Keyboard Shortcuts

| Key | Calculator Button |
|-----|-------------------|
| `0`-`9` | Number keys |
| `+` `-` `*` `/` | Arithmetic |
| `Enter` | ENTER |
| `Backspace` | Backspace (←) |
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
├── main_sdl.c     SDL2 main loop, timing, rendering, event handling
├── x48_sdl.c      Button table, thread-safe event queue, GetEvent()
├── lcd_mac.c      LCD display buffer management (lcd.c without JNI)
├── Makefile        Build system
└── CMakeLists.txt  Alternative CMake build

app/src/main/jni/  Emulator core (from droid48, compiled unchanged)
├── emulate.c      Saturn CPU instruction execution
├── actions.c      CPU actions (SHUTDN, interrupts, etc.)
├── memory.c       Memory management and I/O dispatch
├── device.c       Hardware device simulation
├── lcd.c          LCD controller (Android version, not used on Mac)
├── init.c         State file loading/saving
├── timer.c        Hardware timer emulation
└── ...            Other core files
```

### Threading Model

- **Main thread**: SDL2 event loop and rendering (~60 fps)
- **Emulator thread**: Runs the Saturn CPU via `emulate()` in a tight loop
- **Communication**: Thread-safe event queue for button presses; emulator thread processes events in `GetEvent()` to avoid race conditions on CPU state
- **Timing**: `SIGALRM` at 50 Hz sets `got_alarm` flag; `blockConditionVariable()` uses `pthread_cond_timedwait` (20 ms timeout) for the SHUTDN idle handler

### Key Design Decisions

- Button events are **only processed in the emulator thread** via the event queue. Direct manipulation of `saturn.PC` from the SDL thread caused race conditions with the SHUTDN handler.
- The SIGALRM handler only sets a flag (`got_alarm = 1`). It does **not** call `pthread_mutex_lock` (which is not async-signal-safe).
- The LCD buffer (`disp_buf_short[]`, RGB565 format) is written by the emulator thread and read by the render thread without locking — safe because writes are atomic at the pixel level and tearing is acceptable.

## ROM Files

This emulator requires an HP-48 ROM image. The ROM included in the repository (`app/src/main/jni/rom`) is from the original droid48 project. HP-48 ROM images can be obtained from:

- Dumping your own HP-48 calculator
- The [HP Museum](https://www.hpmuseum.org/)

## Credits

- **x48** by Eddie C. Dost — original HP-48 emulator for X11 (1994)
- **droid48** by shagr4th — Android port of x48
- **droid48-mac** — macOS port using SDL2

## License

GPL-3.0 — see [COPYING](COPYING) for details.
