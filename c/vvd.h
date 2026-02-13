/*
 * vvd.h - VVD (Versatile Vector Diagram) C library
 * Single-header library for parsing, rendering, and writing VVD files.
 *
 * Usage:
 *   #define VVD_IMPLEMENTATION
 *   #include "vvd.h"
 *
 * in exactly ONE C file before including this header.
 */
#ifndef VVD_H
#define VVD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */

#define VVD_MAGIC       "VVD1"
#define VVD_VERSION     0x02
#define VVD_MAX_PALETTE 256
#define VVD_MAX_DEFS    256
#define VVD_MAX_CMDS    4096
#define VVD_MAX_SUBCMDS 1024
#define VVD_MAX_TXT_LEN 255
#define VVD_MAX_INC_LEN 255

/* Opcodes (binary format) */
#define VVD_OP_PAL  0x01
#define VVD_OP_DEF  0x02
#define VVD_OP_SIZ  0x03
#define VVD_OP_ROT  0x04
#define VVD_OP_MOV  0x05
#define VVD_OP_LIN  0x06
#define VVD_OP_ARC  0x07
#define VVD_OP_SYM  0x08
#define VVD_OP_TXT  0x09
#define VVD_OP_INC  0x0A
#define VVD_OP_END  0xFF

/* DEF sub-command tags */
#define VVD_SUB_MOV 0x00
#define VVD_SUB_LIN 0x01
#define VVD_SUB_ARC 0x02

/* ------------------------------------------------------------------ */
/*  Types                                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    VVD_CMD_PAL,
    VVD_CMD_DEF,
    VVD_CMD_SIZ,
    VVD_CMD_ROT,
    VVD_CMD_MOV,
    VVD_CMD_LIN,
    VVD_CMD_ARC,
    VVD_CMD_SYM,
    VVD_CMD_TXT,
    VVD_CMD_INC,
    VVD_CMD_END
} VvdCmdType;

typedef struct {
    uint8_t r, g, b;
} VvdColor;

/* A single sub-command inside a DEF (no color) */
typedef struct {
    VvdCmdType type; /* MOV, LIN, or ARC only */
    union {
        struct { float x, y; } mov;
        struct { float x, y; } lin;
        struct { float cx, cy, r, start_deg, end_deg; } arc;
    };
} VvdSubCmd;

/* A stored definition */
typedef struct {
    uint16_t   id;
    int        sub_count;
    VvdSubCmd  subs[VVD_MAX_SUBCMDS];
} VvdDef;

/* A top-level command */
typedef struct {
    VvdCmdType type;
    union {
        struct { uint8_t id; uint8_t r, g, b; } pal;
        struct { uint16_t id; } def;
        struct { float scale; } siz;
        struct { float degrees; } rot;
        struct { float x, y; } mov;
        struct { float x, y; uint8_t col; } lin;
        struct { float cx, cy, r, start_deg, end_deg; uint8_t col; } arc;
        struct { uint16_t id; float x, y; uint8_t col; } sym;
        struct { char str[VVD_MAX_TXT_LEN + 1]; float x, y, size; uint8_t col; } txt;
        struct { char filename[VVD_MAX_INC_LEN + 1]; } inc;
    };
} VvdCmd;

/* Full document */
typedef struct {
    VvdColor   palette[VVD_MAX_PALETTE];
    VvdDef     defs[VVD_MAX_DEFS];
    int        def_count;
    VvdCmd     cmds[VVD_MAX_CMDS];
    int        cmd_count;
    uint16_t   coord_scale; /* from binary header, default 1 */
} VvdDoc;

/* ------------------------------------------------------------------ */
/*  Render callback interface                                         */
/* ------------------------------------------------------------------ */

typedef struct VvdRenderer VvdRenderer;

