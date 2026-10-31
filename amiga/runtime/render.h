/* Tile + sprite renderer of the Yie Ar Kung-Fu board for the Amiga's DUAL PLAYFIELD display (256 x 224, 8 interleaved bitplanes).
 *   playfield 1 (planes 1,3,5,7 = memory slots 0,2,4,6): the tile layer, pen 0..15 -> colour registers 0..15  (arcade palette entries 16..31)
 *   playfield 2 (planes 2,4,6,8 = memory slots 1,3,5,7): the sprites,    pen 1..15 -> colour registers 17..31 (arcade palette entries 1..15), pen 0 transparent
 * which is the structure of the arcade board itself (sprites over tiles, sprite pen 0 transparent): the hardware does the compositing.
 * Rules from MAME yiear.cpp (screen_update / get_bg_tile_info / draw_sprites); verified against MAME screenshots (harness/rtest). */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>
#define R_W      256
#define R_H      224
#define R_ROWB   256                      /* bytes per line: eight planes of 32 bytes, interleaved */
#define R_BUFSZ  (R_ROWB * R_H)           /* 57344 */
#define R_TILE_SLOT(p)   ((p) * 2)        /* byte offset of tile plane p (0..3) within a line group: slot*32 */
#define R_SPR_SLOT(p)    ((p) * 2 + 1)

typedef struct { int16_t c0, c1, y0, y1; } rrect;     /* byte columns c0..c1, display rows y0..y1 (inclusive) */
typedef struct rview {
    uint8_t *buf;                         /* R_BUFSZ bytes (chip RAM on the Amiga) */
    uint8_t tshadow[2048];                /* tile RAM as drawn into this buffer */
    rrect   old[26]; int nold;            /* sprite rectangles drawn into this buffer */
    int8_t  flip;                         /* flip state drawn, -1 = buffer content invalid */
} rview;

void render_init(const uint8_t *gfx, uint8_t *workmem);      /* gfx = tools/mkgfx.py output (81920 bytes); workmem: RENDER_WORK bytes */
#define RENDER_WORK (65536 * 5 + 64)
void render_view_init(rview *v, uint8_t *buf);
void render_frame(rview *v, const uint8_t *ram, int flip);   /* ram = the 4 KB at $5000 */
#endif
