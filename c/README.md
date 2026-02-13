# VVD C Library & SDL2 Demo

A single-header C library for parsing, rendering, and writing VVD (Versatile Vector Diagram) files, plus an SDL2-based viewer.

## Files

| File | Description |
|------|-------------|
| `vvd.h` | Core library (header-only). Text/binary parsing, binary writing, render callbacks. |
| `vvd_sdl.h` | SDL2 rendering backend (header-only). |
| `demo.c` | Interactive SDL2 viewer application. |
| `Makefile` | Build script. |

## Dependencies

- **C11 compiler** (gcc, clang)
- **SDL2** development libraries
- **SDL2_ttf** development libraries

### Install on Debian/Ubuntu

```bash
sudo apt install libsdl2-dev libsdl2-ttf-dev
```

### Install on Arch Linux

```bash
sudo pacman -S sdl2 sdl2_ttf
```

### Install on macOS (Homebrew)

```bash
brew install sdl2 sdl2_ttf
```

## Build

```bash
make
```

## Run

```bash
# With a VVD file
./vvd_demo path/to/file.vvd

# Built-in demo (tic-tac-toe + arcs)
./vvd_demo
```

## Controls

| Key | Action |
|-----|--------|
| `ESC` / `Q` | Quit |
| `F` | Fit to window |
| `+` / `-` | Zoom in / out |
| Arrow keys | Pan |

## Using the Library

The library is header-only. In **one** C file, define the implementation macro before including:

```c
#define VVD_IMPLEMENTATION
#include "vvd.h"
```

### Parse and render

```c
VvdDoc doc;
vvd_init(&doc);
vvd_parse_file(&doc, "diagram.vvd");

// Set up your renderer callbacks
VvdRenderer renderer = {
    .user = my_context,
    .line = my_line_callback,
    .arc  = my_arc_callback,
    .text = my_text_callback,
};
vvd_render(&doc, &renderer);
```

### Convert text to binary

```c
VvdDoc doc;
vvd_init(&doc);
vvd_parse_file(&doc, "input.vvd");

// Get required size
int size = vvd_write_binary(&doc, NULL, 0);
uint8_t *buf = malloc(size);
vvd_write_binary(&doc, buf, size);
// Write buf to file...
free(buf);
```

## Render Callback Interface

The `VvdRenderer` struct has three function pointers:

```c
void (*line)(VvdRenderer *r, float x1, float y1, float x2, float y2, VvdColor color);
void (*arc)(VvdRenderer *r, float cx, float cy, float radius, float start_deg, float end_deg, VvdColor color);
void (*text)(VvdRenderer *r, const char *str, float x, float y, float size, VvdColor color);
```

This makes it easy to plug in any graphics backend (OpenGL, Vulkan, Cairo, etc.).
