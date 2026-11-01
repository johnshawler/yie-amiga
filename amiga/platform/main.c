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
#include "gamesound.h"

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
static uint32_t idle_hist[8];
static uint32_t skipped, frames_late, max_lag, spins, spin_calib, gap2, gap3, gap4;
static int shown_view;

static void fatal(uint16_t colour) { for (;;) { *(volatile uint16_t *)0xDFF180 = colour; } }
void rt_fatal(const char *msg, uint32_t a, uint32_t b) { (void)msg; (void)a; (void)b; fatal(0x0F00); }

#ifdef PROFILE
/* PC sampling (CIA-A timer A at ~573 Hz, not commensurate with the game tick): 256-byte buckets of the image */
static uint16_t prof_hist[4096]; static uint32_t prof_total; static uint32_t prof_cat[12];
extern char P_board_init[] __asm__("board_init"), P_rt_init[] __asm__("rt_init"), P_render_init[] __asm__("render_init"), P_m6809_reset[] __asm__("m6809_reset"), P_m6809_reset_flat[] __asm__("m6809_reset_flat"), P_y_code_start[] __asm__("y_code_start"), P_y_code_end[] __asm__("y_code_end"), P_rt_refill[] __asm__("rt_refill"), P_plat_hw_reset[] __asm__("plat_hw_reset"), P_memcpy[] __asm__("memcpy"), P_sns_init[] __asm__("sns_init"), P_lz4_decode[] __asm__("lz4_decode"), P_gs_init[] __asm__("gs_init");
extern char _payload_base[];
static unsigned prof_category(uint32_t pc) {
    if (pc < (uint32_t)P_board_init) return 0;                 /* main.c: frame hook, input, HUD, the pacing spin */
    if (pc < (uint32_t)P_rt_init - 0x20) return 1;             /* board model */
    if (pc < (uint32_t)P_render_init - 0x90) return 2;         /* yrt.c: I/O stubs, helpers, slow path glue */
    if (pc < (uint32_t)P_m6809_reset - 0xABA) return 3;       /* render.c */
    if (pc < (uint32_t)P_y_code_start) return 4;               /* 6809 interpreter (slow path) */
    if (pc < (uint32_t)P_y_code_end) return 5;                 /* translated game code */
    if (pc < (uint32_t)P_plat_hw_reset) return 6;              /* ysys.s: refill / dispatch / mid entries */
    if (pc < (uint32_t)P_memcpy) return 7;                     /* platform.c */
    if (pc < (uint32_t)P_sns_init) return 8;                   /* rt.c, rtasm.s */
    if (pc < (uint32_t)P_lz4_decode) return 9;                 /* SN76489A synthesis */
    return 10;                                               /* lz4, game sound glue, other */
}
void plat_prof_pc(uint32_t pc) { prof_total++; prof_cat[prof_category(pc)]++; uint32_t b = (pc - (uint32_t)_payload_base) >> 8; if (b < 4096 && prof_hist[b] != 65535) prof_hist[b]++; }
#endif
#ifdef HUD
#ifdef PROFILE
#define HUD_ROWS 25
#else
#if defined(SOAK) && !defined(PROFILE) && !defined(DIAG)
#define HUD_ROWS 7
#else
#define HUD_ROWS 17
#endif
#endif
/* debug heads-up display: 3x5 hex digits in colour 15 at the top-left corner */
static const uint16_t font35[16] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x7BED, 0x6BAE, 0x7127, 0x6B6E, 0x79E7, 0x79E4 };
static void hud_digit(uint8_t *b, int x, int y, int v) {
    uint16_t g = font35[v & 15];
    for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) {
        int on = (g >> (14 - (r * 3 + c))) & 1;
        uint8_t m = (uint8_t)(0x80 >> ((x + c) & 7)); uint8_t *d = b + (y + r) * R_ROWB + ((x + c) >> 3);
        /* sprite-layer pen 15 (white) for a lit pixel, pen 1 (dark blue) as the background of the box */
        d[32] |= m; if (on) { d[96] |= m; d[160] |= m; d[224] |= m; } else { d[96] &= (uint8_t)~m; d[160] &= (uint8_t)~m; d[224] &= (uint8_t)~m; }
    }
}
static void hud(uint8_t *b, int row, uint32_t v) {
    if (row == 0) for (int y = 0; y < HUD_ROWS * 6 + 2; y++) for (int p = 0; p < 4; p++) for (int c = 0; c < 5; c++) b[y * R_ROWB + (2 * p + 1) * 32 + c] = 0;
    for (int i = 0; i < 8; i++) hud_digit(b, 2 + i * 4, 1 + row * 6, (v >> (28 - 4 * i)) & 15); }
