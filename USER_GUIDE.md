# MAC48GX User Guide

MAC48GX is a macOS emulator for the Hewlett-Packard HP-48GX graphing calculator. It faithfully reproduces the calculator's Saturn CPU, display, keyboard, and speaker using SDL2.

## Installation

### From DMG (recommended)

1. Open the `MAC48GX-<version>.dmg` file
2. Drag **MAC48GX.app** to your Applications folder
3. Double-click to launch — macOS may ask you to approve the app in System Settings > Privacy & Security

### From source

```bash
brew install sdl2 sdl2_ttf
cd macos
make bundle
open build/MAC48GX.app
```

## First Launch

On first launch, MAC48GX creates `~/.droid48/` and copies the HP-48GX ROM image into it. The calculator boots to the standard HP-48GX home screen showing `{ HOME }` as the current directory. All calculator state (memory, settings, stored programs) is saved automatically when you quit and restored on the next launch.

## The Display

The HP-48GX display is divided into several areas:

```
┌──────────────────────────────┐
│ ◀ ▶ α (·) Σ ⇒   RAD        │  ← Annunciators
│ { HOME }                     │  ← Current directory
│ 4:                           │
│ 3:                           │  ← Stack (levels 1–4)
│ 2:                           │
│ 1:                           │
│                              │  ← Command line
│ [VECTR][MATR][LIST][HYP]...  │  ← Menu labels
└──────────────────────────────┘
```

- **Annunciators** appear at the top and indicate the calculator's state:
  - ◀ — Left-shift is active
  - ▶ — Right-shift is active
  - α — Alpha keyboard is active
  - (·) — Alert (appointment due or low battery)
  - Σ — Busy processing
  - ⇒ — Transmitting data
  - RAD — Radians angle mode (not shown in default DEG mode)
  - GRAD — Grads angle mode
- **Stack** shows up to four levels of stored values, numbered 1 (bottom) to 4 (top). The stack can hold hundreds of levels; only the bottom four are visible.
- **Command line** appears when you begin typing and shows your current input.
- **Menu labels** at the bottom correspond to the six white menu keys (A–F) at the top of the keyboard. Labels change depending on the active menu.

## Using the Calculator

### Mouse

Click any button on the calculator face to press it. The button depresses visually (2px shift) when clicked. The cursor changes to a pointing hand when hovering over a button.

### Keyboard

MAC48GX maps your Mac keyboard to calculator keys for fast input.

#### Number Entry

| Key | Calculator Button |
|-----|-------------------|
| `0`–`9` | Digit keys |
| `.` | Decimal point |
| `Space` | SPC (space/separator) |
| `e` | EEX (scientific notation exponent) |
| `n` | +/- (negate / change sign) |

To enter a negative number, type the digits first, then press `n` to negate. To enter scientific notation (e.g., 6.022 x 10^23), type `6.022`, press `e`, then type `23`.

#### Arithmetic

| Key | Calculator Button |
|-----|-------------------|
| `+` | + (add) |
| `-` | - (subtract) |
| `*` | x (multiply) |
| `/` | ÷ (divide) |
| `Enter` | ENTER (push onto stack / DUP) |

Numeric keypad equivalents (`+` `-` `*` `/` `Enter`) also work.

#### Navigation and Editing

| Key | Calculator Button |
|-----|-------------------|
| Arrow keys | Cursor movement |
| `Backspace` | ← (backspace / DROP) |
| `Delete` | DEL (delete character or clear stack) |
| `Escape` | ON / CANCEL |

When a cursor is displayed (editing the command line), the arrow and delete keys move/edit text. When no cursor is displayed (stack view), these keys perform stack operations:

| Key | No cursor (stack view) | With cursor (editing) |
|-----|------------------------|----------------------|
| ← | PICTURE (display current picture) | Move cursor left |
| → | SWAP (swap stack levels 1 and 2) | Move cursor right |
| ↑ | Interactive Stack | Move cursor up |
| ↓ | VIEW (view object on level 1) | Move cursor down |
| DEL | CLEAR (clear the stack) | Delete current character |
| ← (BS) | DROP (remove level 1) | Delete previous character |

#### Function Keys

| Key | Calculator Button |
|-----|-------------------|
| `F1`–`F6` | Menu keys A–F (soft keys) |
| `s` | SIN |
| `c` | COS |
| `t` | TAN |
| `a` | ALPHA (text entry mode) |

#### Command Shortcuts

| Shortcut | Action |
|----------|--------|
| `Cmd+C` | Copy stack level 1 to clipboard |
| `Cmd+V` | Paste number from clipboard |
| `Cmd+K` | Show/hide keyboard help overlay |
| `Cmd+I` | Show/hide About screen |
| `Cmd+Q` | Quit (saves state automatically) |

### Copy and Paste

**Cmd+C** copies the value at stack level 1 (the bottom line of the stack display) to the system clipboard as plain text. You can paste it into any other application.

