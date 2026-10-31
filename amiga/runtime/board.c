#include "board.h"
#include "vlm_dur.h"
#ifdef BOARD_HOST
#include <stdio.h>
#endif

uint8_t *mem;
m6809_t cpu;
board_hooks hooks;
uint8_t in_sys = 0xff, in_p1 = 0xff, in_p2 = 0xff;
uint8_t dsw1 = 0xff, dsw2 = 0x5b, dsw3 = 0xff;

static int timing;
static int nmi_enable, irq_enable;
static uint8_t sn_latch;
static uint8_t vlm_ctl, vlm_data;
static uint64_t vlm_busy_until;                 /* cycles; 0 = idle */
static uint32_t frames;

/* ---- event times: whole cycles + 32-bit fraction (the 0.264 frame period is not an integer number of cycles) ---- */
typedef struct { uint64_t w; uint32_t f; } tm_t;
static tm_t t_nmi, t_irq;                       /* next NMI tick, next vblank (frame end) */
static tm_t P_frame, P_line;                    /* frame period, line period (pinned) */
static tm_t nmi_frame0;                         /* pinned: start of the frame the next NMI belongs to */
static int nmi_k;
static tm_t tm_add(tm_t a, tm_t b) { tm_t r; uint64_t f = (uint64_t)a.f + b.f; r.w = a.w + b.w + (f >> 32); r.f = (uint32_t)f; return r; }
static tm_t TM(uint64_t w, uint32_t f) { tm_t r; r.w = w; r.f = f; return r; }
static tm_t tm_mul(tm_t a, uint32_t n) { tm_t r = TM(0, 0); while (n--) r = tm_add(r, a); return r; }
static int tm_le(tm_t a, tm_t b) { return a.w < b.w || (a.w == b.w && a.f <= b.f); }

void board_init(uint8_t *image, int tm) {
    mem = image; timing = tm;
    cpu.rd = board_rd; cpu.wr = board_wr; cpu.ctx = 0;
}

void board_reset(void) {
    m6809_reset(&cpu);
    cpu.cycles = 4;                              /* MAME: the reset sequence takes 4 cycles before the first opcode fetch */
    nmi_enable = irq_enable = 0; sn_latch = 0; vlm_ctl = vlm_data = 0; vlm_busy_until = 0; frames = 0;
    if (timing == TIMING_MAME0264) {
        /* the installed MAME 0.264: 60.58 Hz frame (1536000 / 60.58 cycles), NMI periodic at 480 Hz, vblank at the frame end */
        /* 1536000 / 60.58 = 25354.9062...: whole part 25354, fraction (1536000*10000 mod 605800) / 605800 */
        { uint64_t num = 1536000ull * 100ull, den = 6058ull; uint64_t w = num / den, r = num % den; P_frame = TM(w, (uint32_t)((r << 32) / den)); }
        /* +1/1024 cycle: MAME's attosecond arithmetic puts the vblank a hair after an NMI tick when the two coincide exactly (frame 9087 of the soak) */
        P_frame.f += 0; t_irq = tm_add(P_frame, TM(0, 1u << 22)); t_nmi = TM(3200, 0);
    } else {
        /* pinned driver: 18.432/3 MHz pixel clock, 384 x 264 -> 96 cycles per line, 25344 per frame; vblank at line 240; NMI = scanline timer(16, 32) */
        P_frame = TM(25344, 0); P_line = TM(96, 0);
        t_irq = TM(240 * 96, 0); nmi_frame0 = TM(0, 0); nmi_k = 0; t_nmi = TM(16 * 96, 0);
    }
}

uint32_t board_frame_count(void) { return frames; }

uint64_t board_next_event(void) {
    return tm_le(t_irq, t_nmi) ? t_irq.w : t_nmi.w;
}