#endif

/* ---- input ---- */
static uint8_t coin_pulse, start_pulse, coin_delay, fire_prev;
#ifdef SOAK
#include "soak_script.h"
static uint32_t soak_crc[2];
static uint32_t crc32_ram(const uint8_t *p, unsigned n) { uint32_t c = 0xFFFFFFFFu; while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); } return ~c; }
#endif
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
#ifdef SOAK
    {   /* scripted play (the same events and timing as yref -i): applied for the frame after the one that has just ended */
        static unsigned si; static uint8_t ssys = 0xFF, sp1 = 0xFF; extern uint32_t soak_fr;
        while (soak_script[si].frame <= soak_fr + 1) { ssys = soak_script[si].sys; sp1 = soak_script[si].p1; si++; }
        in_sys = ssys; in_p1 = sp1;
    }
#endif
}

/* ---- frame hook: called by the board model at every emulated vertical blank (arcade 60.606 Hz) ---- */
#ifdef SOAK
uint32_t soak_fr;
#endif
static void frame_hook(uint64_t now, uint32_t fr) {
    (void)now;
#ifdef SOAK
    soak_fr = fr;
    if (fr == SOAK_F1) soak_crc[0] = crc32_ram(image + 0x5000, 4096);
    if (fr == SOAK_F2) soak_crc[1] = crc32_ram(image + 0x5000, 4096);
#endif
    gs_frame_prepare(fr);                             /* sound block of the frame that has just ended (queued below, at the frame's real-time tick) */
    { static uint32_t last_t; uint32_t g = plat_tick_count - last_t; last_t = plat_tick_count; if (g == 2) gap2++; else if (g == 3) gap3++; else if (g >= 4 && g < 20) gap4++; }
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
    static uint32_t last_vbl;
    if (plat_vbl_count == last_vbl) { }                 /* no vertical blank since the last picture: this one would never be shown */
    else if (lag > 0 && skip_run < DISPLAY_SKIP_MAX) { skip_run++; skipped++; }
    else {
        last_vbl = plat_vbl_count;
        skip_run = 0;
        int back = shown_view ^ 1;
        render_frame(&views[back], image + 0x5000, board_flip());
#ifdef HUD
        if (plat_key_down(KEY_F1)) {
        hud(views[back].buf, 0, fr); hud(views[back].buf, 1, plat_tick_count - tick_origin); hud(views[back].buf, 2, spins); hud(views[back].buf, 3, spin_calib);
        { extern uint32_t rt_stat_slow; hud(views[back].buf, 4, rt_stat_slow); }
#if defined(SOAK) && !defined(PROFILE) && !defined(DIAG)
        hud(views[back].buf, 5, soak_crc[0]); hud(views[back].buf, 6, soak_crc[1]);
#endif
#if !defined(PROFILE) && (!defined(SOAK) || defined(DIAG))
        hud(views[back].buf, 5, plat_snd_underruns); hud(views[back].buf, 6, plat_snd_blocks); hud(views[back].buf, 7, (plat_snd_dropped << 24) | ((plat_snd_skipped & 0xFFF) << 12) | (gs_vlm_started & 0xFFF)); hud(views[back].buf, 8, (plat_snd_lost << 16) | plat_snd_lagmax); hud(views[back].buf, 9, (max_lag << 24) | (frames_late & 0xFFFFFF)); hud(views[back].buf, 10, (gap2 << 20) | (gap3 << 10) | gap4); hud(views[back].buf, 11, (plat_snd_lagh[0] << 16) | (plat_snd_lagh[1] & 0xFFFF)); hud(views[back].buf, 12, (plat_snd_lagh[2] << 16) | (plat_snd_lagh[3] & 0xFFFF)); hud(views[back].buf, 13, (plat_snd_ci[0] << 24) | ((plat_snd_ci[1] & 0xFFF) << 12) | (plat_snd_ci[2] & 0xFFF)); hud(views[back].buf, 15, (idle_hist[0] << 16) | (idle_hist[1] & 0xFFFF)); hud(views[back].buf, 16, (idle_hist[2] << 16) | (idle_hist[3] & 0xFFFF)); hud(views[back].buf, 14, (plat_snd_pi[0] << 24) | ((plat_snd_pi[1] & 0xFFF) << 12) | (plat_snd_pi[2] & 0xFFF)); hud(views[back].buf, 12, plat_input()); hud(views[back].buf, 13, ((uint32_t)image[0x51C3] << 24) | ((uint32_t)image[0x51C4] << 16) | ((uint32_t)image[0x51C5] << 8) | image[0x51C6]);
#endif
#ifdef PROFILE
        { for (int r = 0; r < 11; r++) hud(views[back].buf, 5 + r, prof_cat[r]); hud(views[back].buf, 16, prof_total);
          { uint32_t b0 = ((uint32_t)P_sns_init - (uint32_t)_payload_base) >> 8; for (int r = 0; r < 8; r++) hud(views[back].buf, 17 + r, (prof_hist[b0 + 2 * r] << 16) | prof_hist[b0 + 2 * r + 1]); hud(views[back].buf, 24, b0); } }
#endif
        { rrect q = { 0, 4, 0, HUD_ROWS * 6 + 1 }; views[back].old[views[back].nold++] = q; }     /* the HUD area is restored from the tile layer next time */
        }
#endif
        plat_show(back); shown_view = back;
    }
    if (lag > 0) frames_late++;
    uint32_t sp0 = spins;
    while ((int32_t)(plat_tick_count - due) < 0) { spins++; }  /* wait for the real-time tick */
    { uint32_t idle = spins - sp0, q = spin_calib ? idle * 8 / spin_calib : 0; idle_hist[q > 7 ? 7 : q]++; }
    gs_frame_commit();
}