struct VvdRenderer {
    void *user;  /* opaque user data */
    void (*line)(VvdRenderer *r, float x1, float y1, float x2, float y2,
                 VvdColor color);
    void (*arc)(VvdRenderer *r, float cx, float cy, float radius,
                float start_deg, float end_deg, VvdColor color);
    void (*text)(VvdRenderer *r, const char *str, float x, float y,
                 float size, VvdColor color);
};

/* ------------------------------------------------------------------ */
/*  API                                                               */
/* ------------------------------------------------------------------ */

void vvd_init(VvdDoc *doc);
int vvd_parse_text(VvdDoc *doc, const char *text);
int vvd_parse_binary(VvdDoc *doc, const uint8_t *data, size_t len);
int vvd_write_binary(const VvdDoc *doc, uint8_t *buf, size_t buf_size);
void vvd_render(const VvdDoc *doc, VvdRenderer *renderer);
char *vvd_load_file(const char *path, size_t *out_len);
int vvd_parse_file(VvdDoc *doc, const char *path);

#ifdef __cplusplus
}
#endif

/* ================================================================== */
/*  IMPLEMENTATION                                                    */
/* ================================================================== */
#ifdef VVD_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint16_t vvd__read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static int16_t vvd__read_i16(const uint8_t *p) {
    return (int16_t)(p[0] | (p[1] << 8));
}
static void vvd__write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void vvd__write_i16(uint8_t *p, int16_t v) {
    vvd__write_u16(p, (uint16_t)v);
}