**Cmd+V** types the clipboard contents into the calculator. It accepts digits, decimal points, and minus signs. Each character is simulated as a button press, so the number appears on the command line just as if you typed it.

## HP-48GX Basics

If you're new to the HP-48GX, here are the essentials. For the complete reference, see the [HP 48G Series User's Guide](https://www.hpcalc.org/hp48/docs/).

### RPN (Reverse Polish Notation)

The HP-48GX uses RPN by default: you enter operands first, then the operation. This eliminates the need for parentheses and follows the natural order of evaluation.

To calculate `3 + 4`:
1. Type `3`, press `Enter`
2. Type `4`
3. Press `+`
4. Result: `7`

To calculate `(3 + 4) x 2`:
1. Type `3`, press `Enter`
2. Type `4`, press `+`
3. Type `2`, press `*`
4. Result: `14`

To calculate `sin(45)` (in DEG mode):
1. Type `45`, press `Enter`
2. Press `s` (SIN)
3. Result: `.707106781187`

### The Stack

The stack is the HP-48's central workspace — a series of memory locations for numbers and other objects. Locations are called *levels*, numbered 1, 2, 3, etc. from the bottom up. The display shows levels 1 through 4; additional levels exist in memory but aren't normally visible.

As you enter new values, they go into level 1 and push existing data up to higher levels. When you use data from the stack (e.g., performing an operation), levels move down to fill in.

### Stack Operations

| Operation | How to Access | Effect |
|-----------|---------------|--------|
| DUP | `Enter` (when no command line is active) | Duplicate level 1 |
| DROP | `Backspace` (when no command line is active) | Remove level 1 |
| SWAP | `→` arrow (when no command line is active) | Swap levels 1 and 2 |
| CLEAR | `Delete` (when no command line is active) | Clear entire stack |
| UNDO | type UNDO or use command menu | Restore the stack to its state before the last operation |

### The Six Keyboard Layers

The HP-48 keyboard has six layers, each providing a different set of functions per key:

1. **Primary** — the labels printed on each key face (e.g., SIN, 7, ENTER)
2. **Left Shift** — purple labels above and to the left of each key (e.g., ASIN, SOLVE)
3. **Right Shift** — green labels above and to the right of each key (e.g., CHARS, MODES, PLOT)
4. **Alpha** — white letters to the lower-right of each key (A–Z, used for text entry)
5. **Alpha Left-Shift** — lowercase letters and special characters
6. **Alpha Right-Shift** — Greek letters and miscellaneous symbols

#### Using Shift Keys

- **Left Shift**: Click the purple left-shift key (◀), then the target key. The ◀ annunciator appears while the shift is active.
- **Right Shift**: Click the green right-shift key (▶), then the target key. The ▶ annunciator appears while the shift is active.
- To cancel a shift, press the same shift key again, or press the other shift key to switch.

Common shifted functions:

| Shifted Key | Function |
|-------------|----------|
| LS + SIN | ASIN (inverse sine) |
| LS + COS | ACOS (inverse cosine) |
| LS + TAN | ATAN (inverse tangent) |
| LS + √x | x² (square) |
| LS + y^x | LOG (common logarithm) |
| LS + 1/x | LN (natural logarithm) |
| LS + SPC | π (pi constant) |
| RS + 1/x | e^x (exponential) |
| RS + y^x | 10^x |

#### Applications (Right-Shift)

Twelve keys provide access to built-in applications when right-shifted:

| Key Combination | Application |
|-----------------|-------------|
| RS + CHARS (MTH) | Character catalog (256 characters) |
| RS + EQ LIB (PRG) | Equation Library (300+ equations, constants) |
| RS + I/O (CST) | Data transfer (Kermit, XMODEM) |
| RS + LIBRARY (VAR) | Library and plug-in card management |
| RS + MEMORY (↑) | Variable Browser |
| RS + MODES (NXT) | Calculator Modes (angle, display, beep, clock) |
| RS + PLOT (') | Plotting (15 plot types) |
| RS + SOLVE (STO) | Equation Solver |
| RS + STACK (EVAL) | Interactive Stack |
| RS + STAT (←) | Statistics and data analysis |
| RS + SYMBOLIC (↓) | Symbolic algebra and calculus |
| RS + TIME (→) | Alarm Browser and clock |

### Alpha Mode

Press `ALPHA` (or `a` on the keyboard) to activate text entry:

- **Press once** — type one character, then alpha mode turns off automatically
- **Press twice** — lock alpha mode (the α annunciator stays on); type as many characters as needed, then press `ALPHA` again to exit
- **Hold down** — type characters while held, releases when you let go

In alpha mode, keys produce the capital letters shown in white to their lower-right. The number keys still produce numbers. Left-shift + alpha gives lowercase letters; right-shift + alpha gives Greek letters and symbols.

### Entering Objects with Delimiters

The HP-48 stores different types of data as *objects*. Most require delimiters:

| Object Type | Delimiters | Example | Keys |
|-------------|------------|---------|------|
| Real number | none | `14.75` | type directly |
| Complex number | `( )` | `(8.25,12.1)` | LS + × |
| String | `" "` | `"Hello"` | RS + - |
| Array/vector | `[ ]` | `[ 4.8 -1.3 2.1 ]` | LS + ( |
| Program | `<< >>` | `<< DUP NEG >>` | LS + - |
| Algebraic | `' '` | `'A+B'` | ( key |
| List | `{ }` | `{ 6.8 5 "FIVE" }` | LS + + |
| Unit | `_` | `11.5_ft` | RS + ( |

### Menus

The HP-48 uses menus extensively to access its hundreds of built-in commands. Menu labels appear at the bottom of the display and correspond to the six menu keys (A–F / `F1`–`F6`).

- Press `MTH` to open the Math menu (submenus: VECTR, MATR, LIST, HYP, REAL, BASE, PROB, etc.)
- Press `NXT` to see the next page of menu items
- Press LS + `NXT` (PREV) to go back a page
- Press RS + `NXT` (MENU) to return to the last menu you were using
- If a menu label has a tab in its upper-left corner, pressing it opens a submenu

You don't need to "exit" a menu to use another — just open the new one directly.

### Angle Modes

The calculator defaults to degrees (DEG). To switch:
- **DEG → RAD**: Left-Shift + MTH (labeled RAD). The RAD annunciator appears.
- **RAD → DEG**: Use the MODES application (Right-Shift + NXT) to change back.

You can also toggle via: RS + NXT (MODES) → select Angle → choose DEG, RAD, or GRAD.

### The CANCEL Key (ON / Escape)

The ON key serves as CANCEL when the calculator is running:
- Delete the command line — press CANCEL
- Exit a special environment and return to the stack — press CANCEL
- Stop a running program — press CANCEL
- Recover from an error — press CANCEL

### Variables and Memory

The HP-48 organizes memory into directories (like folders). The home directory is `{ HOME }`.

To store a value in a variable:
1. Put the value on stack level 1
2. Press `STO`
3. Type the variable name (in alpha mode)
4. Press `ENTER`

To recall a variable, press `VAR` to see the current directory's variables as menu labels, then press the corresponding menu key.

## Overlays

### Keyboard Help (Cmd+K)

Displays a reference card showing all keyboard-to-calculator mappings and command shortcuts. Press `Escape` or click anywhere to dismiss.

### About (Cmd+I)

Shows version, credits, and license information. Press `Escape` or click anywhere to dismiss.

## Audio

The calculator's built-in speaker is emulated via SDL2 audio. Beep tones (square waves from 20 Hz to 20 kHz) play through your Mac's audio output at 44.1 kHz sample rate. You can control the beep setting from the calculator itself via MODES (Right-Shift + NXT). If audio hardware is unavailable, the emulator continues silently.

## Data Files

All calculator state is stored in `~/.droid48/`:

| File | Purpose |
|------|---------|
| `rom` | HP-48GX ROM image (read-only after first copy) |
| `ram` | Calculator memory — variables, programs, settings (~128 KB) |
| `hp48` | Hardware configuration state |
| `port1` | Port 1 card memory (optional) |
| `port2` | Port 2 card memory (optional) |

State is saved when you quit (`Cmd+Q` or close the window) and restored on next launch.

**To reset the calculator to factory state**, delete `~/.droid48/ram` and `~/.droid48/hp48`, then relaunch. Your ROM will be preserved.

**To back up your calculator state**, copy the entire `~/.droid48/` directory.

## Command-Line Options

When running MAC48GX from the terminal:

```bash
./MAC48GX              # normal launch
./MAC48GX --test       # run 148 automated tests (visual mode)
./MAC48GX --test --delay 30  # tests with 30-second countdown
```

## Troubleshooting

**Calculator won't start / blank screen**
- Ensure `~/.droid48/rom` exists. If missing, the app copies it from bundled resources on launch. If you built from source, make sure `app/src/main/jni/rom` exists.

**No sound**
- Check your Mac's audio output volume. Ensure the calculator's beep is enabled via MODES (Right-Shift + NXT → Beep).

**Calculator is frozen / unresponsive**
- Press `Escape` (ON/CANCEL) to interrupt. The HP-48 can remember up to 15 keystrokes while busy and will process them when free.
- If truly locked, delete `~/.droid48/ram` and `~/.droid48/hp48` to perform a memory reset.

**Keys don't respond**
- Make sure the MAC48GX window is focused.
- If an overlay (keyboard help or about) is showing, dismiss it first by pressing `Escape` or clicking.

**Display shows unexpected annunciators**
- ◀ or ▶ means a shift key is active — press it again to cancel.
- α means alpha mode is locked — press ALPHA (or `a`) to exit.
- RAD means you're in radians mode — use MODES to switch back to DEG if needed.

## License

GNU General Public License v3.0. Core emulator (x48): GPL v2+. See [COPYING](COPYING).
