# CLAUDE.md — Project Guide for droid48-mac

## What is this project?

A macOS port of droid48, an HP-48GX calculator emulator. The Android JNI/UI layer is replaced with SDL2. The C emulator core from `app/src/main/jni/` is compiled unchanged (with minimal `#ifdef MAC_BUILD` guards).

## Build

```bash
cd macos && make
```

Requires: `brew install sdl2 sdl2_ttf`

Binary: `macos/build/droid48-mac`

## Project structure

- `macos/` — All Mac-specific code (main_sdl.c, x48_sdl.c, lcd_mac.c, Makefile)
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

### Files that differ between Android and Mac
| Android | Mac replacement | Purpose |
|---------|----------------|---------|
| `jni/main.c` | `macos/main_sdl.c` | Entry point, timing, UI |
| `jni/x48.c` | `macos/x48_sdl.c` | Buttons, events, X11 stubs |
| `jni/lcd.c` | `macos/lcd_mac.c` | LCD without JNI functions |

### Compiler flags
- `-DMAC_BUILD=1` — activates Mac code paths in shared headers
- `-DLINUX=1 -DSYSV_TIME=1` — enables Linux-compatible time functions
- Warning suppressions for old C code: `-Wno-implicit-function-declaration` etc.
- Include order: `macos/` headers do NOT override `jni/` headers; `x48.h` uses `#ifdef` internally

## Common tasks

### Adding a new keyboard shortcut
Edit `keysym_to_button()` in `macos/main_sdl.c`. Map an `SDL_Keycode` to a button index (0-48).

### Changing button appearance
Edit the `render()` function in `macos/main_sdl.c`. Button colors, labels, and shift annotations are all drawn there.

### Changing window size
Adjust `SC_NUM`/`SC_DEN` (unified scale factor) in `macos/main_sdl.c`. Current: 8/5 = 1.6x.

### Debugging emulator issues
Add `fprintf(stderr, ...)` in the relevant file. Key debug points:
- `do_shutdown()` in `actions.c` — SHUTDN/idle handler
- `GetEvent()` in `x48_sdl.c` — button event processing
- `update_display()` in `lcd_mac.c` — display refresh
- `schedule()` in `emulate.c` — periodic emulator housekeeping