#ifdef AUDIOTEST
/* Audio self-test (no game): the script of tools/mkaudiotest.py drives the sound path exactly like the game does (chip writes and speech starts with their cycle
 * stamps, one stream block per frame at the real-time tick); the Paula output is recorded and compared with sound/sndpc.c's expected stream. */
#include "audiotest_script.h"
void main(void) {
    board_init(image, TIMING_PINNED);
    gs_init();
    plat_wait_vbl();
    { uint32_t t0 = plat_tick_count; while (plat_tick_count == t0) { } }
    uint32_t base = plat_tick_count; unsigned si = 0;
    for (uint32_t fr = 0; ; fr++) {
        while (at_script[si].cycle != 0xFFFFFFFFu && at_script[si].cycle < (fr + 1) * 25344u) {
            if (at_script[si].kind == 0) hooks.sn_write(at_script[si].cycle, at_script[si].data);
            else { hooks.vlm_reset(at_script[si].cycle - 20); hooks.vlm_start(at_script[si].cycle, at_script[si].data); }
            si++;
        }
        gs_frame_prepare(fr);
        while ((int32_t)(plat_tick_count - (base + fr + 1)) < 0) { }
        gs_frame_commit();
        if (fr > AT_FRAMES + 200) { for (;;) { } }
    }
}
#else
void main(void) {
    memcpy(image + 0x8000, rom_blob, 0x8000);
    render_init(gfx_blob, render_work);
    render_view_init(&views[0], (uint8_t *)plat_buf(0));
    render_view_init(&views[1], (uint8_t *)plat_buf(1));
    {   /* dual playfield colour registers: 0..15 = tile pens (arcade palette entries 16..31), 16..31 = sprite pens (entries 0..15; pen 0 is transparent) */
        static uint32_t pal[32];
        for (int i = 0; i < 16; i++) { pal[i] = yie_palette[16 + i]; pal[16 + i] = yie_palette[i]; }
        plat_set_palette32(pal);
    }
    board_init(image, TIMING_PINNED);
    hooks.frame = frame_hook;
    gs_init();
    rt_init(image);
    plat_wait_vbl();
    { uint32_t t0 = plat_tick_count; while (plat_tick_count == t0) { } t0 = plat_tick_count; spins = 0; while (plat_tick_count - t0 < 8) spins++; spin_calib = spins / 8; spins = 0; }
    tick_base = tick_origin = plat_tick_count;
    rt_run();
}
#endif