static const char *vvd__skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == ',')) p++;
    return p;
}
static const char *vvd__skip_line(const char *p) {
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
    return p;
}
static float vvd__parse_float(const char **pp) {
    const char *p = vvd__skip_ws(*pp);
    float val = (float)strtod(p, (char **)&p);
    *pp = p;
    return val;
}
static int vvd__parse_int(const char **pp) {
    const char *p = vvd__skip_ws(*pp);
    int val = (int)strtol(p, (char **)&p, 10);
    *pp = p;
    return val;
}
static void vvd__parse_quoted_string(const char **pp, char *out, int max_len) {
    const char *p = vvd__skip_ws(*pp);
    if (*p == '"') p++;
    int i = 0;
    while (*p && *p != '"' && i < max_len) {
        if (*p == '\\' && *(p+1)) { p++; }
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    *pp = p;
}
static uint32_t vvd__parse_hex_color(const char **pp) {
    const char *p = vvd__skip_ws(*pp);
    if (*p == '#') p++;
    uint32_t val = (uint32_t)strtoul(p, (char **)&p, 16);
    *pp = p;
    return val;
}
static VvdDef *vvd__find_def(const VvdDoc *doc, uint16_t id) {
    for (int i = 0; i < doc->def_count; i++) {
        if (doc->defs[i].id == id) return (VvdDef *)&doc->defs[i];
    }
    return NULL;
}

void vvd_init(VvdDoc *doc) {
    memset(doc, 0, sizeof(*doc));
    doc->coord_scale = 1;
}

int vvd_parse_text(VvdDoc *doc, const char *text) {
    const char *p = text;
    bool in_def = false;
    VvdDef *cur_def = NULL;

    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (strncmp(p, "VVD1", 4) == 0) {
        p += 4;
        p = vvd__skip_line(p);
    }

    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (!*p) break;
        if (*p == '#') { p = vvd__skip_line(p); continue; }

        char kw[8] = {0};
        int ki = 0;
        while (*p && isalpha((unsigned char)*p) && ki < 7) kw[ki++] = *p++;
        kw[ki] = '\0';

        if (strcmp(kw, "PAL") == 0) {
            int id = vvd__parse_int(&p);
            uint32_t hex = vvd__parse_hex_color(&p);
            if (id >= 0 && id < VVD_MAX_PALETTE) {
                doc->palette[id].r = (hex >> 16) & 0xFF;
                doc->palette[id].g = (hex >> 8) & 0xFF;
                doc->palette[id].b = hex & 0xFF;
            }
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++];
                c->type = VVD_CMD_PAL;
                c->pal.id = (uint8_t)id;
                c->pal.r = (hex >> 16) & 0xFF;
                c->pal.g = (hex >> 8) & 0xFF;
                c->pal.b = hex & 0xFF;
            }
        } else if (strcmp(kw, "DEF") == 0) {
            int id = vvd__parse_int(&p);
            if (doc->def_count < VVD_MAX_DEFS) {
                cur_def = &doc->defs[doc->def_count++];
                cur_def->id = (uint16_t)id;
                cur_def->sub_count = 0;
                in_def = true;
            }
        } else if (strcmp(kw, "END") == 0) {
            in_def = false;
            cur_def = NULL;
        } else if (strcmp(kw, "MOV") == 0) {
            float x = vvd__parse_float(&p);
            float y = vvd__parse_float(&p);
            if (in_def && cur_def) {
                if (cur_def->sub_count < VVD_MAX_SUBCMDS) {
                    VvdSubCmd *s = &cur_def->subs[cur_def->sub_count++];
                    s->type = VVD_CMD_MOV; s->mov.x = x; s->mov.y = y;
                }
            } else if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++];
                c->type = VVD_CMD_MOV; c->mov.x = x; c->mov.y = y;
            }
        } else if (strcmp(kw, "LIN") == 0) {
            float x = vvd__parse_float(&p);
            float y = vvd__parse_float(&p);
            if (in_def && cur_def) {
                if (cur_def->sub_count < VVD_MAX_SUBCMDS) {
                    VvdSubCmd *s = &cur_def->subs[cur_def->sub_count++];
                    s->type = VVD_CMD_LIN; s->lin.x = x; s->lin.y = y;
                }
            } else {
                int col = vvd__parse_int(&p);
                if (doc->cmd_count < VVD_MAX_CMDS) {
                    VvdCmd *c = &doc->cmds[doc->cmd_count++];
                    c->type = VVD_CMD_LIN; c->lin.x = x; c->lin.y = y; c->lin.col = (uint8_t)col;
                }
            }
        } else if (strcmp(kw, "ARC") == 0) {
            float cx = vvd__parse_float(&p);
            float cy = vvd__parse_float(&p);
            float r  = vvd__parse_float(&p);
            float sd = vvd__parse_float(&p);
            float ed = vvd__parse_float(&p);
            if (in_def && cur_def) {
                if (cur_def->sub_count < VVD_MAX_SUBCMDS) {
                    VvdSubCmd *s = &cur_def->subs[cur_def->sub_count++];
                    s->type = VVD_CMD_ARC;
                    s->arc.cx = cx; s->arc.cy = cy; s->arc.r = r;
                    s->arc.start_deg = sd; s->arc.end_deg = ed;
                }
            } else {
                int col = vvd__parse_int(&p);
                if (doc->cmd_count < VVD_MAX_CMDS) {
                    VvdCmd *c = &doc->cmds[doc->cmd_count++];
                    c->type = VVD_CMD_ARC;
                    c->arc.cx = cx; c->arc.cy = cy; c->arc.r = r;
                    c->arc.start_deg = sd; c->arc.end_deg = ed; c->arc.col = (uint8_t)col;
                }
            }
        } else if (strcmp(kw, "SIZ") == 0) {
            float scale = vvd__parse_float(&p);
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++];
                c->type = VVD_CMD_SIZ; c->siz.scale = scale;
            }
        } else if (strcmp(kw, "ROT") == 0) {
            float deg = vvd__parse_float(&p);
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++];
                c->type = VVD_CMD_ROT; c->rot.degrees = deg;
            }
        } else if (strcmp(kw, "SYM") == 0) {
            int id   = vvd__parse_int(&p);
            float x  = vvd__parse_float(&p);
            float y  = vvd__parse_float(&p);
            int col  = vvd__parse_int(&p);
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++];
                c->type = VVD_CMD_SYM;
                c->sym.id = (uint16_t)id; c->sym.x = x; c->sym.y = y; c->sym.col = (uint8_t)col;
            }
        } else if (strcmp(kw, "TXT") == 0) {
            char str[VVD_MAX_TXT_LEN + 1];
            vvd__parse_quoted_string(&p, str, VVD_MAX_TXT_LEN);
            float x    = vvd__parse_float(&p);
            float y    = vvd__parse_float(&p);
            float size = vvd__parse_float(&p);
            int col    = vvd__parse_int(&p);
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++];
                c->type = VVD_CMD_TXT;
                snprintf(c->txt.str, sizeof(c->txt.str), "%s", str);
                c->txt.str[VVD_MAX_TXT_LEN] = '\0';
                c->txt.x = x; c->txt.y = y; c->txt.size = size; c->txt.col = (uint8_t)col;
            }
        } else if (strcmp(kw, "INC") == 0) {
            char fname[VVD_MAX_INC_LEN + 1];
            vvd__parse_quoted_string(&p, fname, VVD_MAX_INC_LEN);
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++];
                c->type = VVD_CMD_INC;
                snprintf(c->inc.filename, sizeof(c->inc.filename), "%s", fname);
                c->inc.filename[VVD_MAX_INC_LEN] = '\0';
            }
        }
        p = vvd__skip_line(p);
    }
    return 0;
}

