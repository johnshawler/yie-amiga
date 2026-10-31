/* Yie Ar Kung-Fu for the Amiga 1200: glue between the translated game (runtime/), the board model and the platform layer.
 * Controls: joystick in port 2 (stick = 8-way, fire = kick + START when no game is running, second button = punch);
 * keyboard: 5 = coin, 1 / 2 = start 1P / 2P, P = pause, Esc = restart. */
#include <stdint.h>
#include <string.h>
#include "platform.h"
#include "board.h"
#include "yrt.h"
#include "render.h"
#include "palette.h"

extern const uint8_t rom_blob[0x8000];            /* the two program ROMs, $8000-$FFFF */
extern const uint8_t gfx_blob[81920];             /* tools/mkgfx.py output */

static uint8_t image[65536 + 64] __attribute__((aligned(4)));
static uint8_t render_work[RENDER_WORK] __attribute__((aligned(4)));
static rview views[2];

#ifndef BACKLOG_MAX
#define BACKLOG_MAX 60          /* frames the game may run behind real time before it resynchronises */
#endif
#ifndef DISPLAY_SKIP_MAX
#define DISPLAY_SKIP_MAX 3      /* consecutive pictures that may be skipped while behind real time */
#endif
#ifndef AUTO_COIN
#define AUTO_COIN 1             /* fire with no credit inserts a coin first (joystick-only play) */
#endif

static uint32_t tick_base, tick_origin;                          /* plat_tick_count corresponding to emulated frame 0 */
static uint32_t skipped, frames_late, max_lag;
static int shown_view;

static void fatal(uint16_t colour) { for (;;) { *(volatile uint16_t *)0xDFF180 = colour; } }
void rt_fatal(const char *msg, uint32_t a, uint32_t b) { (void)msg; (void)a; (void)b; fatal(0x0F00); }

#ifdef PROFILE
/* PC sampling (CIA-A timer A at ~573 Hz, not commensurate with the game tick): 256-byte buckets of the image */
static uint16_t prof_hist[4096]; static uint32_t prof_total;
void plat_prof_pc(uint32_t pc) { prof_total++; uint32_t b = (pc - 0x800) >> 8; if (b < 4096 && prof_hist[b] != 65535) prof_hist[b]++; }
#endif
#ifdef HUD
#ifdef PROFILE
#define HUD_ROWS 26
#else
#define HUD_ROWS 5
#endif
/* debug heads-up display: 3x5 hex digits in colour 15 at the top-left corner */
static const uint16_t font35[16] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x7BED, 0x6BAE, 0x7127, 0x6B6E, 0x79E7, 0x79E4 };
static void hud_digit(uint8_t *b, int x, int y, int v) {
    uint16_t g = font35[v & 15];
    for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) {
        int on = (g >> (14 - (r * 3 + c))) & 1;
        uint8_t m = (uint8_t)(0x80 >> ((x + c) & 7)); uint8_t *d = b + (y + r) * R_ROWB + ((x + c) >> 3);
        for (int p = 0; p < 4; p++) { if (on) d[p * 32] |= m; else d[p * 32] &= (uint8_t)~m; }
        d[128] &= (uint8_t)~m;
    }
}
static void hud(uint8_t *b, int row, uint32_t v) {
    if (row == 0) for (int y = 0; y < HUD_ROWS * 6 + 2; y++) for (int p = 0; p < 5; p++) for (int c = 0; c < 5; c++) b[y * R_ROWB + p * 32 + c] = 0;
    for (int i = 0; i < 8; i++) hud_digit(b, 2 + i * 4, 1 + row * 6, (v >> (28 - 4 * i)) & 15); }
#endif

/* ---- input ---- */
static uint8_t coin_pulse, start_pulse, coin_delay, fire_prev;
static void read_inputs(void) {
    uint16_t in = plat_input();
    uint8_t p1 = 0xFF, sys = 0xFF;
    if (in & PIN_LEFT)  p1 &= ~0x01;
    if (in & PIN_RIGHT) p1 &= ~0x02;
    if (in & PIN_UP)    p1 &= ~0x04;
    if (in & PIN_DOWN)  p1 &= ~0x08;
    if (in & PIN_FIRE)  p1 &= ~0x10;                 /* button 1: kick */
    if (in & PIN_BTN2)  p1 &= ~0x20;                 /* button 2: punch */
    if (in & PIN_COIN)   sys &= ~0x01;
    if (in & PIN_START1) sys &= ~0x08;
    if (in & PIN_START2) sys &= ~0x10;
    int playing = (image[0x51C3] == 2);
    int fire = (in & PIN_FIRE) != 0;
    if (fire && !fire_prev && !playing) {            /* fire = start switch while no game is running */
#if AUTO_COIN
        if (image[0x51C3] == 0) { coin_pulse = 4; coin_delay = 8; }
        else
#endif
        start_pulse = 4;
    }
    fire_prev = (uint8_t)fire;
    if (coin_pulse) { sys &= ~0x01; coin_pulse--; }
    if (coin_delay) { if (--coin_delay == 0) start_pulse = 4; }
    if (start_pulse) { sys &= ~0x08; start_pulse--; }
    in_sys = sys; in_p1 = p1; in_p2 = 0xFF;
}

