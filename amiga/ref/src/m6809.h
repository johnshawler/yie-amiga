/* Cycle-counted MC6809E core for the Zoo Keeper reference emulator.
 * Semantics follow the Motorola datasheet; validated against MAME traces. */
#ifndef M6809_H
#define M6809_H
#include <stdint.h>

typedef struct m6809 m6809_t;
struct m6809 {
    uint16_t pc, x, y, u, s;
    uint8_t a, b, dp, cc;
    int firq, irq, nmi;          /* line levels */
    int nmi_pending, nmi_delay, irq_delay;   /* delay = instructions that must complete before the line is recognised */             /* NMI edge latched by the board */
    int halted;                  /* inside SYNC */
    int cwai;                    /* inside CWAI (state already pushed) */
    uint64_t cycles;             /* total cycles executed */
    uint16_t ppc;                /* PC of the instruction last started */
    uint8_t (*rd)(void *ctx, uint16_t addr);
    void (*wr)(void *ctx, uint16_t addr, uint8_t v);
    void *ctx;
    int in_fetch;                /* set while an opcode/operand byte is being fetched (access classification) */
    int illegal;                 /* count of illegal opcodes seen */
    uint8_t last_op;
};
enum { CC_C = 1, CC_V = 2, CC_Z = 4, CC_N = 8, CC_I = 0x10, CC_H = 0x20, CC_F = 0x40, CC_E = 0x80 };

void m6809_reset(m6809_t *c);
/* Execute one instruction (or take one interrupt); returns cycles consumed (>=1). */
int m6809_step(m6809_t *c);
#endif
