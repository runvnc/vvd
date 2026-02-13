/*
 * vvd_sdl.h - SDL2 rendering backend for VVD
 *
 * Usage:
 *   #define VVD_SDL_IMPLEMENTATION
 *   #include "vvd_sdl.h"
 *
 * Requires SDL2 and SDL2_ttf.
 */
#ifndef VVD_SDL_H
#define VVD_SDL_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "vvd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    VvdRenderer base;       /* must be first */
    SDL_Renderer *sdl_renderer;
    TTF_Font *font;
    float scale;            /* pixels per VVD unit */
    float offset_x, offset_y; /* pixel offset for panning */
    int line_thickness;     /* default 2 */
} VvdSdlRenderer;

/* Initialize the SDL renderer with defaults */
void vvd_sdl_init(VvdSdlRenderer *r, SDL_Renderer *sdl_renderer, TTF_Font *font);

/* Compute scale and offset to fit a VVD document in a given pixel rect */
void vvd_sdl_fit(VvdSdlRenderer *r, const VvdDoc *doc, int win_w, int win_h, int margin);

/* Render a VVD document */
void vvd_sdl_render(VvdSdlRenderer *r, const VvdDoc *doc);

#ifdef __cplusplus
}
#endif

/* ================================================================== */
/*  IMPLEMENTATION                                                    */
/* ================================================================== */
#ifdef VVD_SDL_IMPLEMENTATION

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- coordinate transform helpers ---- */

static float vvd_sdl__tx(VvdSdlRenderer *r, float x) {
    return x * r->scale + r->offset_x;
}
static float vvd_sdl__ty(VvdSdlRenderer *r, float y) {
    return y * r->scale + r->offset_y;
}

/* ---- thick line using SDL_RenderDrawLine (multiple offsets) ---- */

static void vvd_sdl__thick_line(SDL_Renderer *sr, int x1, int y1, int x2, int y2, int thickness) {
    if (thickness <= 1) {
        SDL_RenderDrawLine(sr, x1, y1, x2, y2);
        return;
    }
    /* Draw multiple parallel lines for thickness */
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.001f) { SDL_RenderDrawPoint(sr, x1, y1); return; }
    float nx = -dy / len;
    float ny =  dx / len;
    int half = thickness / 2;
    for (int i = -half; i <= half; i++) {
        int ox = (int)(nx * i);
        int oy = (int)(ny * i);
        SDL_RenderDrawLine(sr, x1+ox, y1+oy, x2+ox, y2+oy);
    }
}

/* ---- Callbacks ---- */

static void vvd_sdl__line(VvdRenderer *base, float x1, float y1, float x2, float y2, VvdColor color) {
    VvdSdlRenderer *r = (VvdSdlRenderer *)base;
    SDL_SetRenderDrawColor(r->sdl_renderer, color.r, color.g, color.b, 255);
    int px1 = (int)vvd_sdl__tx(r, x1);
    int py1 = (int)vvd_sdl__ty(r, y1);
    int px2 = (int)vvd_sdl__tx(r, x2);
    int py2 = (int)vvd_sdl__ty(r, y2);
    vvd_sdl__thick_line(r->sdl_renderer, px1, py1, px2, py2, r->line_thickness);
}

static void vvd_sdl__arc(VvdRenderer *base, float cx, float cy, float radius,
                          float start_deg, float end_deg, VvdColor color) {
    VvdSdlRenderer *r = (VvdSdlRenderer *)base;
    SDL_SetRenderDrawColor(r->sdl_renderer, color.r, color.g, color.b, 255);

    float pcx = vvd_sdl__tx(r, cx);
    float pcy = vvd_sdl__ty(r, cy);
    float pr  = radius * r->scale;

    /* Number of segments proportional to arc length */
    float arc_span = end_deg - start_deg;
    if (arc_span < 0) arc_span += 360.0f;
    int segments = (int)(fabsf(arc_span) * pr * M_PI / 180.0f);
    if (segments < 16) segments = 16;
    if (segments > 360) segments = 360;

    float step = arc_span / (float)segments;
    float prev_x = pcx + pr * cosf(start_deg * (float)M_PI / 180.0f);
    float prev_y = pcy + pr * sinf(start_deg * (float)M_PI / 180.0f);

    for (int i = 1; i <= segments; i++) {
        float angle = (start_deg + step * i) * (float)M_PI / 180.0f;
        float nx = pcx + pr * cosf(angle);
        float ny = pcy + pr * sinf(angle);
        vvd_sdl__thick_line(r->sdl_renderer, (int)prev_x, (int)prev_y, (int)nx, (int)ny, r->line_thickness);
        prev_x = nx;
        prev_y = ny;
    }
}

