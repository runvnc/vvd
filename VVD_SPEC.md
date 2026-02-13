# VVD (Versatile Vector Diagram) Specification v1.1

**File Extension:** `.vvd` (text), `.vvb` (binary)  
**Magic Number (binary):** `VVD1` (0x56 0x56 0x44 0x31)

---

## 1. Overview

VVD is a minimal, state-based vector diagram format. It supports:

- **Definitions** (reusable shapes built from the same drawing primitives)
- **Native text** (client-rendered at a coordinate-relative size)
- **Primitives** (lines, bezier curves)
- **Palette-indexed color**
- **File includes** for modularity
- **Compact binary serialization**

The format has two representations: a human-readable **text format** and a compact **binary format**. Both encode the same command stream.

---

## 2. Coordinate System

All coordinates are in an abstract unit space. There is no fixed canvas size — the client determines viewport and scaling. Coordinates can be negative. The origin (0,0) is top-left by convention, with Y increasing downward, but clients may remap this.

---

## 3. Command Set (Text Format)

Comments start with `#` and run to end of line. Blank lines are ignored.

| Cmd   | Syntax                              | Description |
|:------|:------------------------------------|:------------|
| `PAL` | `PAL id, #rrggbb`                   | Set palette slot (0–255) to a hex color. |
| `DEF` | `DEF id` ... `END`                  | Define a reusable shape using `MOV`, `LIN`, and `BEZ` commands (without color). Closed by `END`. |
| `SIZ` | `SIZ scale`                         | Set scale multiplier for subsequent `SYM` commands. Default 1.0. |
| `ROT` | `ROT degrees`                       | Set rotation for the **next** `SYM` only, then resets to 0. |
| `MOV` | `MOV x, y`                          | Move pen to absolute position (no drawing). |
| `LIN` | `LIN x, y, col`                     | Draw line from current pen to (x,y) using palette[col]. Updates pen position. |
| `BEZ` | `BEZ cx1,cy1, cx2,cy2, ex,ey, col` | Cubic bezier from current pen to (ex,ey) with control points (cx1,cy1) and (cx2,cy2). Uses palette[col]. Updates pen position. |
| `SYM` | `SYM id, x, y, col`                | Place defined shape [id] at (x,y) using palette[col]. Affected by current `SIZ` and `ROT`. |
| `TXT` | `TXT "string", x, y, size, col`    | Render text at (x,y) using the client's native font. `size` is approximate cap-height in coordinate units. Uses palette[col]. |
| `INC` | `INC "filename"`                    | Include another `.vvd` file inline. All state carries over. |
| `END` | `END`                               | Closes a `DEF` block. |

### DEF Blocks

A `DEF` block captures a sequence of drawing primitives as a reusable template. Inside a `DEF`, only `MOV`, `LIN`, and `BEZ` are allowed, and **color is omitted** — color is supplied later by `SYM`. All coordinates inside a `DEF` are relative to the shape's local origin (0,0).

```
DEF 1
  MOV -8, -8
  LIN 8, 8
  MOV 8, -8
  LIN -8, 8
END
```

When placed with `SYM 1, 50, 50, 3`, the shape is scaled by `SIZ`, rotated by `ROT`, translated to (50,50), and drawn in palette color 3.

**Inside DEF, the syntax for LIN and BEZ omits the color parameter:**

| Cmd   | Syntax (inside DEF)                 |
|:------|:------------------------------------|
| `MOV` | `MOV x, y`                          |
| `LIN` | `LIN x, y`                          |
| `BEZ` | `BEZ cx1,cy1, cx2,cy2, ex,ey`      |

### State Summary

| State   | Set by | Default | Scope |
|:--------|:-------|:--------|:------|
| Palette | `PAL`  | All black | Persistent |
| Pen pos | `MOV`, `LIN`, `BEZ` | (0,0) | Persistent |
| Scale   | `SIZ`  | 1.0 | Persistent until changed |
| Rotation| `ROT`  | 0 | Consumed by next `SYM`, then resets to 0 |

---

## 4. Client Rendering Logic

