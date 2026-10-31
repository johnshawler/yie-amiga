#include <string.h>
#include "render.h"

static const uint8_t *tv[4];            /* tile variants: [v][code][row 8][plane 4]      v = flipx | flipy << 1 */
static const uint16_t *sv[4];           /* sprite variants: [v][code][row 16][plane 4]   (host-endian 16-bit) */
static uint8_t *bg;                     /* tile layer, R_BUFSZ */
static uint8_t *bgshadow;               /* tile RAM as drawn into bg (2048) */
static int bgflip = -1;

static uint8_t rev8(uint8_t v) { v = (v >> 4) | (v << 4); v = ((v & 0xCC) >> 2) | ((v & 0x33) << 2); return ((v & 0xAA) >> 1) | ((v & 0x55) << 1); }
static uint16_t rev16(uint16_t v) { return (uint16_t)((rev8(v & 255) << 8) | rev8(v >> 8)); }

void render_init(const uint8_t *gfx, uint8_t *w) {
    uint8_t *t = w;                                  /* 4 x 512 x 32 = 64 KB */
    uint16_t *s = (uint16_t *)(w + 65536);           /* 4 x 512 x 128 = 256 KB */
    for (int v = 0; v < 4; v++) {
        tv[v] = t + v * 512 * 32; sv[v] = s + v * 512 * 64;
        for (int c = 0; c < 512; c++) {
            for (int r = 0; r < 8; r++) for (int p = 0; p < 4; p++) {
                uint8_t b = gfx[c * 32 + ((v & 2) ? 7 - r : r) * 4 + p];
                t[(v * 512 + c) * 32 + r * 4 + p] = (v & 1) ? rev8(b) : b;
            }
            for (int r = 0; r < 16; r++) for (int p = 0; p < 4; p++) {
                const uint8_t *g = gfx + 16384 + c * 128 + ((v & 2) ? 15 - r : r) * 8 + p * 2;
                uint16_t b = (uint16_t)((g[0] << 8) | g[1]);
                s[(v * 512 + c) * 64 + r * 4 + p] = (v & 1) ? rev16(b) : b;
            }
        }
    }
    bg = w + 65536 * 5;
    bgshadow = bg + R_BUFSZ;
    memset(bg, 0, R_BUFSZ + 2048);
    bgflip = -1;
}

void render_view_init(rview *v, uint8_t *buf) { memset(v, 0, sizeof *v); v->buf = buf; v->flip = -1; }

/* draw tile cell (displayed column dc, displayed tile row dr) from `code`, flips into buffer b */
static void draw_cell(uint8_t *b, int dc, int dr, int code, int fx, int fy) {
    if (dr < 2 || dr > 29) return;
    const uint8_t *g = tv[fx | (fy << 1)] + code * 32;
    uint8_t *d = b + (dr * 8 - 16) * R_ROWB + dc;
    for (int r = 0; r < 8; r++, d += R_ROWB, g += 4) {
        d[0] = g[0]; d[32] = g[1]; d[64] = g[2]; d[96] = g[3]; d[128] = 0xFF;
    }
}
static void copy_cell(uint8_t *dst, const uint8_t *src, int dc, int dr) {
    if (dr < 2 || dr > 29) return;
    int o = (dr * 8 - 16) * R_ROWB + dc;
    for (int r = 0; r < 8; r++, o += R_ROWB) { dst[o] = src[o]; dst[o + 32] = src[o + 32]; dst[o + 64] = src[o + 64]; dst[o + 96] = src[o + 96]; dst[o + 128] = src[o + 128]; }
}

static void cell_pos(int ti, int flip, int *dc, int *dr) { int c = ti & 31, r = ti >> 5; if (flip) { c = 31 - c; r = 31 - r; } *dc = c; *dr = r; }

static void bg_update(const uint8_t *vram, int flip) {
    int all = (flip != bgflip);
    for (int ti = 0; ti < 1024; ti++) {
        uint8_t a = vram[ti * 2], c = vram[ti * 2 + 1];
        if (!all && a == bgshadow[ti * 2] && c == bgshadow[ti * 2 + 1]) continue;
        bgshadow[ti * 2] = a; bgshadow[ti * 2 + 1] = c;
        int dc, dr; cell_pos(ti, flip, &dc, &dr);
        draw_cell(bg, dc, dr, c | ((a & 0x10) << 4), ((a >> 7) & 1) ^ flip, ((a >> 6) & 1) ^ flip);
    }
    bgflip = flip;
}