/* floor() of the event time is what the CPU model compares against (MAME runs the CPU for floor(T - now) cycles) */
void board_fire(void) {
    for (;;) {
        /* vblank wins a tie, as in the reference loop (NMIs strictly before the frame end go first) */
        if (tm_le(t_irq, t_nmi)) {
            if (t_irq.w > cpu.cycles) return;
            frames++;
            if (hooks.frame) hooks.frame(cpu.cycles, frames - 1);
            if (irq_enable) { if (!cpu.irq) cpu.irq_delay = 0; cpu.irq = 1; }
            t_irq = tm_add(t_irq, P_frame);
        } else {
            if (t_nmi.w > cpu.cycles) return;
            if (nmi_enable) { if (!cpu.nmi) cpu.nmi_pending = 1; cpu.nmi = 1; }
            if (timing == TIMING_MAME0264) t_nmi = tm_add(t_nmi, TM(3200, 0));
            else {
                nmi_k++;
                if (16 + 32 * nmi_k >= 264) { nmi_k = 0; nmi_frame0 = tm_add(nmi_frame0, P_frame); }
                t_nmi = tm_add(nmi_frame0, TM((uint64_t)(16 + 32 * nmi_k) * 96, 0));
            }
        }
    }
}

/* ---- I/O ---- */
uint8_t board_io_rd(uint16_t a, uint64_t now) {
    uint8_t v = 0;
    switch (a) {
    case 0x0000: v = (vlm_busy_until && now < vlm_busy_until) ? 1 : 0; break;
    case 0x4c00: v = dsw2; break;
    case 0x4d00: v = dsw3; break;
    case 0x4e00: v = in_sys; break;
    case 0x4e01: v = in_p1; break;
    case 0x4e02: v = in_p2; break;
    case 0x4e03: v = dsw1; break;
    default: return 0;
    }
    if (hooks.io_log) hooks.io_log('R', now, a, v);
    return v;
}

void board_io_wr(uint16_t a, uint8_t v, uint64_t now) {
    if (hooks.io_log) hooks.io_log('W', now, a, v);
    switch (a) {
    case 0x4000:
        if (nmi_enable && !(v & 2)) cpu.nmi = 0;
        nmi_enable = (v >> 1) & 1;
        if (irq_enable && !(v & 4)) cpu.irq = 0;
        irq_enable = (v >> 2) & 1;
        break;
    case 0x4800: sn_latch = v; break;
    case 0x4900: if (hooks.sn_write) hooks.sn_write(now, sn_latch); break;
    case 0x4a00:
        /* ST rising edge (bit 1) with RST (bit 2) low starts the phrase latched in $4B00; RST clears BSY */
        if (v & 4) { vlm_busy_until = 0; if (hooks.vlm_reset) hooks.vlm_reset(now); }
        else if ((v & 2) && !(vlm_ctl & 2)) {
            int ph = (vlm_data >> 1) & 127;
            vlm_busy_until = now + 5 + vlm_dur[ph];
            if (hooks.vlm_start) hooks.vlm_start(now, ph);
        }
        vlm_ctl = v;
        break;
    case 0x4b00: vlm_data = v; break;
    default: break;                              /* $4F00 watchdog and unmapped: no effect */
    }
}

/* ---- interpreter bus ---- */
uint8_t board_rd(void *ctx, uint16_t a) {
    (void)ctx;
    if (a >= 0x8000 || (a >= 0x5000 && a < 0x6000)) return mem[a];
    if (cpu.in_fetch) return 0;
    return board_io_rd(a, cpu.cycles);
}
void board_wr(void *ctx, uint16_t a, uint8_t v) {
    (void)ctx;
    if (a >= 0x5000 && a < 0x6000) { mem[a] = v; return; }
    if (a >= 0x4000 && a < 0x5000) board_io_wr(a, v, cpu.cycles);
}

void board_run(uint64_t until) {
    while (cpu.cycles < until) {
        uint64_t ev = board_next_event();
        uint64_t lim = ev < until ? ev : until;
        while (cpu.cycles < lim) m6809_step(&cpu);
        if (cpu.cycles >= ev) board_fire();
    }
}
