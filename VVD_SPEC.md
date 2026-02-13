# VVD (Versatile Vector Diagram) Specification v1.2

**File Extension:** `.vvd` (text), `.vvb` (binary)  
**Magic Number (binary):** `VVD1` (0x56 0x56 0x44 0x31)

---

## 1. Overview

VVD is a minimal, state-based vector diagram format. It supports:

- **Definitions** (reusable shapes built from the same drawing primitives)
- **Native text** (client-rendered at a coordinate-relative size)
- **Primitives** (lines, circular arcs)
- **Palette-indexed color**
- **File includes** for modularity
- **Compact binary serialization**

The format has two representations: a human-readable **text format** and a compact **binary format**. Both encode the same command stream.

---

## 2. Coordinate System

All coordinates are in an abstract unit space. There is no fixed canvas size — the client determines viewport and scaling. Coordinates can be negative. The origin (0,0) is top-left by convention, with Y increasing downward, but clients may remap this.

Angles are in degrees, measured clockwise from the 3 o'clock position (standard screen coordinates where Y points down). So 0° = right, 90° = down, 180° = left, 270° = up.

---

## 3. Command Set (Text Format)

Comments start with `#` and run to end of line. Blank lines are ignored.

| Cmd   | Syntax                              | Description |
|:------|:------------------------------------|:------------|
| `PAL` | `PAL id, #rrggbb`                   | Set palette slot (0–255) to a hex color. |
| `DEF` | `DEF id` ... `END`                  | Define a reusable shape using `MOV`, `LIN`, and `ARC` commands (without color). Closed by `END`. |
| `SIZ` | `SIZ scale`                         | Set scale multiplier for subsequent `SYM` commands. Default 1.0. |
| `ROT` | `ROT degrees`                       | Set rotation for the **next** `SYM` only, then resets to 0. |
| `MOV` | `MOV x, y`                          | Move pen to absolute position (no drawing). |
| `LIN` | `LIN x, y, col`                     | Draw line from current pen to (x,y) using palette[col]. Updates pen position. |
| `ARC` | `ARC cx, cy, r, startDeg, endDeg, col` | Draw a circular arc centered at (cx,cy) with radius r, from startDeg to endDeg, using palette[col]. Updates pen to arc endpoint. |
| `SYM` | `SYM id, x, y, col`                | Place defined shape [id] at (x,y) using palette[col]. Affected by current `SIZ` and `ROT`. |
| `TXT` | `TXT "string", x, y, size, col`    | Render text centered at (x,y) using the client's native font. `size` is approximate cap-height in coordinate units. Uses palette[col]. |
| `INC` | `INC "filename"`                    | Include another `.vvd` file inline. All state carries over. |
| `END` | `END`                               | Closes a `DEF` block. |

### DEF Blocks

A `DEF` block captures a sequence of drawing primitives as a reusable template. Inside a `DEF`, only `MOV`, `LIN`, and `ARC` are allowed, and **color is omitted** — color is supplied later by `SYM`. All coordinates inside a `DEF` are relative to the shape's local origin (0,0).

```
# X mark
DEF 1
  MOV -8, -8
  LIN 8, 8
  MOV 8, -8
  LIN -8, 8
END

# Circle
DEF 2
  ARC 0, 0, 10, 0, 360
END
```

When placed with `SYM 2, 50, 50, 3`, the shape is scaled by `SIZ`, rotated by `ROT`, translated to (50,50), and drawn in palette color 3.

**Inside DEF, the syntax for LIN and ARC omits the color parameter:**

| Cmd   | Syntax (inside DEF)                 |
|:------|:------------------------------------|
| `MOV` | `MOV x, y`                          |
| `LIN` | `LIN x, y`                          |
| `ARC` | `ARC cx, cy, r, startDeg, endDeg`   |

### ARC Details

The `ARC` command draws a circular arc:
- **(cx, cy)** — center of the circle
- **r** — radius
- **startDeg** — starting angle in degrees
- **endDeg** — ending angle in degrees

The arc is drawn clockwise from startDeg to endDeg. A full circle is `ARC cx, cy, r, 0, 360, col`.

After drawing, the pen position is updated to the endpoint of the arc:
```
penX = cx + r × cos(endDeg × π/180)
penY = cy + r × sin(endDeg × π/180)
```

### State Summary

| State   | Set by | Default | Scope |
|:--------|:-------|:--------|:------|
| Palette | `PAL`  | All black | Persistent |
| Pen pos | `MOV`, `LIN`, `ARC` | (0,0) | Persistent |
| Scale   | `SIZ`  | 1.0 | Persistent until changed |
| Rotation| `ROT`  | 0 | Consumed by next `SYM`, then resets to 0 |

---

## 4. Client Rendering Logic

