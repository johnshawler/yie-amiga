/* Renderer verification: replays the RAM trace of MAME (t2p.bin) through render_frame incrementally and compares the picture of every frame
 * for which MAME dumped its screen (t2p.pix) pixel by pixel.  MAME's frame_done callback sees the bitmap of the previous vblank: RAM of frame-1.
 * usage: rtest gfx.bin t2p.bin t2p.pix [palette: 0 = MAME 0.264 blue weights] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "render.h"

static uint8_t *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); } fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != (size_t)*n) exit(1); fclose(f); return b; }
static uint32_t pal[32];
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void mkpal(const char *romdir) {
    char path[300]; snprintf(path, sizeof path, "%s/407c10.1g", romdir); long n; uint8_t *p = slurp(path, &n);
    for (int i = 0; i < 32; i++) { int b = p[i];
        int r = 0x21 * (b & 1) + 0x47 * ((b >> 1) & 1) + 0x97 * ((b >> 2) & 1), g = 0x21 * ((b >> 3) & 1) + 0x47 * ((b >> 4) & 1) + 0x97 * ((b >> 5) & 1);
        int bl = 0x47 * ((b >> 6) & 1) + 0x97 * ((b >> 7) & 1);                      /* MAME 0.264 weights (the product uses 0x52/0xad) */
        pal[i] = 0xFF000000u | (r << 16) | (g << 8) | bl; }
}
/* the dual playfield: sprite layer (slots 1,3,5,7) over tile layer (slots 0,2,4,6); returns the arcade palette index (0..15 sprite, 16..31 tile) */
static uint32_t pixel(const uint8_t *buf, int x, int y) {
    int sp = 0, tl = 0;
    for (int p = 0; p < 4; p++) {
        sp |= ((buf[y * R_ROWB + (2 * p + 1) * 32 + (x >> 3)] >> (7 - (x & 7))) & 1) << p;
        tl |= ((buf[y * R_ROWB + (2 * p) * 32 + (x >> 3)] >> (7 - (x & 7))) & 1) << p;
    }
    return sp ? (uint32_t)sp : 16u + tl;
}
int main(int argc, char **argv) {
    long ng, nr, np; uint8_t *gfx = slurp(argv[1], &ng), *rec = slurp(argv[2], &nr), *pix = slurp(argv[3], &np);
    mkpal(argc > 4 ? argv[4] : "roms");
    uint8_t *work = calloc(1, RENDER_WORK + 16);
    render_init(gfx, work);
    static uint8_t b0[R_BUFSZ], b1[R_BUFSZ], fresh[R_BUFSZ];
    rview v0, v1, vf; render_view_init(&v0, b0); render_view_init(&v1, b1);
    long nframes = nr / 4122; int bad = 0, cmp = 0; long po = 0;
    int flipmode = getenv("RTEST_FLIP") != 0;
    for (long f = 0; f < nframes; f++) {
        const uint8_t *ram = rec + f * 4122 + 26;
        int flip = flipmode ? (f & 128) != 0 : (getenv("RTEST_FLIP51") ? (ram[0x1C1] & 1) : 0);
        rview *v = (f & 1) ? &v1 : &v0;                      /* double buffer: alternate views, each keeps its own incremental state */
        render_frame(v, ram, flip);
        /* incremental result must equal a from-scratch render (also checks the restore logic) */
        render_view_init(&vf, fresh); render_frame(&vf, ram, flip);
        if (memcmp(v->buf, fresh, R_BUFSZ)) { if (bad++ < 5) printf("frame %ld: incremental != fresh render\n", f); }
        while (po < np) {
            uint32_t fr = le32(pix + po), nb = le32(pix + po + 4);
            if (fr > f + 1) break;
            if (fr == f + 1 && !flipmode && f >= (getenv("RTEST_FROM") ? atol(getenv("RTEST_FROM")) : 0)) {                  /* MAME's picture of dump frame fr shows the RAM of frame fr-1 */
                const uint8_t *px = pix + po + 16; int diff = 0;
                for (int y = 0; y < R_H; y++) for (int x = 0; x < R_W; x++) if (pal[pixel(v->buf, x, y)] != le32(px + 4 * (y * R_W + x))) diff++;
                cmp++; if (diff) { bad++; printf("frame %ld (MAME dump %u): %d pixels differ\n", f, fr, diff); }
            }
            po += 16 + nb;
        }
    }
    printf("frames rendered %ld, compared with MAME %d, failures %d\n", nframes, cmp, bad);
    return bad != 0;
}