int vvd_parse_binary(VvdDoc *doc, const uint8_t *data, size_t len) {
    if (len < 7) return -1;
    if (memcmp(data, VVD_MAGIC, 4) != 0) return -1;
    doc->coord_scale = vvd__read_u16(data + 5);
    if (doc->coord_scale == 0) doc->coord_scale = 1;
    float cs = (float)doc->coord_scale;
    size_t pos = 7;

    while (pos < len) {
        uint8_t op = data[pos++];
        if (op == VVD_OP_END) break;
        switch (op) {
        case VVD_OP_PAL: {
            if (pos + 4 > len) return -1;
            uint8_t id = data[pos++], r = data[pos++], g = data[pos++], b = data[pos++];
            { doc->palette[id].r = r; doc->palette[id].g = g; doc->palette[id].b = b;  }
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++];
                c->type = VVD_CMD_PAL; c->pal.id = id; c->pal.r = r; c->pal.g = g; c->pal.b = b;
            }
            break;
        }
        case VVD_OP_DEF: {
            if (pos + 4 > len) return -1;
            uint16_t id = vvd__read_u16(data + pos); pos += 2;
            uint16_t sc = vvd__read_u16(data + pos); pos += 2;
            VvdDef *def = NULL;
            if (doc->def_count < VVD_MAX_DEFS) {
                def = &doc->defs[doc->def_count++]; def->id = id; def->sub_count = 0;
            }
            for (int i = 0; i < (int)sc; i++) {
                if (pos >= len) return -1;
                uint8_t tag = data[pos++];
                if (tag == VVD_SUB_MOV) {
                    if (pos + 4 > len) return -1;
                    float x = vvd__read_i16(data+pos)/cs; pos+=2;
                    float y = vvd__read_i16(data+pos)/cs; pos+=2;
                    if (def && def->sub_count < VVD_MAX_SUBCMDS) {
                        VvdSubCmd *s = &def->subs[def->sub_count++];
                        s->type = VVD_CMD_MOV; s->mov.x = x; s->mov.y = y;
                    }
                } else if (tag == VVD_SUB_LIN) {
                    if (pos + 4 > len) return -1;
                    float x = vvd__read_i16(data+pos)/cs; pos+=2;
                    float y = vvd__read_i16(data+pos)/cs; pos+=2;
                    if (def && def->sub_count < VVD_MAX_SUBCMDS) {
                        VvdSubCmd *s = &def->subs[def->sub_count++];
                        s->type = VVD_CMD_LIN; s->lin.x = x; s->lin.y = y;
                    }
                } else if (tag == VVD_SUB_ARC) {
                    if (pos + 10 > len) return -1;
                    float cx2 = vvd__read_i16(data+pos)/cs; pos+=2;
                    float cy2 = vvd__read_i16(data+pos)/cs; pos+=2;
                    float r2  = vvd__read_u16(data+pos)/cs; pos+=2;
                    float sd = vvd__read_i16(data+pos)/10.0f; pos+=2;
                    float ed = vvd__read_i16(data+pos)/10.0f; pos+=2;
                    if (def && def->sub_count < VVD_MAX_SUBCMDS) {
                        VvdSubCmd *s = &def->subs[def->sub_count++];
                        s->type = VVD_CMD_ARC;
                        s->arc.cx = cx2; s->arc.cy = cy2; s->arc.r = r2;
                        s->arc.start_deg = sd; s->arc.end_deg = ed;
                    }
                } else return -1;
            }
            break;
        }
        case VVD_OP_SIZ: {
            if (pos+2>len) return -1;
            float scale = vvd__read_u16(data+pos)/100.0f; pos+=2;
            if (doc->cmd_count < VVD_MAX_CMDS) { VvdCmd *c = &doc->cmds[doc->cmd_count++]; c->type = VVD_CMD_SIZ; c->siz.scale = scale; }
            break;
        }
        case VVD_OP_ROT: {
            if (pos+2>len) return -1;
            float deg = vvd__read_i16(data+pos)/10.0f; pos+=2;
            if (doc->cmd_count < VVD_MAX_CMDS) { VvdCmd *c = &doc->cmds[doc->cmd_count++]; c->type = VVD_CMD_ROT; c->rot.degrees = deg; }
            break;
        }
        case VVD_OP_MOV: {
            if (pos+4>len) return -1;
            float x = vvd__read_i16(data+pos)/cs; pos+=2;
            float y = vvd__read_i16(data+pos)/cs; pos+=2;
            if (doc->cmd_count < VVD_MAX_CMDS) { VvdCmd *c = &doc->cmds[doc->cmd_count++]; c->type = VVD_CMD_MOV; c->mov.x = x; c->mov.y = y; }
            break;
        }
        case VVD_OP_LIN: {
            if (pos+5>len) return -1;
            float x = vvd__read_i16(data+pos)/cs; pos+=2;
            float y = vvd__read_i16(data+pos)/cs; pos+=2;
            uint8_t col = data[pos++];
            if (doc->cmd_count < VVD_MAX_CMDS) { VvdCmd *c = &doc->cmds[doc->cmd_count++]; c->type = VVD_CMD_LIN; c->lin.x = x; c->lin.y = y; c->lin.col = col; }
            break;
        }
        case VVD_OP_ARC: {
            if (pos+11>len) return -1;
            float cx2 = vvd__read_i16(data+pos)/cs; pos+=2;
            float cy2 = vvd__read_i16(data+pos)/cs; pos+=2;
            float r2  = vvd__read_u16(data+pos)/cs; pos+=2;
            float sd = vvd__read_i16(data+pos)/10.0f; pos+=2;
            float ed = vvd__read_i16(data+pos)/10.0f; pos+=2;
            uint8_t col = data[pos++];
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++]; c->type = VVD_CMD_ARC;
                c->arc.cx = cx2; c->arc.cy = cy2; c->arc.r = r2;
                c->arc.start_deg = sd; c->arc.end_deg = ed; c->arc.col = col;
            }
            break;
        }
        case VVD_OP_SYM: {
            if (pos+7>len) return -1;
            uint16_t id = vvd__read_u16(data+pos); pos+=2;
            float x = vvd__read_i16(data+pos)/cs; pos+=2;
            float y = vvd__read_i16(data+pos)/cs; pos+=2;
            uint8_t col = data[pos++];
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++]; c->type = VVD_CMD_SYM;
                c->sym.id = id; c->sym.x = x; c->sym.y = y; c->sym.col = col;
            }
            break;
        }
        case VVD_OP_TXT: {
            if (pos+1>len) return -1;
            uint8_t slen = data[pos++];
            if (pos+slen+7>len) return -1;
            char str[VVD_MAX_TXT_LEN+1];
            memcpy(str, data+pos, slen); str[slen] = '\0'; pos += slen;
            float x = vvd__read_i16(data+pos)/cs; pos+=2;
            float y = vvd__read_i16(data+pos)/cs; pos+=2;
            float size = vvd__read_u16(data+pos)/10.0f; pos+=2;
            uint8_t col = data[pos++];
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++]; c->type = VVD_CMD_TXT;
                snprintf(c->txt.str, sizeof(c->txt.str), "%s", str); c->txt.str[VVD_MAX_TXT_LEN] = '\0';
                c->txt.x = x; c->txt.y = y; c->txt.size = size; c->txt.col = col;
            }
            break;
        }
        case VVD_OP_INC: {
            if (pos+1>len) return -1;
            uint8_t slen = data[pos++];
            if (pos+slen>len) return -1;
            char fname[VVD_MAX_INC_LEN+1];
            memcpy(fname, data+pos, slen); fname[slen] = '\0'; pos += slen;
            if (doc->cmd_count < VVD_MAX_CMDS) {
                VvdCmd *c = &doc->cmds[doc->cmd_count++]; c->type = VVD_CMD_INC;
                snprintf(c->inc.filename, sizeof(c->inc.filename), "%s", fname); c->inc.filename[VVD_MAX_INC_LEN] = '\0';
            }
            break;
        }
        default: return -1;
        }
    }
    return 0;
}

