/*
 * demo.c - SDL2 demo for VVD rendering
 *
 * Usage: ./vvd_demo <file.vvd>
 *
 * Controls:
 *   ESC / Q  - Quit
 *   F        - Fit to window
 *   +/-      - Zoom in/out
 *   Arrow keys - Pan
 */
#define VVD_IMPLEMENTATION
#include "vvd.h"

#define VVD_SDL_IMPLEMENTATION
#include "vvd_sdl.h"

#include <stdio.h>
#include <stdlib.h>

#define WINDOW_W 800
#define WINDOW_H 600
#define MARGIN   30

static const char *DEFAULT_VVD =
    "VVD1\n"
    "PAL 0, #333333\n"
    "PAL 1, #E74C3C\n"
    "PAL 2, #2980B9\n"
    "PAL 3, #2ECC71\n"
    "\n"
    "# X mark\n"
    "DEF 1\n"
    "  MOV -8, -8\n"
    "  LIN 8, 8\n"
    "  MOV 8, -8\n"
    "  LIN -8, 8\n"
    "END\n"
    "\n"
    "# O mark\n"
    "DEF 2\n"
    "  ARC 0, 0, 10, 0, 360\n"
    "END\n"
    "\n"
    "# Grid\n"
    "MOV 40, 10\n"
    "LIN 40, 100, 0\n"
    "MOV 70, 10\n"
    "LIN 70, 100, 0\n"
    "MOV 10, 40\n"
    "LIN 100, 40, 0\n"
    "MOV 10, 70\n"
    "LIN 100, 70, 0\n"
    "\n"
    "SYM 1, 25, 25, 1\n"
    "SYM 1, 55, 55, 1\n"
    "SYM 1, 85, 85, 1\n"
    "SYM 2, 55, 25, 2\n"
    "SYM 2, 25, 55, 2\n"
    "\n"
    "# Arcs demo\n"
    "ARC 160, 55, 30, 0, 360, 3\n"
    "ARC 230, 55, 30, 180, 360, 2\n"
    "\n"
    "TXT \"VVD Demo\", 55, 115, 10, 0\n";

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "VVD Viewer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    SDL_Renderer *sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit();
        return 1;
    }

    /* Try to load a font - search common paths */
    TTF_Font *font = NULL;
    const char *font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        NULL
    };
    for (int i = 0; font_paths[i]; i++) {
        font = TTF_OpenFont(font_paths[i], 48);
        if (font) break;
    }
    if (!font) {
        fprintf(stderr, "Warning: Could not load any font. Text will not render.\n");
    }

    /* Parse VVD */
    VvdDoc doc;
    vvd_init(&doc);

    if (argc > 1) {
        if (vvd_parse_file(&doc, argv[1]) != 0) {
            fprintf(stderr, "Failed to parse: %s\n", argv[1]);
            /* Fall back to built-in demo */
            vvd_init(&doc);
            vvd_parse_text(&doc, DEFAULT_VVD);
        } else {
            printf("Loaded: %s (%d commands, %d defs)\n", argv[1], doc.cmd_count, doc.def_count);
        }
    } else {
        printf("No file specified, using built-in demo.\n");
        printf("Usage: %s <file.vvd>\n", argv[0]);
        vvd_parse_text(&doc, DEFAULT_VVD);
    }

    /* Setup renderer */
    VvdSdlRenderer vr;
    vvd_sdl_init(&vr, sdl_renderer, font);

    int win_w = WINDOW_W, win_h = WINDOW_H;
    vvd_sdl_fit(&vr, &doc, win_w, win_h, MARGIN);

    /* Main loop */
    bool running = true;
    bool needs_redraw = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    running = false;
                    break;
                case SDLK_f:
                    SDL_GetWindowSize(window, &win_w, &win_h);
                    vvd_sdl_fit(&vr, &doc, win_w, win_h, MARGIN);
                    needs_redraw = true;
                    break;
                case SDLK_PLUS:
                case SDLK_EQUALS:
                    vr.scale *= 1.2f;
                    needs_redraw = true;
                    break;
                case SDLK_MINUS:
                    vr.scale /= 1.2f;
                    needs_redraw = true;
                    break;
                case SDLK_LEFT:
                    vr.offset_x -= 20;
                    needs_redraw = true;
                    break;
                case SDLK_RIGHT:
                    vr.offset_x += 20;
                    needs_redraw = true;
                    break;
                case SDLK_UP:
                    vr.offset_y -= 20;
                    needs_redraw = true;
                    break;
                case SDLK_DOWN:
                    vr.offset_y += 20;
                    needs_redraw = true;
                    break;
                default: break;
                }
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    ev.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    SDL_GetWindowSize(window, &win_w, &win_h);
                    vvd_sdl_fit(&vr, &doc, win_w, win_h, MARGIN);
                    needs_redraw = true;
                }
                break;
            }
        }

        if (needs_redraw) {
            SDL_SetRenderDrawColor(sdl_renderer, 245, 245, 245, 255);
            SDL_RenderClear(sdl_renderer);

            vvd_sdl_render(&vr, &doc);

            SDL_RenderPresent(sdl_renderer);
            needs_redraw = false;
        } else {
            SDL_Delay(16);
        }
    }

    if (font) TTF_CloseFont(font);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
