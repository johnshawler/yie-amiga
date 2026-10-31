/* Tile + sprite renderer of the Yie Ar Kung-Fu board for a 5-bitplane interleaved Amiga screen (256 x 224).
 * Colour index = pen | 16 for background tiles (palette entries 16..31), pen (1..15) for sprites (entries 0..15, pen 0 transparent).
 * Rules from MAME yiear.cpp (screen_update / get_bg_tile_info / draw_sprites); verified against MAME screenshots (tools/gfxdec.py, harness/rtest). */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>
#define R_W      256
#define R_H      224
#define R_PLANES 5
#define R_ROWB   (R_PLANES * 32)          /* bytes per line: five planes of 32 bytes, interleaved */
#define R_BUFSZ  (R_ROWB * R_H)           /* 35840 */

typedef struct { int16_t c0, c1, y0, y1; } rrect;     /* byte columns c0..c1, display rows y0..y1 (inclusive) */
typedef struct rview {
    uint8_t *buf;                         /* R_BUFSZ bytes (chip RAM on the Amiga) */
    uint8_t tshadow[2048];                /* tile RAM as drawn into this buffer */
    rrect   old[26]; int nold;            /* sprite rectangles drawn into this buffer */
    int8_t  flip;                         /* flip state drawn, -1 = buffer content invalid */
} rview;

void render_init(const uint8_t *gfx, uint8_t *workmem);      /* gfx = tools/mkgfx.py output (81920 bytes); workmem: RENDER_WORK bytes in fast RAM */
#define RENDER_WORK (65536 * 5 + R_BUFSZ + 2048 + 64)
void render_view_init(rview *v, uint8_t *buf);
void render_frame(rview *v, const uint8_t *ram, int flip);   /* ram = the 4 KB at $5000 */
#endif
