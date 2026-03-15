# MAC48GX Manual Tests

These tests require keyboard/mouse interaction because they involve alpha text entry, matrix/vector input via brackets, or menu navigation that cannot be reliably automated through the button event queue.

Run the calculator: `./macos/build/MAC48GX` or open `MAC48GX.app`

> **Tip:** To type brackets, use left-shift + `×` for `[ ]` and left-shift + `÷` for `( )`.
> To enter a matrix: `LS+×` `LS+×` type first row with SPC between elements, `▶` to move past `]`, `LS+×` for next row, then ENTER.
> To type commands in alpha mode: press `α` twice (lock), type letters, press `α` to unlock, then ENTER.

---

## 1. Vector Entry and Operations

### 1.1 Enter a 2D vector
```
LS+× 3 SPC 4 ENTER
```
**Expected:** `[ 3 4 ]` on stack level 1

### 1.2 Vector magnitude (ABS)
```
(with [3,4] on stack)
α α A B S α ENTER
```
**Expected:** `5` (√(9+16) = 5)

### 1.3 Vector addition
```
LS+× 1 SPC 2 ENTER
LS+× 3 SPC 4 ENTER
+
```
**Expected:** `[ 4 6 ]`

### 1.4 Scalar × vector
```
3 ENTER
LS+× 2 SPC 5 ENTER
×
```
**Expected:** `[ 6 15 ]`

### 1.5 3D vector entry
```
LS+× 1 SPC 2 SPC 3 ENTER
```
**Expected:** `[ 1 2 3 ]`

### 1.6 Dot product (2D)
```
LS+× 1 SPC 2 ENTER
LS+× 3 SPC 4 ENTER
α α D O T α ENTER
```
**Expected:** `11` (1×3 + 2×4 = 11)

### 1.7 Cross product (3D)
```
LS+× 1 SPC 0 SPC 0 ENTER
LS+× 0 SPC 1 SPC 0 ENTER
α α C R O S S α ENTER
```
**Expected:** `[ 0 0 1 ]`

---

## 2. Matrix Entry and Operations

### 2.1 Enter a 2×2 matrix
```
LS+× LS+× 1 SPC 2 ▶ LS+× 3 SPC 4 ENTER
```
**Expected:** `[[ 1 2 ][ 3 4 ]]` on stack

### 2.2 Determinant
```
(with [[1,2],[3,4]] on stack)
α α D E T α ENTER
```
**Expected:** `-2` (1×4 - 2×3 = -2)

### 2.3 Matrix inverse
```
LS+× LS+× 1 SPC 2 ▶ LS+× 3 SPC 4 ENTER
1/x
```
**Expected:** `[[ -2 1 ][ 1.5 -.5 ]]`

### 2.4 Verify A × A⁻¹ = Identity
```
LS+× LS+× 1 SPC 2 ▶ LS+× 3 SPC 4 ENTER
ENTER                    (DUP the matrix)
1/x                      (invert copy)
×                        (multiply)
```
**Expected:** `[[ 1 0 ][ 0 1 ]]` (identity matrix)

### 2.5 Identity matrix via IDN
```
2 ENTER
α α I D N α ENTER
```
**Expected:** `[[ 1 0 ][ 0 1 ]]`

### 2.6 3×3 Identity determinant
```
3 ENTER
α α I D N α ENTER
α α D E T α ENTER
```
**Expected:** `1`

### 2.7 Trace of a matrix
```
LS+× LS+× 5 SPC 0 ▶ LS+× 0 SPC 3 ENTER
α α T R A C E α ENTER
```
**Expected:** `8` (5 + 3 = 8)

### 2.8 Matrix + scalar
```
2 ENTER
α α I D N α ENTER
5 +
```
**Expected:** `[[ 6 0 ][ 0 6 ]]` (I×2 + 5 = [[7,5],[5,7]]... actually this adds 5 to each element of 2×I)

> Note: On HP-48, scalar + matrix adds the scalar to each diagonal element. Verify the actual behavior.

### 2.9 Transpose
```
LS+× LS+× 1 SPC 2 ▶ LS+× 3 SPC 4 ENTER
α α T R N α ENTER
```
**Expected:** `[[ 1 3 ][ 2 4 ]]`

---

## 3. Complex Numbers

### 3.1 Enter a complex number
```
LS+÷ 3 SPC 4 ENTER
```
**Expected:** `(3,4)` on stack

### 3.2 Complex magnitude (ABS)
```
LS+÷ 3 SPC 4 ENTER
α α A B S α ENTER
```
**Expected:** `5`

