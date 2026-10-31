/* C side of the translated-code runtime: slow path (interpreter), I/O accessors, helpers, dispatch table. */
#include <stdint.h>
#include <string.h>
#include "board.h"
#include "yrt.h"

extern uint32_t ytab[65536];
uint32_t ystep[32768];                        /* per 6809 address: native single-instruction routine (tail mode), rt_interp1 where there is none */
extern const uint32_t ystep_tab[];
extern uint8_t yolast[65536];                 /* per leader: start offset (cycles) of the block's last instruction */
extern const uint16_t yolast_tab[];
extern uint8_t *image_base;
extern const uint32_t ylead_tab[];
extern const uint16_t ymid_tab[];              /* 8 bytes per entry: pc, (rest << 8) | rel, native offset (long) */
extern uint16_t ymid_ord[65536];
extern void rt_mid(void);
extern void rt_interp1(void);
extern void rt_enter(struct rtregs *r);
extern int m6809_step_flat(m6809_t *c);       /* the same core compiled for the flat image (M6809_FLAT) */

uint64_t rt_ev_t;                    /* absolute time (cycles) of the next scheduler event (floor) */
static uint8_t is_leader[65536];
uint32_t rt_stat_slow, rt_stat_interp1;

void rt_init(uint8_t *image) {
    image_base = image;
    for (int i = 0; i < 65536; i++) ytab[i] = (uint32_t)(uintptr_t)rt_interp1;       /* unknown target -> interpreter */
    for (int i = 0; i < 32768; i++) ystep[i] = (uint32_t)(uintptr_t)rt_interp1;
    for (const uint32_t *p = ystep_tab; p[0] || p[1]; p += 2) ystep[(uint16_t)p[0] - 32768] = p[1];          /* code lives at $8000-$FFFF: index 0..32767 */
    for (const uint32_t *p = ylead_tab; p[0] || p[1]; p += 2) {
        is_leader[p[0]] = 1;
        ytab[32768 + (int16_t)p[0]] = p[1];
    }
    { uint32_t n = 1; for (const uint16_t *p = ymid_tab; p[0] || p[1]; p += 4, n++) { ytab[32768 + (int16_t)p[0]] = (uint32_t)(uintptr_t)rt_mid; ymid_ord[(uint16_t)(p[0] + 32768)] = (uint16_t)n; } }   /* entries inside blocks (return from an interrupt) */
    for (const uint16_t *q = yolast_tab; q[0] || q[1]; q += 2) yolast[(uint16_t)(q[0] + 32768) ^ 0x0000] = (uint8_t)q[1];
}

static inline int pending_interrupt(void) {
    return cpu.nmi_pending || (cpu.irq && !(cpu.cc & CC_I)) || (cpu.firq && !(cpu.cc & CC_F));
}

void rt_slow_c(struct rtregs *r) {
    rt_stat_slow++;
    cpu.a = r->d[0]; cpu.b = r->d[1]; cpu.x = r->d[2]; cpu.y = r->d[3]; cpu.u = r->d[4];
    cpu.s = r->s; cpu.dp = r->dp; cpu.cc = (r->ccr & 0x0F) | (r->cch & 0xF0); cpu.pc = r->pc;
    cpu.cycles = rt_ev_t - (int32_t)r->a3;     /* a3 is negative when the last block ran past the event */
    int force = r->force;
    if (force) rt_stat_interp1++;
    uint64_t ev = rt_ev_t;
#ifdef SLOWLOG
    { extern void slowlog(uint64_t c, uint32_t pc, int force, int32_t a3); slowlog(cpu.cycles, cpu.pc, force, (int32_t)r->a3); }
#endif
    for (;;) {
        if (cpu.cycles >= ev) { board_fire(); ev = board_next_event(); }
        if (!force && is_leader[cpu.pc] && !pending_interrupt() && ev - cpu.cycles > yolast[(uint16_t)(cpu.pc + 32768)]) break;
        force = 0;
        m6809_step_flat(&cpu);
    }
    rt_ev_t = ev;
    r->d[0] = cpu.a; r->d[1] = cpu.b; r->d[2] = cpu.x; r->d[3] = cpu.y; r->d[4] = cpu.u;
    r->s = cpu.s; r->dp = cpu.dp; r->ccr = cpu.cc & 0x0F; r->cch = cpu.cc & 0xF0; r->pc = cpu.pc;
    uint32_t rem = (uint32_t)(rt_ev_t - cpu.cycles);
    r->a3 = rem;
    uint32_t b = rem >= 128 ? (rem >> 6) - 1 : 1; r->force = b > 0xFFFF ? 0xFFFF : b;
}

uint32_t rt_io_rd_c(uint32_t addr, uint32_t a3, uint32_t delay) {
    return board_io_rd((uint16_t)addr, rt_ev_t - (int32_t)a3 - delay);
}
void rt_io_wr_c(uint32_t addr, uint32_t data, uint32_t a3, uint32_t delay) {
    board_io_wr((uint16_t)addr, (uint8_t)data, rt_ev_t - (int32_t)a3 - delay);
}

/* ---- helpers: the instruction is executed by the reference core on a scratch CPU (identical semantics by construction) ---- */
static m6809_t hcpu; static uint8_t hmem[4];
static uint8_t h_rd(void *c, uint16_t a) { (void)c; return a < 4 ? hmem[a] : 0; }
static void h_wr(void *c, uint16_t a, uint8_t v) { (void)c; (void)a; (void)v; }
void rt_helper_c(uint8_t *s) {                  /* s: a b x(2) cc op m */
    hcpu.rd = h_rd; hcpu.wr = h_wr; hcpu.irq = hcpu.firq = hcpu.nmi = hcpu.nmi_pending = hcpu.halted = hcpu.cwai = 0;
    hcpu.a = s[0]; hcpu.b = s[1]; hcpu.x = (s[2] << 8) | s[3]; hcpu.cc = s[4] | CC_I | CC_F; hcpu.s = 0x1000;
    hmem[0] = s[5]; hmem[1] = s[6]; hcpu.pc = 0;
    m6809_step(&hcpu);
    s[0] = hcpu.a; s[1] = hcpu.b; s[2] = hcpu.x >> 8; s[3] = hcpu.x; s[4] = (hcpu.cc & ~(CC_I | CC_F)) | (s[4] & (CC_I | CC_F));
}

void rt_chk_c(uint32_t ea, uint32_t pc) {
    if ((ea >= 0x5000 && ea < 0x6000) || (ea >= 0x8000 && ea < 0x10000)) return;
    rt_fatal("dynamic access outside RAM/ROM", ea, pc);
}
void rt_trap_c(void) { rt_fatal("fell into data", 0, 0); }

void rt_run(void) {
    struct rtregs r;
    memset(&r, 0, sizeof r);
    board_reset();
    rt_ev_t = board_next_event();
    r.d[0] = cpu.a; r.d[1] = cpu.b; r.d[2] = cpu.x; r.d[3] = cpu.y; r.d[4] = cpu.u;
    r.s = cpu.s; r.dp = cpu.dp; r.ccr = cpu.cc & 0x0F; r.cch = cpu.cc & 0xF0; r.pc = cpu.pc;
    r.a3 = (uint32_t)(rt_ev_t - cpu.cycles);
    r.force = (r.a3 >> 6) - 1;
    rt_enter(&r);
}