/* Binary Writer */
#define VVD__WBYTE(b) do { if (buf && off < buf_size) buf[off] = (b); off++; } while(0)
#define VVD__WU16(v) do { uint8_t _t[2]; vvd__write_u16(_t,(v)); VVD__WBYTE(_t[0]); VVD__WBYTE(_t[1]); } while(0)
#define VVD__WI16(v) do { uint8_t _t[2]; vvd__write_i16(_t,(v)); VVD__WBYTE(_t[0]); VVD__WBYTE(_t[1]); } while(0)

int vvd_write_binary(const VvdDoc *doc, uint8_t *buf, size_t buf_size) {
    size_t off = 0;
    uint16_t cs = doc->coord_scale ? doc->coord_scale : 1;
    float csf = (float)cs;

    VVD__WBYTE('V'); VVD__WBYTE('V'); VVD__WBYTE('D'); VVD__WBYTE('1');
    VVD__WBYTE(VVD_VERSION);
    VVD__WU16(cs);

    for (int d = 0; d < doc->def_count; d++) {
        const VvdDef *def = &doc->defs[d];
        VVD__WBYTE(VVD_OP_DEF); VVD__WU16(def->id); VVD__WU16((uint16_t)def->sub_count);
        for (int s = 0; s < def->sub_count; s++) {
            const VvdSubCmd *sc = &def->subs[s];
            switch (sc->type) {
            case VVD_CMD_MOV:
                VVD__WBYTE(VVD_SUB_MOV);
                VVD__WI16((int16_t)(sc->mov.x*csf)); VVD__WI16((int16_t)(sc->mov.y*csf));
                break;
            case VVD_CMD_LIN:
                VVD__WBYTE(VVD_SUB_LIN);
                VVD__WI16((int16_t)(sc->lin.x*csf)); VVD__WI16((int16_t)(sc->lin.y*csf));
                break;
            case VVD_CMD_ARC:
                VVD__WBYTE(VVD_SUB_ARC);
                VVD__WI16((int16_t)(sc->arc.cx*csf)); VVD__WI16((int16_t)(sc->arc.cy*csf));
                VVD__WU16((uint16_t)(sc->arc.r*csf));
                VVD__WI16((int16_t)(sc->arc.start_deg*10.0f)); VVD__WI16((int16_t)(sc->arc.end_deg*10.0f));
                break;
            default: break;
            }
        }
    }

    for (int i = 0; i < doc->cmd_count; i++) {
        const VvdCmd *c = &doc->cmds[i];
        switch (c->type) {
        case VVD_CMD_PAL:
            VVD__WBYTE(VVD_OP_PAL); VVD__WBYTE(c->pal.id);
            VVD__WBYTE(c->pal.r); VVD__WBYTE(c->pal.g); VVD__WBYTE(c->pal.b);
            break;
        case VVD_CMD_SIZ:
            VVD__WBYTE(VVD_OP_SIZ); VVD__WU16((uint16_t)(c->siz.scale*100.0f));
            break;
        case VVD_CMD_ROT:
            VVD__WBYTE(VVD_OP_ROT); VVD__WI16((int16_t)(c->rot.degrees*10.0f));
            break;
        case VVD_CMD_MOV:
            VVD__WBYTE(VVD_OP_MOV); VVD__WI16((int16_t)(c->mov.x*csf)); VVD__WI16((int16_t)(c->mov.y*csf));
            break;
        case VVD_CMD_LIN:
            VVD__WBYTE(VVD_OP_LIN); VVD__WI16((int16_t)(c->lin.x*csf)); VVD__WI16((int16_t)(c->lin.y*csf));
            VVD__WBYTE(c->lin.col);
            break;
        case VVD_CMD_ARC:
            VVD__WBYTE(VVD_OP_ARC);
            VVD__WI16((int16_t)(c->arc.cx*csf)); VVD__WI16((int16_t)(c->arc.cy*csf));
            VVD__WU16((uint16_t)(c->arc.r*csf));
            VVD__WI16((int16_t)(c->arc.start_deg*10.0f)); VVD__WI16((int16_t)(c->arc.end_deg*10.0f));
            VVD__WBYTE(c->arc.col);
            break;
        case VVD_CMD_SYM:
            VVD__WBYTE(VVD_OP_SYM); VVD__WU16(c->sym.id);
            VVD__WI16((int16_t)(c->sym.x*csf)); VVD__WI16((int16_t)(c->sym.y*csf));
            VVD__WBYTE(c->sym.col);
            break;
        case VVD_CMD_TXT: {
            VVD__WBYTE(VVD_OP_TXT);
            uint8_t slen = (uint8_t)strlen(c->txt.str);
            VVD__WBYTE(slen);
            for (int j = 0; j < slen; j++) VVD__WBYTE((uint8_t)c->txt.str[j]);
            VVD__WI16((int16_t)(c->txt.x*csf)); VVD__WI16((int16_t)(c->txt.y*csf));
            VVD__WU16((uint16_t)(c->txt.size*10.0f)); VVD__WBYTE(c->txt.col);
            break;
        }
        case VVD_CMD_INC: {
            VVD__WBYTE(VVD_OP_INC);
            uint8_t slen = (uint8_t)strlen(c->inc.filename);
            VVD__WBYTE(slen);
            for (int j = 0; j < slen; j++) VVD__WBYTE((uint8_t)c->inc.filename[j]);
            break;
        }
        default: break;
        }
    }
    VVD__WBYTE(VVD_OP_END);
    return (int)off;
}
#undef VVD__WBYTE
#undef VVD__WU16
#undef VVD__WI16

