#!/usr/bin/env python3
"""
VVD Python Library Demo
=======================

Demonstrates:
  1. Parsing VVD text format
  2. Rendering to SVG
  3. Rendering to PNG (requires Pillow)
  4. Binary round-trip (serialize + deserialize)
  5. Building a VVD document with VVDWriter (fluent API)
"""

import os
import sys

# Add parent dir to path so we can import vvd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vvd import VVDParser, VVDBinary, VVDRenderer, VVDWriter

# ---------------------------------------------------------------------------
# Sample VVD document (Tic-Tac-Toe from the spec)
# ---------------------------------------------------------------------------

SAMPLE_VVD = """\
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
"""

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'output')


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # ------------------------------------------------------------------
    # 1. Parse VVD text
    # ------------------------------------------------------------------
    print("=" * 60)
    print("1. Parsing VVD text format")
    print("=" * 60)

    parser = VVDParser()
    commands = parser.parse(SAMPLE_VVD)
    print(f"   Parsed {len(commands)} commands:")
    for cmd in commands:
        print(f"     {cmd}")
    print()

    # ------------------------------------------------------------------
    # 2. Render to SVG
    # ------------------------------------------------------------------
    print("=" * 60)
    print("2. Rendering to SVG")
    print("=" * 60)

    renderer = VVDRenderer()
    svg = renderer.render_svg(commands, width=400, height=400)
    svg_path = os.path.join(OUT_DIR, 'tictactoe.svg')
    with open(svg_path, 'w') as f:
        f.write(svg)
    print(f"   SVG saved to {svg_path}")
    print(f"   SVG length: {len(svg)} chars")
    print()

    # ------------------------------------------------------------------
    # 3. Render to PNG (requires Pillow)
    # ------------------------------------------------------------------
    print("=" * 60)
    print("3. Rendering to PNG")
    print("=" * 60)

    try:
        img = renderer.render_pil(commands, width=400, height=400, line_width=2)
        png_path = os.path.join(OUT_DIR, 'tictactoe.png')
        img.save(png_path)
        print(f"   PNG saved to {png_path}")
    except ImportError as e:
        print(f"   Skipped (Pillow not installed): {e}")
    print()

    # ------------------------------------------------------------------
    # 4. Binary round-trip
    # ------------------------------------------------------------------
    print("=" * 60)
    print("4. Binary round-trip (.vvb)")
    print("=" * 60)

    binary = VVDBinary()
    vvb_data = binary.serialize(commands, coord_scale=1)
    vvb_path = os.path.join(OUT_DIR, 'tictactoe.vvb')
    with open(vvb_path, 'wb') as f:
        f.write(vvb_data)
    print(f"   Binary serialized: {len(vvb_data)} bytes -> {vvb_path}")

    # Deserialize back
    commands2, cs = binary.deserialize(vvb_data)
    print(f"   Deserialized: {len(commands2)} commands (coord_scale={cs})")

    # Verify round-trip by re-rendering SVG
    svg2 = renderer.render_svg(commands2, width=400, height=400)
    svg2_path = os.path.join(OUT_DIR, 'tictactoe_roundtrip.svg')
    with open(svg2_path, 'w') as f:
        f.write(svg2)
    print(f"   Round-trip SVG saved to {svg2_path}")

    # Quick sanity check
    if len(commands) == len(commands2):
        print("   ✓ Command count matches after round-trip")
    else:
        print(f"   ✗ Command count mismatch: {len(commands)} vs {len(commands2)}")
    print()

    # ------------------------------------------------------------------
    # 5. VVDWriter fluent API
    # ------------------------------------------------------------------
    print("=" * 60)
    print("5. Building VVD with VVDWriter (fluent API)")
    print("=" * 60)

    writer = (VVDWriter()
        .pal(0, '#000000')
        .pal(1, '#FF6600')
        .pal(2, '#0066FF')
        # Define a triangle
        .begin_def(1)
            .mov(0, -15)
            .lin(13, 10)
            .lin(-13, 10)
            .lin(0, -15)
        .end_def()
        # Define a small circle
        .begin_def(2)
            .arc(0, 0, 8, 0, 360)
        .end_def()
        # Draw a border
        .mov(10, 10)
        .lin(190, 10, 0)
        .lin(190, 90, 0)
        .lin(10, 90, 0)
        .lin(10, 10, 0)
        # Place shapes
        .sym(1, 50, 50, 1)
        .sym(2, 100, 50, 2)
        .sym(1, 150, 50, 2)
        # Title
        .txt("VVDWriter Demo", 100, 105, 8, 0)
    )

    # Save as text
    vvd_text = writer.to_text()
    text_path = os.path.join(OUT_DIR, 'writer_demo.vvd')
    writer.save_text(text_path)
    print(f"   VVD text saved to {text_path}")
    print(f"   Content:\n{vvd_text}")

    # Save as binary
    bin_path = os.path.join(OUT_DIR, 'writer_demo.vvb')
    writer.save_binary(bin_path)
    print(f"   VVB binary saved to {bin_path}")

    # Render the writer output
    writer_svg = renderer.render_svg(writer.commands, width=400, height=250)
    writer_svg_path = os.path.join(OUT_DIR, 'writer_demo.svg')
    with open(writer_svg_path, 'w') as f:
        f.write(writer_svg)
    print(f"   SVG saved to {writer_svg_path}")

    try:
        writer_img = renderer.render_pil(writer.commands, width=400, height=250, line_width=2)
        writer_png_path = os.path.join(OUT_DIR, 'writer_demo.png')
        writer_img.save(writer_png_path)
        print(f"   PNG saved to {writer_png_path}")
    except ImportError:
        print("   PNG skipped (Pillow not installed)")

    print()
    print("=" * 60)
    print("Demo complete! Check the 'output/' directory for results.")
    print("=" * 60)


if __name__ == '__main__':
    main()
