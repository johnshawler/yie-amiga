#include <string.h>
#include "render.h"

static const uint8_t *tv[4];            /* tile variants: [v][code][row 8][plane 4]      v = flipx | flipy << 1 */
static const uint16_t *sv[4];           /* sprite variants: [v][code][row 16][plane 4]   (host-endian 16-bit) */
typedef uint32_t __attribute__((may_alias, aligned(2))) u32a;     /* the 68020 reads longwords at any address (this file assumes a big-endian CPU) */

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
}

void render_view_init(rview *v, uint8_t *buf) { memset(v, 0, sizeof *v); v->buf = buf; v->flip = -1; }

/* tile cell (displayed column dc, displayed tile row dr): code and flips -> playfield 1 */
static void draw_cell(uint8_t *b, int dc, int dr, int code, int fx, int fy) {
    if (dr < 2 || dr > 29) return;
    const uint8_t *g = tv[fx | (fy << 1)] + code * 32;
    uint8_t *d = b + (dr * 8 - 16) * R_ROWB + dc;
    for (int r = 0; r < 8; r++, d += R_ROWB, g += 4) { d[0] = g[0]; d[64] = g[1]; d[128] = g[2]; d[192] = g[3]; }
}
static void cell_pos(int ti, int flip, int *dc, int *dr) { int c = ti & 31, r = ti >> 5; if (flip) { c = 31 - c; r = 31 - r; } *dc = c; *dr = r; }
static void tile(uint8_t *b, const uint8_t *vram, int ti, int flip) {
    uint8_t a = vram[ti * 2], c = vram[ti * 2 + 1];
    int dc, dr; cell_pos(ti, flip, &dc, &dr);
    draw_cell(b, dc, dr, c | ((a & 0x10) << 4), ((a >> 7) & 1) ^ flip, ((a >> 6) & 1) ^ flip);
}

/* clear the four sprite planes of a rectangle */
static void clear_rect(uint8_t *b, const rrect *q) {
    int n = q->c1 - q->c0 + 1;
    for (int y = q->y0; y <= q->y1; y++) {
        uint8_t *d = b + y * R_ROWB + q->c0;
        if (q->c0 <= 28) { *(u32a *)(d + 32) = 0; *(u32a *)(d + 96) = 0; *(u32a *)(d + 160) = 0; *(u32a *)(d + 224) = 0; }   /* 4 columns: harmless over-clear, still inside the 32-byte plane row */
        else for (int c = 0; c < n; c++) { d[32 + c] = 0; d[96 + c] = 0; d[160 + c] = 0; d[224 + c] = 0; }
    }
}

/* draw a sprite into playfield 2; returns the rectangle touched */
static void draw_sprite(uint8_t *b, int code, int fx, int fy, int sx, int sy, rrect *out) {
    out->c0 = 0; out->c1 = -1;
    int y0 = 0, y1 = 15;
    if (sy + y0 < 16) y0 = 16 - sy;
    if (sy + y1 > 239) y1 = 239 - sy;
    if (y0 > y1) return;
    int sh = sx & 7, c0 = sx >> 3;
    int c1 = (sx + 15) >> 3; if (c1 > 31) c1 = 31;
    out->c0 = c0; out->c1 = c1; out->y0 = sy + y0 - 16; out->y1 = sy + y1 - 16;
    uint32_t clip = c0 <= 29 ? 0xFFFFFF00u : (c0 == 30 ? 0xFFFF0000u : 0xFF000000u);       /* byte columns 32.. belong to the next plane */
    const uint16_t *g = sv[fx | (fy << 1)] + code * 64 + y0 * 4;
    uint8_t *d = b + (sy + y0 - 16) * R_ROWB + c0;
    for (int r = y0; r <= y1; r++, g += 4, d += R_ROWB) {
        uint32_t w0 = g[0], w1 = g[1], w2 = g[2], w3 = g[3];
        uint32_t m = w0 | w1 | w2 | w3;
        if (!m) continue;
        uint32_t M = ((m << 16) >> sh) & clip, nM = ~M;
        u32a *p;
        p = (u32a *)(d + 32);  *p = (*p & nM) | (((w0 << 16) >> sh) & M);
        p = (u32a *)(d + 96);  *p = (*p & nM) | (((w1 << 16) >> sh) & M);
        p = (u32a *)(d + 160); *p = (*p & nM) | (((w2 << 16) >> sh) & M);
        p = (u32a *)(d + 224); *p = (*p & nM) | (((w3 << 16) >> sh) & M);
    }
}

void render_frame(rview *v, const uint8_t *ram, int flip) {
    const uint8_t *vram = ram + 0x800, *s1 = ram, *s2 = ram + 0x400;
    flip = flip ? 1 : 0;
    uint8_t *b = v->buf;
    if (v->flip != flip) {                               /* first use or flip change: the whole tile layer */
        memset(b, 0, R_BUFSZ);
        for (int ti = 0; ti < 1024; ti++) tile(b, vram, ti, flip);
        memcpy(v->tshadow, vram, 2048);
        v->nold = 0; v->flip = flip;
    } else {
        const u32a *vl = (const u32a *)vram; u32a *sh = (u32a *)v->tshadow;
        for (int i = 0; i < 512; i++) {                 /* two tiles per longword */
            if (vl[i] == sh[i]) continue;
            sh[i] = vl[i];
            tile(b, vram, i * 2, flip); tile(b, vram, i * 2 + 1, flip);
        }
        for (int i = 0; i < v->nold; i++) clear_rect(b, &v->old[i]);
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
