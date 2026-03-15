# CLAUDE.md — Project Guide for MAC48GX

## What is this project?

MAC48GX is a macOS calculator emulator based on droid48/x48. The Android JNI/UI layer is replaced with SDL2. The C emulator core from `app/src/main/jni/` is compiled unchanged (with minimal `#ifdef MAC_BUILD` guards).

## Build

```bash
cd macos && make              # build binary (native arch)
cd macos && make bundle       # create MAC48GX.app
cd macos && make dmg          # create DMG installer
cd macos && make test         # run 148 headless tests
./macos/build/MAC48GX --test  # run tests with visible GUI
./macos/build/MAC48GX --test --delay 30  # with countdown
```

Native build requires: `brew install sdl2 sdl2_ttf`

### Universal binary (x86_64 + arm64)

Homebrew SDL2 is single-arch. For a universal build, use the official prebuilt SDL2 framework DMGs from the SDL GitHub releases (they contain fat binaries):

```bash
curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.dmg
curl -LO https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-2.24.0.dmg
hdiutil attach SDL2-2.32.10.dmg -mountpoint /Volumes/SDL2
hdiutil attach SDL2_ttf-2.24.0.dmg -mountpoint /Volumes/SDL2_ttf

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

hdiutil detach /Volumes/SDL2 && hdiutil detach /Volumes/SDL2_ttf
```

## Project structure

- `macos/` — All Mac-specific code:
  - `main_sdl.c` — SDL2 main loop, rendering, audio, events, overlays, --test mode
  - `x48_sdl.c` — button table, thread-safe event queue, `GetEvent()`, X11 stubs
  - `lcd_mac.c` — LCD display buffer (lcd.c without JNI)
  - `test_harness.c` — headless test runner (no SDL2 dependency)
  - `test_cases.h` — shared test functions (148 tests)
  - `Info.plist` — macOS app bundle metadata
  - `MAC48GX.icns` — app icon
  - `Makefile` — targets: `all`, `bundle`, `dmg`, `test`, `clean`, `run`
- `app/src/main/jni/` — Emulator core (shared with Android). **Minimize changes** — use `#ifdef MAC_BUILD`.
- `app/src/main/assets/Asana-Math.ttf` — Math font for overlay symbols
- Data files: `~/.droid48/` at runtime; `app/src/main/jni/rom` and `ram` are source assets

## Key architecture rules

### Threading
- **Never call `button_pressed()` or `do_kbd_int()` from the SDL thread.** Use `sdl_push_event()`.
- **Never call `pthread_mutex_lock` from a signal handler.** SIGALRM only sets `got_alarm = 1`.
- `blockConditionVariable()` uses `pthread_cond_timedwait` (20ms).

### Display
- Emulator writes RGB565 to `disp_buf_short[]` / `disp_buf_header_short[]`
- SDL render loop copies to texture only when `flipable` is set
- `update_display()` called by emulator via `check_devices()` — never from render thread

### Audio
- SDL2 audio callback generates square wave from `device.speaker_counter`
- `update_audio()` runs ~60x/sec in main thread, computes frequency

### Overlays
- Cmd+K: keyboard help, Cmd+I: about — skip `render()` when active
- Both draw on cleared screen with own `SDL_RenderPresent`

### Files that differ between Android and Mac
| Android | Mac replacement | Purpose |
|---------|----------------|---------|
| `jni/main.c` | `macos/main_sdl.c` | Entry point, timing, UI, audio |
| `jni/x48.c` | `macos/x48_sdl.c` | Buttons, events, X11 stubs |
| `jni/lcd.c` | `macos/lcd_mac.c` | LCD without JNI functions |

### Compiler flags
- `-DMAC_BUILD=1 -DLINUX=1 -DSYSV_TIME=1`
- `-DAPP_VERSION='"$(VERSION)"'` — version from git tags
- Warning suppressions for old C code

## Testing

### Architecture
- `test_cases.h` — shared by `test_harness.c` (headless) and `main_sdl.c` (GUI --test)
- Tests send button presses via `sdl_push_event()`, read RPL stack via `decode_rpl_obj_2()`
- Test state in `~/.droid48-test/` (separate from user state)

### Coverage (148 tests)
- Arithmetic, decimals, negatives
- Powers, roots, squaring, reciprocal
- Trig (DEG), inverse trig, trig identities, roundtrips
- Logarithms, exponentials, log/power identities
- Angle modes (DEG/RAD toggle, π constant)
- ATAN2 via ATAN, polar/rectangular conversions
- Stack manipulation (SWAP, DROP, DUP, CLEAR)
- EEX scientific notation
- MTH PROB menu (COMB, PERM, factorial)
- Shifted functions (LS+key, RS+key)
- Edge cases

### Known limitation
Alpha keyboard text entry timing is unreliable for automated tests. Matrix/vector/HYP/REAL menu tests require manual verification — see MANUAL_TESTS.md.

## Common tasks

### Changing window size
Adjust `SC_NUM`/`SC_DEN` (horizontal: 2/1=2x) and `SCV_NUM`/`SCV_DEN` (vertical: 13/10=1.3x).
Adjust `BTN_MARGIN` for left/right padding.

### Adding a test
Add `static void test_xxx(void)` in `test_cases.h`, then add to `run_all_tests()`.

### Adding a keyboard shortcut
Edit `keysym_to_button()` in `main_sdl.c`. Map `SDL_Keycode` → button index (0-48).

### Changing button appearance
Edit `render()` in `main_sdl.c`. Colors, rounded rect radius, fonts.

### Fonts
- Button text: Helvetica bold (22/30/17pt)
- Shift labels: Asana-Math 16pt (has ∂ ∫ Σ π ∠ → ↵)
- Title: HelveticaNeue bold 14pt