static void vvd_sdl__text(VvdRenderer *base, const char *str, float x, float y,
                           float size, VvdColor color) {
    VvdSdlRenderer *r = (VvdSdlRenderer *)base;
    if (!r->font || !str || !str[0]) return;

    SDL_Color sdl_color = { color.r, color.g, color.b, 255 };
    SDL_Surface *surface = TTF_RenderUTF8_Blended(r->font, str, sdl_color);
    if (!surface) return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(r->sdl_renderer, surface);
    if (!texture) { SDL_FreeSurface(surface); return; }

    /* Scale text: size is cap-height in VVD units */
    float pixel_height = size * r->scale;
    float aspect = (float)surface->w / (float)surface->h;
    int dst_h = (int)pixel_height;
    int dst_w = (int)(pixel_height * aspect);

    /* Center at (x, y) */
    int px = (int)vvd_sdl__tx(r, x) - dst_w / 2;
    int py = (int)vvd_sdl__ty(r, y) - dst_h / 2;

    SDL_Rect dst = { px, py, dst_w, dst_h };
    SDL_RenderCopy(r->sdl_renderer, texture, NULL, &dst);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

/* ---- Public API ---- */

void vvd_sdl_init(VvdSdlRenderer *r, SDL_Renderer *sdl_renderer, TTF_Font *font) {
    memset(r, 0, sizeof(*r));
    r->sdl_renderer = sdl_renderer;
    r->font = font;
    r->scale = 1.0f;
    r->offset_x = 0;
    r->offset_y = 0;
    r->line_thickness = 2;
    r->base.user = r;
    r->base.line = vvd_sdl__line;
    r->base.arc  = vvd_sdl__arc;
    r->base.text = vvd_sdl__text;
}

void vvd_sdl_fit(VvdSdlRenderer *r, const VvdDoc *doc, int win_w, int win_h, int margin) {
    /* Find bounding box by scanning commands */
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    bool found = false;

    for (int i = 0; i < doc->cmd_count; i++) {
        const VvdCmd *c = &doc->cmds[i];
        float px = 0, py = 0;
        bool use = false;
        switch (c->type) {
        case VVD_CMD_MOV: px = c->mov.x; py = c->mov.y; use = true; break;
        case VVD_CMD_LIN: px = c->lin.x; py = c->lin.y; use = true; break;
        case VVD_CMD_ARC:
            /* Include arc bounding box */
            if (c->arc.cx - c->arc.r < min_x) min_x = c->arc.cx - c->arc.r;
            if (c->arc.cy - c->arc.r < min_y) min_y = c->arc.cy - c->arc.r;
            if (c->arc.cx + c->arc.r > max_x) max_x = c->arc.cx + c->arc.r;
            if (c->arc.cy + c->arc.r > max_y) max_y = c->arc.cy + c->arc.r;
            found = true;
            break;
        case VVD_CMD_SYM: px = c->sym.x; py = c->sym.y; use = true; break;
        case VVD_CMD_TXT: px = c->txt.x; py = c->txt.y; use = true; break;
        default: break;
        }
        if (use) {
            if (px < min_x) min_x = px;
            if (py < min_y) min_y = py;
            if (px > max_x) max_x = px;
            if (py > max_y) max_y = py;
            found = true;
        }
    }

    /* Also check SYM definitions for extent */
    for (int i = 0; i < doc->cmd_count; i++) {
        const VvdCmd *c = &doc->cmds[i];
        if (c->type == VVD_CMD_SYM) {
            const VvdDef *def = NULL;
            for (int d = 0; d < doc->def_count; d++) {
                if (doc->defs[d].id == c->sym.id) { def = &doc->defs[d]; break; }
            }
            if (!def) continue;
            for (int s = 0; s < def->sub_count; s++) {
                float sx = 0, sy = 0;
                switch (def->subs[s].type) {
                case VVD_CMD_MOV: sx = def->subs[s].mov.x; sy = def->subs[s].mov.y; break;
                case VVD_CMD_LIN: sx = def->subs[s].lin.x; sy = def->subs[s].lin.y; break;
                case VVD_CMD_ARC:
                    sx = def->subs[s].arc.r; sy = def->subs[s].arc.r;
                    if (c->sym.x - sx < min_x) min_x = c->sym.x - sx;
                    if (c->sym.y - sy < min_y) min_y = c->sym.y - sy;
                    if (c->sym.x + sx > max_x) max_x = c->sym.x + sx;
                    if (c->sym.y + sy > max_y) max_y = c->sym.y + sy;
                    continue;
                default: continue;
                }
                float wx = c->sym.x + sx, wy = c->sym.y + sy;
                if (wx < min_x) min_x = wx;
                if (wy < min_y) min_y = wy;
                if (wx > max_x) max_x = wx;
                if (wy > max_y) max_y = wy;
            }
        }
    }

    if (!found) { r->scale = 1.0f; r->offset_x = 0; r->offset_y = 0; return; }

    float vvd_w = max_x - min_x;
    float vvd_h = max_y - min_y;
    if (vvd_w < 1) vvd_w = 1;
    if (vvd_h < 1) vvd_h = 1;

    float avail_w = (float)(win_w - 2 * margin);
    float avail_h = (float)(win_h - 2 * margin);
    float sx = avail_w / vvd_w;
    float sy = avail_h / vvd_h;
    r->scale = (sx < sy) ? sx : sy;

    float cx = (min_x + max_x) / 2.0f;
    float cy = (min_y + max_y) / 2.0f;
    r->offset_x = (float)win_w / 2.0f - cx * r->scale;
    r->offset_y = (float)win_h / 2.0f - cy * r->scale;
}

void vvd_sdl_render(VvdSdlRenderer *r, const VvdDoc *doc) {
    vvd_render(doc, &r->base);
}

#endif /* VVD_SDL_IMPLEMENTATION */
#endif /* VVD_SDL_H */