void vvd_render(const VvdDoc *doc, VvdRenderer *renderer) {
    float pen_x = 0, pen_y = 0;
    float cur_siz = 1.0f;
    float cur_rot = 0.0f;
    VvdColor palette[VVD_MAX_PALETTE];
    memcpy(palette, doc->palette, sizeof(palette));

    for (int i = 0; i < doc->cmd_count; i++) {
        const VvdCmd *c = &doc->cmds[i];
        switch (c->type) {
        case VVD_CMD_PAL:
            palette[c->pal.id].r = c->pal.r;
            palette[c->pal.id].g = c->pal.g;
            palette[c->pal.id].b = c->pal.b;
            break;
        case VVD_CMD_SIZ: cur_siz = c->siz.scale; break;
        case VVD_CMD_ROT: cur_rot = c->rot.degrees; break;
        case VVD_CMD_MOV: pen_x = c->mov.x; pen_y = c->mov.y; break;
        case VVD_CMD_LIN: {
            VvdColor col = palette[c->lin.col];
            if (renderer->line) renderer->line(renderer, pen_x, pen_y, c->lin.x, c->lin.y, col);
            pen_x = c->lin.x; pen_y = c->lin.y;
            break;
        }
        case VVD_CMD_ARC: {
            VvdColor col = palette[c->arc.col];
            if (renderer->arc) renderer->arc(renderer, c->arc.cx, c->arc.cy, c->arc.r, c->arc.start_deg, c->arc.end_deg, col);
            float end_rad = c->arc.end_deg * (float)M_PI / 180.0f;
            pen_x = c->arc.cx + c->arc.r * cosf(end_rad);
            pen_y = c->arc.cy + c->arc.r * sinf(end_rad);
            break;
        }
        case VVD_CMD_SYM: {
            const VvdDef *def = vvd__find_def(doc, c->sym.id);
            if (!def) break;
            VvdColor col = palette[c->sym.col];
            float rad = cur_rot * (float)M_PI / 180.0f;
            float cos_r = cosf(rad), sin_r = sinf(rad);
            float lx = 0, ly = 0;
            for (int s = 0; s < def->sub_count; s++) {
                const VvdSubCmd *sc = &def->subs[s];
                switch (sc->type) {
                case VVD_CMD_MOV: {
                    float sx = sc->mov.x * cur_siz, sy = sc->mov.y * cur_siz;
                    lx = sx*cos_r - sy*sin_r; ly = sx*sin_r + sy*cos_r;
                    break;
                }
                case VVD_CMD_LIN: {
                    float sx = sc->lin.x * cur_siz, sy = sc->lin.y * cur_siz;
                    float nx = sx*cos_r - sy*sin_r, ny = sx*sin_r + sy*cos_r;
                    if (renderer->line) renderer->line(renderer, c->sym.x+lx, c->sym.y+ly, c->sym.x+nx, c->sym.y+ny, col);
                    lx = nx; ly = ny;
                    break;
                }
                case VVD_CMD_ARC: {
                    float acx = sc->arc.cx*cur_siz, acy = sc->arc.cy*cur_siz;
                    float rcx = acx*cos_r - acy*sin_r, rcy = acx*sin_r + acy*cos_r;
                    float ar = sc->arc.r * cur_siz;
                    if (renderer->arc) renderer->arc(renderer, c->sym.x+rcx, c->sym.y+rcy, ar, sc->arc.start_deg+cur_rot, sc->arc.end_deg+cur_rot, col);
                    float elr = (sc->arc.end_deg+cur_rot)*(float)M_PI/180.0f;
                    lx = rcx + ar*cosf(elr); ly = rcy + ar*sinf(elr);
                    break;
                }
                default: break;
                }
            }
            cur_rot = 0;
            break;
        }
        case VVD_CMD_TXT: {
            VvdColor col = palette[c->txt.col];
            if (renderer->text) renderer->text(renderer, c->txt.str, c->txt.x, c->txt.y, c->txt.size, col);
            break;
        }
        default: break;
        }
    }
}

char *vvd_load_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    return buf;
}

int vvd_parse_file(VvdDoc *doc, const char *path) {
    size_t len = 0;
    char *data = vvd_load_file(path, &len);
    if (!data) return -1;
    int ret;
    if (len >= 7 && memcmp(data, VVD_MAGIC, 4) == 0 && (uint8_t)data[4] == VVD_VERSION) {
        ret = vvd_parse_binary(doc, (const uint8_t *)data, len);
    } else {
        ret = vvd_parse_text(doc, data);
    }
    free(data);
    return ret;
}

#endif /* VVD_IMPLEMENTATION */
#endif /* VVD_H */
