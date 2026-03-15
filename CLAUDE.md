# CLAUDE.md — Project Guide for droid48-mac

## What is this project?

A macOS port of droid48, an HP-48GX calculator emulator. The Android JNI/UI layer is replaced with SDL2. The C emulator core from `app/src/main/jni/` is compiled unchanged (with minimal `#ifdef MAC_BUILD` guards).

## Build

```bash
cd macos && make          # build the app
cd macos && make test     # run 100 headless tests
./macos/build/droid48-mac --test   # run tests with visible GUI
```

Requires: `brew install sdl2 sdl2_ttf`

Binary: `macos/build/droid48-mac`

## Project structure

- `macos/` — All Mac-specific code:
  - `main_sdl.c` — SDL2 main loop, rendering, audio, event handling, `--test` mode
  - `x48_sdl.c` — button table, thread-safe event queue, `GetEvent()`, X11 stubs
  - `lcd_mac.c` — LCD display buffer (lcd.c without JNI)
  - `test_harness.c` — headless test runner (no SDL2 dependency)
  - `test_cases.h` — shared test functions (100 tests)
  - `Makefile` — targets: `all`, `test`, `clean`, `run`
- `app/src/main/jni/` — Emulator core (shared with Android). **Minimize changes here** — use `#ifdef MAC_BUILD` guards for any platform-specific code.
- `app/src/main/jni/x48.h` — Shared header with `MAC_BUILD` guards for Android vs Mac includes
- Data files: `~/.droid48/` at runtime; `app/src/main/jni/rom` and `ram` are source assets

## Key architecture rules

### Threading
- **Never call `button_pressed()` or `do_kbd_int()` from the SDL thread.** These modify `saturn.PC` and cause race conditions with the SHUTDN handler. Use `sdl_push_event()` to queue events for the emulator thread.
- **Never call `pthread_mutex_lock` from a signal handler.** The SIGALRM handler must only set `got_alarm = 1`.
- `blockConditionVariable()` uses `pthread_cond_timedwait` (20ms) — not indefinite wait.

### Display
- The emulator writes RGB565 pixels to `disp_buf_short[]` and `disp_buf_header_short[]` (defined in `lcd_mac.c`)
- The SDL render loop copies these buffers to a texture every frame
- `update_display()` is called by the emulator via `check_devices()` — do not call it from the render thread

### Audio
- SDL2 audio callback in `main_sdl.c` generates a square wave from `device.speaker_counter`
- `update_audio()` runs in the main thread ~60x/sec, reads the counter and computes frequency
- The audio callback reads `audio_delta` (volatile) — written by main thread, read by audio thread

### Files that differ between Android and Mac
| Android | Mac replacement | Purpose |
|---------|----------------|---------|
| `jni/main.c` | `macos/main_sdl.c` | Entry point, timing, UI, audio |
| `jni/x48.c` | `macos/x48_sdl.c` | Buttons, events, X11 stubs |
| `jni/lcd.c` | `macos/lcd_mac.c` | LCD without JNI functions |

### Compiler flags
- `-DMAC_BUILD=1` — activates Mac code paths in shared headers
- `-DLINUX=1 -DSYSV_TIME=1` — enables Linux-compatible time functions
- Warning suppressions for old C code: `-Wno-implicit-function-declaration` etc.
- Include order: `macos/` headers do NOT override `jni/` headers; `x48.h` uses `#ifdef` internally

## Testing

### Test architecture
- `test_cases.h` — shared test functions included by both `test_harness.c` (headless) and `main_sdl.c` (GUI `--test` mode)
- Tests boot the emulator, send button presses via `sdl_push_event()`, wait for computation, then read the RPL stack via `decode_rpl_obj_2()` and compare to expected values
- Test state is stored in `~/.droid48-test/` (separate from user state)

### Test coverage (100 tests)
- Basic arithmetic (+, -, *, /, repeating decimals)
- Decimal and negative numbers
- Powers, roots, squaring (y^x, sqrt, x^2, 1/x)
- Trigonometry (sin, cos, tan at key angles in DEG mode)
- Inverse trig (asin, acos, atan) and roundtrips
- Trig identities (sin^2+cos^2, complementary angles)
- Logarithms (log, ln) and exponentials (10^x, e^x)
- Log/power identities (10^log(x)=x, e^ln(x)=x)
- ATAN2 via ATAN (all quadrant angles, roundtrips)
- Polar/rectangular conversions (magnitude, angle, r*cos/sin)
- Stack operations (multi-level push, level verification)
- Large numbers, chained operations, edge cases

### Known test limitation
Alpha keyboard text entry (needed for commands like ABS, DET, IDN) has timing issues with the emulator's event queue. Matrix/vector tests are scaffolded but deferred.

## Common tasks

### Adding a new keyboard shortcut
Edit `keysym_to_button()` in `macos/main_sdl.c`. Map an `SDL_Keycode` to a button index (0-48).

### Changing button appearance
Edit the `render()` function in `macos/main_sdl.c`. Button colors, labels, and shift annotations are all drawn there.

### Changing window size
Adjust `SC_NUM`/`SC_DEN` (unified scale factor) in `macos/main_sdl.c`. Current: 8/5 = 1.6x.

### Adding a new test
Add a `static void test_xxx(void)` function in `macos/test_cases.h` using the helpers:
- `type_number("123.45")` — types digits/period
- `press_key(BTN_PLUS)` — presses a button
- `lshift_key(BTN_SIN)` — left-shift + key (e.g., ASIN)
- `rshift_key(BTN_INV)` — right-shift + key (e.g., LN)
- `check_result("test name", "expected")` — reads stack level 1 and compares
- `drop()` — drops stack level 1
- `clear_stack()` — clears entire stack (with error recovery)

Then add `test_xxx(); clear_stack();` to `run_all_tests()`.

### Debugging emulator issues
Add `fprintf(stderr, ...)` in the relevant file. Key debug points:
- `do_shutdown()` in `actions.c` — SHUTDN/idle handler
- `GetEvent()` in `x48_sdl.c` — button event processing
- `update_display()` in `lcd_mac.c` — display refresh
- `schedule()` in `emulate.c` — periodic emulator housekeeping