static void restore_rect(uint8_t *b, const rrect *q) {
    for (int y = q->y0; y <= q->y1; y++) {
        int o = y * R_ROWB;
        for (int c = q->c0; c <= q->c1; c++) { int k = o + c; b[k] = bg[k]; b[k + 32] = bg[k + 32]; b[k + 64] = bg[k + 64]; b[k + 96] = bg[k + 96]; b[k + 128] = bg[k + 128]; }
    }
}

static void draw_sprite(uint8_t *b, int code, int fx, int fy, int sx, int sy, rrect *out) {
    out->c0 = 0; out->c1 = -1;
    int y0 = 0, y1 = 15;
    if (sy + y0 < 16) y0 = 16 - sy;
    if (sy + y1 > 239) y1 = 239 - sy;
    if (y0 > y1) return;
    int s = sx & 7, c0 = sx >> 3;
    int c1 = (sx + 15) >> 3; if (c1 > 31) c1 = 31;
    out->c0 = c0; out->c1 = c1; out->y0 = sy + y0 - 16; out->y1 = sy + y1 - 16;
    const uint16_t *g = sv[fx | (fy << 1)] + code * 64 + y0 * 4;
    for (int r = y0; r <= y1; r++, g += 4) {
        uint16_t w0 = g[0], w1 = g[1], w2 = g[2], w3 = g[3];
        uint16_t m = w0 | w1 | w2 | w3;
        if (!m) continue;
        uint8_t *d = b + (sy + r - 16) * R_ROWB + c0;
        uint32_t M = (uint32_t)m << (8 - s);
        uint32_t P0 = (uint32_t)w0 << (8 - s), P1 = (uint32_t)w1 << (8 - s), P2 = (uint32_t)w2 << (8 - s), P3 = (uint32_t)w3 << (8 - s);
        for (int k = 0; k < 3; k++) {
            int c = c0 + k; if (c > 31) break;
            int sh = 16 - 8 * k;
            uint8_t mk = (uint8_t)(M >> sh);
            if (!mk) continue;
            uint8_t nm = ~mk;
            d[k] = (d[k] & nm) | ((uint8_t)(P0 >> sh) & mk);
            d[k + 32] = (d[k + 32] & nm) | ((uint8_t)(P1 >> sh) & mk);
            d[k + 64] = (d[k + 64] & nm) | ((uint8_t)(P2 >> sh) & mk);
            d[k + 96] = (d[k + 96] & nm) | ((uint8_t)(P3 >> sh) & mk);
            d[k + 128] &= nm;
        }
    }
}

void render_frame(rview *v, const uint8_t *ram, int flip) {
    const uint8_t *vram = ram + 0x800, *s1 = ram, *s2 = ram + 0x400;
    flip = flip ? 1 : 0;
    bg_update(vram, flip);
    uint8_t *b = v->buf;
    if (v->flip != flip) {
        memcpy(b, bg, R_BUFSZ);                          /* first use or flip change: whole tile layer */
        memcpy(v->tshadow, bgshadow, 2048);
        v->nold = 0; v->flip = flip;
    } else {
        for (int ti = 0; ti < 1024; ti++) {
            if (vram[ti * 2] == v->tshadow[ti * 2] && vram[ti * 2 + 1] == v->tshadow[ti * 2 + 1]) continue;
            v->tshadow[ti * 2] = vram[ti * 2]; v->tshadow[ti * 2 + 1] = vram[ti * 2 + 1];
            int dc, dr; cell_pos(ti, flip, &dc, &dr);
            copy_cell(b, bg, dc, dr);
        }
        for (int i = 0; i < v->nold; i++) restore_rect(b, &v->old[i]);
        v->nold = 0;
    }
    for (int offs = 0x2e; offs >= 0; offs -= 2) {
        int attr = s1[offs];
        int code = s2[offs + 1] + 256 * (attr & 1);
        int fx = (~attr >> 6) & 1, fy = (attr >> 7) & 1;
        int sy = 240 - s1[offs + 1], sx = s2[offs];
        if (flip) { sy = 240 - sy; fy ^= 1; }
        if (offs < 0x26) sy++;
        rrect q;
        draw_sprite(b, code, fx, fy, sx, sy, &q);
        if (q.c1 >= q.c0) v->old[v->nold++] = q;
    }
}