/* ---- frame hook: called by the board model at every emulated vertical blank (arcade 60.606 Hz) ---- */
static void frame_hook(uint64_t now, uint32_t fr) {
    (void)now;
    uint32_t due = tick_base + fr + 1;                /* real-time tick at which this frame should complete */
    uint32_t t = plat_tick_count;
    int32_t lag = (int32_t)(t - due);                 /* > 0: running behind */
    if (lag > BACKLOG_MAX) { tick_base += (uint32_t)(lag - 2); due = tick_base + fr + 1; lag = 2; }   /* too far behind: resynchronise */
    if (lag > (int32_t)max_lag) max_lag = (uint32_t)lag;
    static uint8_t pause_prev; static int paused;
    read_inputs();
    int pk = plat_key_down(KEY_P);
    if (pk && !pause_prev) paused = !paused;
    pause_prev = (uint8_t)pk;
    if (plat_key_down(KEY_ESC)) plat_restart();
    while (paused) {                                   /* frozen: keep the clock running so that resuming does not "catch up" */
        if (plat_key_down(KEY_P)) { } 
        uint32_t t0 = plat_tick_count; while (plat_tick_count == t0) { }
        tick_base++;
        int pk2 = plat_key_down(KEY_P);
        if (pk2 && !pause_prev) paused = 0;
        pause_prev = (uint8_t)pk2;
        if (plat_key_down(KEY_ESC)) plat_restart();
    }
    /* picture: skipped while behind real time (at most DISPLAY_SKIP_MAX in a row) */
    static uint32_t skip_run;
    if (lag > 0 && skip_run < DISPLAY_SKIP_MAX) { skip_run++; skipped++; }
    else {
        skip_run = 0;
        int back = shown_view ^ 1;
        render_frame(&views[back], image + 0x5000, board_flip());
#ifdef HUD
        if (plat_key_down(KEY_F1)) {
        hud(views[back].buf, 0, fr); hud(views[back].buf, 1, plat_tick_count - tick_origin); hud(views[back].buf, 2, skipped); hud(views[back].buf, 3, max_lag);
        { extern uint32_t rt_stat_slow; hud(views[back].buf, 4, rt_stat_slow); }
#ifdef PROFILE
        { /* the 20 busiest 256-byte buckets: (bucket << 16) | samples; row 25 = total samples */
            uint16_t top[20], tc[20]; memset(tc, 0, sizeof tc); memset(top, 0, sizeof top);
            for (uint32_t k = 0; k < 4096; k++) { uint16_t c = prof_hist[k]; if (c <= tc[19]) continue; int j = 19; while (j > 0 && tc[j - 1] < c) { tc[j] = tc[j - 1]; top[j] = top[j - 1]; j--; } tc[j] = c; top[j] = (uint16_t)k; }
            for (int r = 0; r < 20; r++) hud(views[back].buf, 5 + r, ((uint32_t)top[r] << 16) | tc[r]);
            hud(views[back].buf, 25, prof_total);
        }
#endif
        { rrect q = { 0, 4, 0, HUD_ROWS * 6 + 1 }; views[back].old[views[back].nold++] = q; }     /* the HUD area is restored from the tile layer next time */
        }
#endif
        plat_show(back); shown_view = back;
    }
    if (lag > 0) frames_late++;
    while ((int32_t)(plat_tick_count - due) < 0) { }  /* wait for the real-time tick */
}

void main(void) {
    memcpy(image + 0x8000, rom_blob, 0x8000);
    render_init(gfx_blob, render_work);
    render_view_init(&views[0], (uint8_t *)plat_buf(0));
    render_view_init(&views[1], (uint8_t *)plat_buf(1));
    plat_set_palette32(yie_palette);
    board_init(image, TIMING_PINNED);
    hooks.frame = frame_hook;
    rt_init(image);
    plat_wait_vbl();
    tick_base = tick_origin = plat_tick_count;
    rt_run();
}