1. **Parse palette** — store `PAL` entries in a 256-slot color array.
2. **Parse definitions** — when a `DEF id` is encountered, collect all commands until `END` and store them in a map keyed by id.
3. **Stream commands** in order:
   - **`SYM`**: Fetch command list from DEF map. For each sub-command's coordinates: multiply by `SIZ`, rotate by `ROT` (if nonzero), translate by (x,y), then draw with `moveTo`/`lineTo`/`bezierCurveTo` using the color from `SYM`'s col parameter.
   - **`TXT`**: Render the string using whatever font system the client has. The `size` parameter is the approximate capital letter height in the same coordinate units used by everything else.
   - **`LIN`**: Draw a straight line from current pen to the target point.
   - **`BEZ`**: Draw a cubic bezier from current pen to the endpoint.
   - **`INC`**: Recursively load and execute the referenced file.

### Rotation Math (for SYM)

```
rad = ROT × π / 180
x' = x × cos(rad) - y × sin(rad)
y' = x × sin(rad) + y × cos(rad)
```

Applied **after** scaling, **before** translation.

---

## 5. Examples

### 5.1 Hello World in a Box

```
VVD1
# Palette
PAL 0, #000000
PAL 1, #3366CC

# Draw a box from (10,10) to (190,50)
MOV 10, 10
LIN 190, 10, 1
LIN 190, 50, 1
LIN 10, 50, 1
LIN 10, 10, 1

# Text centered inside the box
TXT "Hello, World!", 30, 38, 12, 0
```

This draws a blue rectangle and places black text inside it. The text size of 12 means each capital letter is roughly 12 coordinate units tall — fitting comfortably in the 40-unit-tall box.

### 5.2 Tic-Tac-Toe

```
VVD1
# --- Palette ---
PAL 0, #333333   # Grid lines
PAL 1, #E74C3C   # X color (red)
PAL 2, #2980B9   # O color (blue)

# --- Definitions ---

# X mark: two diagonal lines across a ~16x16 area
DEF 1
  MOV -8, -8
  LIN 8, 8
  MOV 8, -8
  LIN -8, 8
END

# O mark: circle approximation (radius ~10) using 4 cubic beziers
# Uses the standard cubic bezier circle constant: k ≈ 0.552
DEF 2
  MOV 0, -10
  BEZ 5.5,-10, 10,-5.5, 10,0
  BEZ 10,5.5, 5.5,10, 0,10
  BEZ -5.5,10, -10,5.5, -10,0
  BEZ -10,-5.5, -5.5,-10, 0,-10
END

# --- Grid (3x3, cells are 30x30, origin at 10,10) ---
# Vertical lines
MOV 40, 10
LIN 40, 100, 0
MOV 70, 10
LIN 70, 100, 0
# Horizontal lines
MOV 10, 40
LIN 100, 40, 0
MOV 10, 70
LIN 100, 70, 0

# --- Place marks ---
# X in top-left cell (center 25, 25)
SYM 1, 25, 25, 1
# X in center cell (center 55, 55)
SYM 1, 55, 55, 1
# X in bottom-right cell (center 85, 85)
SYM 1, 85, 85, 1

# O in top-center cell (center 55, 25)
SYM 2, 55, 25, 2
# O in middle-left cell (center 25, 55)
SYM 2, 25, 55, 2

# --- Label ---
TXT "Tic-Tac-Toe", 20, 115, 10, 0
```

This draws a grid, places X and O marks using `DEF`/`SYM` (the O is a bezier circle), and labels the board with native text.

---

## 6. Binary Format (`.vvb`)

The binary format encodes the same command stream compactly.

### 6.1 Header

| Offset | Size | Field | Description |
|:-------|:-----|:------|:------------|
| 0 | 4 | Magic | `VVD1` (0x56 0x56 0x44 0x31) |
| 4 | 1 | Version | Format version (currently `0x01`) |
| 5 | 2 | Coord Scale | `uint16` — divide all `int16` coordinate values by this to get actual coordinates. Default `1`. Use `10` for one decimal place, `100` for two. |

Total header: **7 bytes**.

### 6.2 Command Opcodes

