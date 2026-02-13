# VVD Python Library

A Python library for the **VVD (Versatile Vector Diagram)** format — a minimal, state-based vector diagram format with text and binary representations.

## Features

- **VVDParser** — Parse `.vvd` text files into a command list
- **VVDBinary** — Serialize/deserialize the compact binary format (`.vvb`)
- **VVDRenderer** — Render to SVG string or PIL/Pillow Image (PNG, etc.)
- **VVDWriter** — Fluent API for programmatically building VVD documents

## Installation

Copy `vvd.py` into your project, or add this directory to your Python path.

**Optional dependency:** [Pillow](https://pypi.org/project/Pillow/) for PNG rendering.

```bash
pip install Pillow  # optional, for PNG output
```

## Quick Start

### Parse and Render

```python
from vvd import VVDParser, VVDRenderer

# Parse a VVD file
parser = VVDParser()
commands = parser.parse_file('diagram.vvd')

# Render to SVG
renderer = VVDRenderer()
svg_string = renderer.render_svg(commands, width=800, height=600)
with open('output.svg', 'w') as f:
    f.write(svg_string)

# Render to PNG (requires Pillow)
img = renderer.render_pil(commands, width=800, height=600)
img.save('output.png')
```

### Binary Round-Trip

```python
from vvd import VVDParser, VVDBinary

parser = VVDParser()
commands = parser.parse_file('diagram.vvd')

binary = VVDBinary()

# Serialize to .vvb
binary.serialize_to_file(commands, 'diagram.vvb', coord_scale=10)

# Deserialize back
commands2, coord_scale = binary.deserialize_file('diagram.vvb')
```

### Build with VVDWriter

```python
from vvd import VVDWriter

writer = (VVDWriter()
    .pal(0, '#000000')
    .pal(1, '#FF0000')
    .pal(2, '#0000FF')
    # Define a reusable shape
    .begin_def(1)
        .mov(-10, -10)
        .lin(10, -10)
        .lin(10, 10)
        .lin(-10, 10)
        .lin(-10, -10)
    .end_def()
    # Draw
    .sym(1, 50, 50, 1)
    .sym(1, 100, 50, 2)
    .txt("Hello VVD", 75, 80, 10, 0)
)

# Save as text
writer.save_text('output.vvd')

# Save as binary
writer.save_binary('output.vvb')

# Get command list for rendering
from vvd import VVDRenderer
renderer = VVDRenderer()
svg = renderer.render_svg(writer.commands)
```

## API Reference

### VVDParser

| Method | Description |
|--------|-------------|
| `parse(text, base_dir='.')` | Parse a VVD text string into a list of `Cmd` objects |
| `parse_file(path)` | Parse a `.vvd` file |

### VVDBinary

| Method | Description |
|--------|-------------|
| `serialize(commands, coord_scale=1)` | Serialize commands to `bytes` |
| `serialize_to_file(commands, path, coord_scale=1)` | Serialize and write to file |
| `deserialize(data)` | Deserialize bytes → `(commands, coord_scale)` |
| `deserialize_file(path)` | Deserialize a `.vvb` file |

### VVDRenderer

| Method | Description |
|--------|-------------|
| `render_svg(commands, width=None, height=None)` | Render to SVG string |
| `render_pil(commands, width=800, height=600, bg_color='#FFFFFF', line_width=2)` | Render to PIL Image |

### VVDWriter (Fluent API)

| Method | Description |
|--------|-------------|
| `.pal(slot, color)` | Set palette color |
| `.begin_def(id)` / `.end_def()` | Define a reusable shape |
| `.siz(scale)` | Set scale |
| `.rot(degrees)` | Set rotation |
| `.mov(x, y)` | Move pen |
| `.lin(x, y, col=None)` | Draw line (col omitted inside DEF) |
| `.arc(cx, cy, r, start, end, col=None)` | Draw arc (col omitted inside DEF) |
| `.sym(id, x, y, col)` | Place a defined shape |
| `.txt(text, x, y, size, col)` | Render text |
| `.inc(filename)` | Include another file |
| `.to_text()` | Generate VVD text string |
| `.save_text(path)` | Save as `.vvd` file |
| `.save_binary(path, coord_scale=1)` | Save as `.vvb` file |
| `.commands` | Get the command list |

### Cmd Dataclass

Each parsed command is a `Cmd(op, args)` where `op` is the command name string and `args` is a dict of parameters.

## VVD Commands

| Command | Description |
|---------|-------------|
| `PAL` | Set palette color slot |
| `DEF`/`END` | Define reusable shape |
| `SIZ` | Set scale multiplier |
| `ROT` | Set rotation (consumed by next SYM) |
| `MOV` | Move pen position |
| `LIN` | Draw line from pen to point |
| `ARC` | Draw circular arc |
| `SYM` | Place a defined shape |
| `TXT` | Render text |
| `INC` | Include another VVD file |

## Running the Demo

```bash
cd python/
python demo.py
```

Output files will be created in `python/output/`.

## License

See the parent VVD project for license information.
