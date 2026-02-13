# VVD — Versatile Vector Diagram

A minimal, state-based vector diagram format with text and binary representations.

## Features

- **Reusable definitions** — define shapes once with MOV/LIN/BEZ, stamp them anywhere with SYM
- **Native text** — render strings at any size using the client’s font, no glyph definitions needed
- **Bezier curves** — cubic beziers for smooth curves and circles
- **256-color palette** — define colors once, reference by index
- **File includes** — modular diagrams via INC
- **Compact binary format** — same command stream in ~200-lines-of-C-decodable binary

## Quick Example

```
VVD1
PAL 0, #000000
PAL 1, #3366CC

MOV 10, 10
LIN 190, 10, 1
LIN 190, 50, 1
LIN 10, 50, 1
LIN 10, 10, 1

TXT "Hello, World!", 30, 38, 12, 0
```

## Files

- [`VVD_SPEC.md`](VVD_SPEC.md) — Full specification (v1.1)
- [`index.html`](index.html) — Browser-based editor, renderer, and example demos

## Try It

Open `index.html` in any browser. Edit VVD text on the left, see it rendered on the right.

## License

Public domain / CC0.