| Opcode | Command | Arg Layout |
|:-------|:--------|:-----------|
| `0x01` | `PAL`   | `u8(id)`, `u8(r)`, `u8(g)`, `u8(b)` |
| `0x02` | `DEF`   | `u16(id)`, `u16(sub_cmd_count)`, then sub-commands (see §6.3) |
| `0x03` | `SIZ`   | `u16(scale_x100)` — value is scale × 100, so 150 = 1.5× |
| `0x04` | `ROT`   | `i16(degrees_x10)` — value is degrees × 10, so 45.5° = 455 |
| `0x05` | `MOV`   | `i16(x)`, `i16(y)` |
| `0x06` | `LIN`   | `i16(x)`, `i16(y)`, `u8(col)` |
| `0x07` | `BEZ`   | `i16(cx1)`, `i16(cy1)`, `i16(cx2)`, `i16(cy2)`, `i16(ex)`, `i16(ey)`, `u8(col)` |
| `0x08` | `SYM`   | `u16(id)`, `i16(x)`, `i16(y)`, `u8(col)` |
| `0x09` | `TXT`   | `u8(strlen)`, `chars[strlen]`, `i16(x)`, `i16(y)`, `u16(size_x10)`, `u8(col)` |
| `0x0A` | `INC`   | `u8(strlen)`, `chars[strlen]` |
| `0xFF` | `END`   | (no args) — optional end-of-stream marker |

All multi-byte integers are **little-endian**.

### 6.3 DEF Sub-Commands (Binary)

Inside a binary `DEF`, sub-commands use their own compact tags with **no color parameter**:

| Tag    | Sub-Cmd | Arg Layout |
|:-------|:--------|:-----------|
| `0x00` | `MOV`   | `i16(x)`, `i16(y)` |
| `0x01` | `LIN`   | `i16(x)`, `i16(y)` |
| `0x02` | `BEZ`   | `i16(cx1)`, `i16(cy1)`, `i16(cx2)`, `i16(cy2)`, `i16(ex)`, `i16(ey)` |

The `sub_cmd_count` in the DEF header tells the parser how many of these to read.

### 6.4 Coordinate Encoding

Raw coordinate values in the binary stream are `int16`. To get the actual coordinate, divide by the header's **Coord Scale**:

```
actual = raw_i16 / coord_scale
```

With `coord_scale = 10`, the value `155` represents `15.5`. Range: ±3276.7.

With `coord_scale = 1` (default), coordinates are plain integers. Range: ±32767.

### 6.5 Size Encodings

- **`SIZ` scale**: stored as `uint16` = scale × 100. So `1.0` = `100`, `2.5` = `250`. Range: 0–655.35.
- **`ROT` degrees**: stored as `int16` = degrees × 10. So `45°` = `450`, `-90°` = `-900`. Range: ±3276.7°.
- **`TXT` size**: stored as `uint16` = size × 10. So `12.0` = `120`. Range: 0–6553.5.

### 6.6 Example: Binary Encoding of Hello World

The text version:
```
PAL 0, #000000
MOV 10, 10
LIN 190, 10, 1
TXT "Hi", 30, 38, 12, 0
```

Binary (coord_scale=1, hex):
```
56 56 44 31    # Magic: VVD1
01             # Version: 1
01 00          # Coord scale: 1 (little-endian)

01 00 00 00 00          # PAL 0, #000000
05 0A 00 0A 00          # MOV 10, 10
06 BE 00 0A 00 01       # LIN 190, 10, 1
09 02 48 69 1E 00 26 00 78 00 00  # TXT "Hi" (len=2), x=30, y=38, size=120(=12×10), col=0
```

---

## 7. File Include Semantics

`INC "filename.vvd"` loads the referenced file and executes its commands inline at the current point in the stream. All state (palette, pen position, SIZ) carries into the included file and any changes persist after it returns.

Includes are resolved relative to the including file's directory. Circular includes are an error.

Binary files can include text files and vice versa — the client detects format by magic number.

---

## 8. Design Notes

- **DEF + SYM** is for reusable vector shapes, sprites, icons, or glyphs. DEFs use the same primitives (MOV, LIN, BEZ) as the scene — there's only one drawing language to learn. Color is deferred to placement time via SYM.
- **TXT** is for readable text. The client picks the font — the spec only controls position, size, and color. This keeps VVD simple while allowing rich text rendering on capable clients.
- **BEZ** enables smooth curves, circles, and organic shapes that would be tedious with line segments alone.
- **INC** enables component libraries (e.g., `INC "ui_widgets.vvd"`) and modular diagrams.
- The binary format is designed for embedded/constrained environments where parsing text is expensive. A minimal decoder needs only ~200 lines of C.