### 3.3 Complex argument (ARG)
Switch to DEG mode first if not already:
```
LS+÷ 3 SPC 4 ENTER
RS+EEX                  (ARG)
```
**Expected:** `53.1301023542` (degrees)

### 3.4 Complex addition
```
LS+÷ 1 SPC 2 ENTER
LS+÷ 3 SPC 4 ENTER
+
```
**Expected:** `(4,6)`

### 3.5 Complex multiplication
```
LS+÷ 1 SPC 1 ENTER
LS+÷ 0 SPC 1 ENTER
×
```
**Expected:** `(-1,1)` (i × (1+i) = -1+i)

### 3.6 Real to complex
```
3 ENTER
4 ENTER
RS+- (R→C)
```
**Expected:** `(3,4)`

> Note: R→C is right-shift + minus key. Verify the actual shifted function.

---

## 4. Number Base Modes

### 4.1 Switch to HEX mode
Press menu key for `HEX` in the BASE menu (if visible), or:
```
α α H E X α ENTER
```
Then enter: `255 ENTER`

**Expected:** `# FFh` (255 in hex)

### 4.2 Binary mode
```
α α B I N α ENTER
10 ENTER
```
**Expected:** `# 1010b` (10 in binary)

### 4.3 Octal mode
```
α α O C T α ENTER
8 ENTER
```
**Expected:** `# 10o` (8 in octal)

### 4.4 Back to decimal
```
α α D E C α ENTER
```

### 4.5 Base conversion
```
(in HEX mode)
# FF ENTER
α α D E C α ENTER
```
**Expected:** `255`

---

## 5. Display Modes

### 5.1 FIX mode
```
RS+CST (MODES)
```
Navigate to NUMBER FORMAT, select FIX, set to 2, press OK.
Then: `1 ENTER 3 ÷`

**Expected:** `0.33` (displayed with 2 decimal places)

### 5.2 SCI mode
Set SCI 4 via MODES, then: `12345.6789 ENTER`

**Expected:** `1.2346E4`

### 5.3 ENG mode
Set ENG 3 via MODES, then: `12345.6789 ENTER`

**Expected:** `12.3E3`

### 5.4 STD mode (restore)
Set STD via MODES, then: `1 ENTER 3 ÷`

**Expected:** `.333333333333`

---

## 6. Coordinate Modes

### 6.1 Polar display mode
```
RS+MTH (POLAR mode toggle)
LS+÷ 3 SPC 4 ENTER
```
**Expected:** `(5,∠53.1301023542)` (magnitude and angle in polar form)

### 6.2 Back to rectangular
```
RS+MTH (toggle back)
```

---

## 7. Symbolic Constants and Evaluation

### 7.1 π constant
```
LS+SPC                   (push π)
```
**Expected:** `'π'` (symbolic)

### 7.2 π to numeric
```
LS+SPC                   (push π)
LS+EVAL                  (→NUM)
```
**Expected:** `3.14159265359`

### 7.3 Symbolic expression
```
LS+÷ 'X+1' ENTER
2 ENTER
(not easily testable without full alpha entry)
```

---

## 8. Programming (Basic)

### 8.1 Simple program
```
LS+× LS+× (enter program delimiters « »)
... type: DUP × ...
ENTER
```
This creates a squaring program `« DUP × »`.

### 8.2 Execute program
```
5 ENTER
(recall program name) EVAL
```
**Expected:** `25`

---

## Key Reference

| Shortcut | Function |
|----------|----------|
| `LS+×` | Enter `[ ]` brackets (arrays/matrices) |
| `LS+÷` | Enter `( )` parentheses (complex numbers) |
| `LS+−` | Enter `« »` program delimiters |
| `RS+−` | Enter `" "` string delimiters |
| `LS++` | Enter `{ }` list delimiters |
| `RS++` | Enter `: :` tag delimiters |
| `LS+SPC` | π (symbolic constant) |
| `RS+SPC` | ∠ (angle symbol) |
| `LS+EVAL` | →NUM (numeric evaluation) |
| `RS+EEX` | ARG (complex argument/angle) |
| `α α` | Lock alpha mode (type commands) |
| `α` | Unlock alpha mode |

### Alpha Key Map (for typing commands)
```
A=menu-A  B=menu-B  C=menu-C  D=menu-D  E=menu-E  F=menu-F
G=MTH     H=PRG     I=CST     J=VAR     K=UP      L=NXT
M=:       N=STO     O=EVAL    P=LEFT    Q=DOWN    R=RIGHT
S=SIN     T=COS     U=TAN     V=SQRT    W=y^x     X=1/x
Y=+/-     Z=EEX
```