1. **Parse palette** — store `PAL` entries in a 256-slot color array.
2. **Parse definitions** — when a `DEF id` is encountered, collect all commands until `END` and store them in a map keyed by id.
3. **Stream commands** in order:
   - **`SYM`**: Fetch command list from DEF map. For each sub-command's coordinates: multiply by `SIZ`, rotate by `ROT` (if nonzero), translate by (x,y), then draw using the color from `SYM`'s col parameter.
   - **`TXT`**: Render the string centered at (x,y) using whatever font system the client has. The `size` parameter is the approximate capital letter height in the same coordinate units used by everything else.
   - **`LIN`**: Draw a straight line from current pen to the target point.
   - **`ARC`**: Draw a circular arc. Use the platform's native arc drawing (e.g., `ctx.arc()` in Canvas, `<circle>` or `<path>` in SVG).
   - **`INC`**: Recursively load and execute the referenced file.

### Rotation Math (for SYM)

```
rad = ROT × π / 180
x' = x × cos(rad) - y × sin(rad)
y' = x × sin(rad) + y × cos(rad)
```

Applied **after** scaling, **before** translation. For ARCs inside a rotated SYM, the arc center is rotated and the start/end angles are offset by ROT.

---

## 5. Examples

### 5.1 Hello World in a Box

```
VVD1
PAL 0, #000000
PAL 1, #3366CC

MOV 10, 10
LIN 190, 10, 1
LIN 190, 50, 1
LIN 10, 50, 1
LIN 10, 10, 1

TXT "Hello, World!", 100, 30, 12, 0
```

### 5.2 Tic-Tac-Toe

```
VVD1
PAL 0, #333333
PAL 1, #E74C3C
PAL 2, #2980B9

# X mark
DEF 1
  MOV -8, -8
  LIN 8, 8
  MOV 8, -8
  LIN -8, 8
END

# O mark (full circle)
DEF 2
  ARC 0, 0, 10, 0, 360
END

# Grid
MOV 40, 10
LIN 40, 100, 0
MOV 70, 10
LIN 70, 100, 0
MOV 10, 40
LIN 100, 40, 0
MOV 10, 70
LIN 100, 70, 0

# X marks
SYM 1, 25, 25, 1
SYM 1, 55, 55, 1
SYM 1, 85, 85, 1

# O marks
SYM 2, 55, 25, 2
SYM 2, 25, 55, 2

TXT "Tic-Tac-Toe", 55, 115, 10, 0
```

### 5.3 Arcs Demo

```
VVD1
PAL 0, #333333
PAL 1, #E74C3C
PAL 2, #2ECC71
PAL 3, #3498DB

# Full circle
ARC 60, 60, 40, 0, 360, 1
TXT "Circle", 60, 115, 8, 0

# Semicircle
ARC 180, 60, 40, 180, 360, 3
TXT "Semicircle", 180, 115, 8, 0

# Smiley face
ARC 300, 60, 40, 0, 360, 2
ARC 280, 45, 6, 0, 360, 0
ARC 320, 45, 6, 0, 360, 0
ARC 300, 60, 22, 20, 160, 0
TXT "Smiley", 300, 115, 8, 0
```

---

## 6. Binary Format (`.vvb`)

The binary format encodes the same command stream compactly.

### 6.1 Header

| Offset | Size | Field | Description |
|:-------|:-----|:------|:------------|
| 0 | 4 | Magic | `VVD1` (0x56 0x56 0x44 0x31) |
| 4 | 1 | Version | Format version (currently `0x02`) |
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
| `0x07` | `ARC`   | `i16(cx)`, `i16(cy)`, `u16(r)`, `i16(startDeg_x10)`, `i16(endDeg_x10)`, `u8(col)` |
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
| `0x02` | `ARC`   | `i16(cx)`, `i16(cy)`, `u16(r)`, `i16(startDeg_x10)`, `i16(endDeg_x10)` |

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
- **`ARC` angles**: stored as `int16` = degrees × 10. So `45.5°` = `455`.

---

## 7. File Include Semantics

`INC "filename.vvd"` loads the referenced file and executes its commands inline at the current point in the stream. All state (palette, pen position, SIZ) carries into the included file and any changes persist after it returns.

Includes are resolved relative to the including file's directory. Circular includes are an error.

Binary files can include text files and vice versa — the client detects format by magic number.

---

## 8. Design Notes

- **DEF + SYM** is for reusable vector shapes, sprites, icons, or glyphs. DEFs use the same primitives (MOV, LIN, ARC) as the scene — there's only one drawing language to learn. Color is deferred to placement time via SYM.
- **TXT** is for readable text, centered at the given point. The client picks the font — the spec only controls position, size, and color.
- **ARC** is the curve primitive. Circles, semicircles, quarter-arcs, and any circular arc are trivial to express. Combined with LIN, this covers the vast majority of diagram needs.
- **INC** enables component libraries (e.g., `INC "ui_widgets.vvd"`) and modular diagrams.
- The binary format is designed for embedded/constrained environments where parsing text is expensive. A minimal decoder needs only ~200 lines of C.
