/* MC6809E core -- see m6809.h */
#include "m6809.h"
#include <string.h>

#define RD(a) c->rd(c->ctx, (uint16_t)(a))
#define WR(a, v) c->wr(c->ctx, (uint16_t)(a), (uint8_t)(v))
#define D(c) ((uint16_t)(((c)->a << 8) | (c)->b))
#define SETD(c, v) do { uint16_t _v = (v); (c)->a = (uint8_t)(_v >> 8); (c)->b = (uint8_t)_v; } while (0)

static inline uint8_t fetch(m6809_t *c) { c->in_fetch = 1; uint8_t v = RD(c->pc++); c->in_fetch = 0; return v; }
static inline uint16_t fetch16(m6809_t *c) { uint16_t h = fetch(c); return (h << 8) | fetch(c); }
static inline uint16_t rd16(m6809_t *c, uint16_t a) { uint16_t h = RD(a); return (h << 8) | RD(a + 1); }
static inline void wr16(m6809_t *c, uint16_t a, uint16_t v) { WR(a, v >> 8); WR(a + 1, v); }

static inline void setnz8(m6809_t *c, uint8_t r) {
    c->cc = (c->cc & ~(CC_N | CC_Z)) | (r & 0x80 ? CC_N : 0) | (r == 0 ? CC_Z : 0);
}
static inline void setnz16(m6809_t *c, uint16_t r) {
    c->cc = (c->cc & ~(CC_N | CC_Z)) | (r & 0x8000 ? CC_N : 0) | (r == 0 ? CC_Z : 0);
}
static inline void setv(m6809_t *c, int v) { c->cc = (c->cc & ~CC_V) | (v ? CC_V : 0); }
static inline void setc(m6809_t *c, int v) { c->cc = (c->cc & ~CC_C) | (v ? CC_C : 0); }

/* ---- 8-bit ALU ---- */
static uint8_t add8(m6809_t *c, uint8_t a, uint8_t b, int cin) {
    unsigned r = a + b + cin;
    uint8_t rr = (uint8_t)r;
    c->cc &= ~(CC_H | CC_N | CC_Z | CC_V | CC_C);
    if ((a ^ b ^ rr) & 0x10) c->cc |= CC_H;
    if (rr & 0x80) c->cc |= CC_N;
    if (!rr) c->cc |= CC_Z;
    if ((a ^ b ^ rr ^ (r >> 1)) & 0x80) c->cc |= CC_V;
    if (r & 0x100) c->cc |= CC_C;
    return rr;
}
static uint8_t sub8(m6809_t *c, uint8_t a, uint8_t b, int cin) {
    unsigned r = a - b - cin;
    uint8_t rr = (uint8_t)r;
    c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (rr & 0x80) c->cc |= CC_N;
    if (!rr) c->cc |= CC_Z;
    if ((a ^ b ^ rr ^ (r >> 1)) & 0x80) c->cc |= CC_V;
    if (r & 0x100) c->cc |= CC_C;
    return rr;
}
static uint16_t add16(m6809_t *c, uint16_t a, uint16_t b) {
    uint32_t r = (uint32_t)a + b;
    uint16_t rr = (uint16_t)r;
    c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (rr & 0x8000) c->cc |= CC_N;
    if (!rr) c->cc |= CC_Z;
    if ((a ^ b ^ rr ^ (r >> 1)) & 0x8000) c->cc |= CC_V;
    if (r & 0x10000) c->cc |= CC_C;
    return rr;
}
static uint16_t sub16(m6809_t *c, uint16_t a, uint16_t b) {
    uint32_t r = (uint32_t)a - b;
    uint16_t rr = (uint16_t)r;
    c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (rr & 0x8000) c->cc |= CC_N;
    if (!rr) c->cc |= CC_Z;
    if ((a ^ b ^ rr ^ (r >> 1)) & 0x8000) c->cc |= CC_V;
    if (r & 0x10000) c->cc |= CC_C;
    return rr;
}
static uint8_t neg8(m6809_t *c, uint8_t a) {
    uint8_t r = (uint8_t)(0 - a);
    c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x80) c->cc |= CC_N;
    if (!r) c->cc |= CC_Z;
    if (a == 0x80) c->cc |= CC_V;
    if (a != 0) c->cc |= CC_C;
    return r;
}
static uint8_t com8(m6809_t *c, uint8_t a) {
    uint8_t r = ~a;
    setnz8(c, r); setv(c, 0); setc(c, 1);
    return r;
}
static uint8_t lsr8(m6809_t *c, uint8_t a) {
    uint8_t r = a >> 1;
    c->cc &= ~(CC_N | CC_Z | CC_C);
    if (!r) c->cc |= CC_Z;
    if (a & 1) c->cc |= CC_C;
    return r;
}
static uint8_t ror8(m6809_t *c, uint8_t a) {
    uint8_t r = (a >> 1) | ((c->cc & CC_C) ? 0x80 : 0);
    setnz8(c, r); setc(c, a & 1);
    return r;
}
static uint8_t asr8(m6809_t *c, uint8_t a) {
    uint8_t r = (a >> 1) | (a & 0x80);
    setnz8(c, r); setc(c, a & 1);
    return r;
}
static uint8_t asl8(m6809_t *c, uint8_t a) {
    uint8_t r = a << 1;
    setnz8(c, r); setc(c, a & 0x80); setv(c, (a ^ r) & 0x80);
    return r;
}
static uint8_t rol8(m6809_t *c, uint8_t a) {
    uint8_t r = (a << 1) | ((c->cc & CC_C) ? 1 : 0);
    setnz8(c, r); setc(c, a & 0x80); setv(c, (a ^ r) & 0x80);
    return r;
}
static uint8_t dec8(m6809_t *c, uint8_t a) {
    uint8_t r = a - 1;
    setnz8(c, r); setv(c, a == 0x80);
    return r;
}
static uint8_t inc8(m6809_t *c, uint8_t a) {
    uint8_t r = a + 1;
    setnz8(c, r); setv(c, a == 0x7f);
    return r;
}
static void tst8(m6809_t *c, uint8_t a) { setnz8(c, a); setv(c, 0); }
static uint8_t clr8(m6809_t *c) {
    c->cc = (c->cc & ~(CC_N | CC_V | CC_C)) | CC_Z;
    return 0;
}
static uint8_t ld8(m6809_t *c, uint8_t v) { setnz8(c, v); setv(c, 0); return v; }
static uint8_t and8(m6809_t *c, uint8_t a, uint8_t b) { return ld8(c, a & b); }
static uint8_t or8(m6809_t *c, uint8_t a, uint8_t b) { return ld8(c, a | b); }
static uint8_t eor8(m6809_t *c, uint8_t a, uint8_t b) { return ld8(c, a ^ b); }

/* RMW dispatch on the low nibble of opcodes 0x0x/0x4x/0x5x/0x6x/0x7x. Returns 0 if illegal. */
static int rmw_op(m6809_t *c, int sel, uint8_t v, uint8_t *out, int *write) {
    *write = 1;
    switch (sel) {
    case 0x0: *out = neg8(c, v); return 1;
    case 0x3: *out = com8(c, v); return 1;
    case 0x4: *out = lsr8(c, v); return 1;
    case 0x6: *out = ror8(c, v); return 1;
    case 0x7: *out = asr8(c, v); return 1;
    case 0x8: *out = asl8(c, v); return 1;
    case 0x9: *out = rol8(c, v); return 1;
    case 0xA: *out = dec8(c, v); return 1;
    case 0xC: *out = inc8(c, v); return 1;
    case 0xD: tst8(c, v); *write = 0; return 1;
    case 0xF: *out = clr8(c); return 1;
    }
    return 0;
}

/* ---- indexed addressing: returns EA, adds postbyte cycles into *extra ---- */
static uint16_t indexed(m6809_t *c, int *extra) {
    uint8_t pb = fetch(c);
    uint16_t *r;
    switch ((pb >> 5) & 3) { case 0: r = &c->x; break; case 1: r = &c->y; break; case 2: r = &c->u; break; default: r = &c->s; }
    uint16_t ea;
    if (!(pb & 0x80)) {
        int off = pb & 0x1f; if (off & 0x10) off -= 32;
        *extra = 1;
        return (uint16_t)(*r + off);
    }
    int ind = pb & 0x10;
    switch (pb & 0x0f) {
    case 0x0: ea = *r; *r += 1; *extra = 2; break;
    case 0x1: ea = *r; *r += 2; *extra = 3; break;
    case 0x2: *r -= 1; ea = *r; *extra = 2; break;
    case 0x3: *r -= 2; ea = *r; *extra = 3; break;
    case 0x4: ea = *r; *extra = 0; break;
    case 0x5: ea = *r + (int8_t)c->b; *extra = 1; break;
    case 0x6: ea = *r + (int8_t)c->a; *extra = 1; break;
    case 0x8: ea = *r + (int8_t)fetch(c); *extra = 1; break;
    case 0x9: ea = *r + (int16_t)fetch16(c); *extra = 4; break;
    case 0xB: ea = *r + D(c); *extra = 4; break;
    case 0xC: { int8_t o = (int8_t)fetch(c); ea = c->pc + o; *extra = 1; break; }
    case 0xD: { int16_t o = (int16_t)fetch16(c); ea = c->pc + o; *extra = 5; break; }
    case 0xF: ea = fetch16(c); *extra = 2; break;
    default: c->illegal++; ea = *r; *extra = 0; break;
    }
    if (ind) { ea = rd16(c, ea); *extra += 3; }
    return ea;
}

static void push8(m6809_t *c, uint16_t *sp, uint8_t v) { (*sp)--; WR(*sp, v); }
static void push16(m6809_t *c, uint16_t *sp, uint16_t v) { push8(c, sp, v & 0xff); push8(c, sp, v >> 8); }
static uint8_t pull8(m6809_t *c, uint16_t *sp) { uint8_t v = RD(*sp); (*sp)++; return v; }
static uint16_t pull16(m6809_t *c, uint16_t *sp) { uint16_t h = pull8(c, sp); return (h << 8) | pull8(c, sp); }

static void push_all(m6809_t *c) {
    push16(c, &c->s, c->pc); push16(c, &c->s, c->u); push16(c, &c->s, c->y); push16(c, &c->s, c->x);
    push8(c, &c->s, c->dp); push8(c, &c->s, c->b); push8(c, &c->s, c->a); push8(c, &c->s, c->cc);
}

static int cond(m6809_t *c, int k) {
    int C = c->cc & CC_C, Z = (c->cc & CC_Z) != 0, N = (c->cc & CC_N) != 0, V = (c->cc & CC_V) != 0;
    switch (k & 0xf) {
    case 0x0: return 1;
    case 0x1: return 0;
    case 0x2: return !(C || Z);
    case 0x3: return C || Z;
    case 0x4: return !C;
    case 0x5: return C != 0;
    case 0x6: return !Z;
    case 0x7: return Z;
    case 0x8: return !V;
    case 0x9: return V;
    case 0xA: return !N;
    case 0xB: return N;
    case 0xC: return N == V;
    case 0xD: return N != V;
    case 0xE: return !Z && (N == V);
    default:  return Z || (N != V);
    }
}

static uint16_t *tfr_reg16(m6809_t *c, int r) {
    switch (r) { case 0: return NULL; case 1: return &c->x; case 2: return &c->y; case 3: return &c->u; case 4: return &c->s; case 5: return &c->pc; }
    return NULL;
}
static uint16_t exg_read(m6809_t *c, int r) {
    switch (r) {
    case 0: return D(c); case 1: return c->x; case 2: return c->y; case 3: return c->u; case 4: return c->s; case 5: return c->pc;
    case 8: return 0xff00 | c->a; case 9: return 0xff00 | c->b; case 10: return 0xff00 | c->cc; case 11: return 0xff00 | c->dp;
    }
    return 0xffff;
}
static void exg_write(m6809_t *c, int r, uint16_t v) {
    switch (r) {
    case 0: SETD(c, v); break; case 1: c->x = v; break; case 2: c->y = v; break; case 3: c->u = v; break;
    case 4: c->s = v; break; case 5: c->pc = v; break;
    case 8: c->a = (uint8_t)v; break; case 9: c->b = (uint8_t)v; break; case 10: c->cc = (uint8_t)v; break; case 11: c->dp = (uint8_t)v; break;
    }
}

void m6809_reset(m6809_t *c) {
    c->a = c->b = 0; c->x = c->y = c->u = c->s = 0; c->dp = 0;
    c->cc = CC_I | CC_F;
    c->halted = c->cwai = 0;
    c->pc = rd16(c, 0xfffe);
}

static int take_irq(m6809_t *c, int vec, int full) {
    c->in_fetch = 1;                       /* the interrupt sequence's own stack/vector accesses are not accesses of the interrupted instruction */
    if (full) { c->cc |= CC_E; push_all(c); }
    else { c->cc &= ~CC_E; push16(c, &c->s, c->pc); push8(c, &c->s, c->cc); }
    c->cc |= CC_I; if (!full) c->cc |= CC_F;
    c->pc = rd16(c, vec);
    c->in_fetch = 0;
    return full ? 19 : 10;
}

int m6809_step(m6809_t *c) {
    /* --- interrupts (checked between instructions) --- */
    if (c->nmi_pending && !c->nmi_delay) {               /* NMI (edge triggered, set by the board) */
        c->nmi_pending = 0; c->halted = 0;
        if (c->cwai) { c->cwai = 0; c->cc |= CC_I | CC_F; c->pc = rd16(c, 0xfffc); c->cycles += 7; return 7; }
        c->cc |= CC_E;  /* full frame */
        c->in_fetch = 1; push_all(c); c->cc |= CC_I | CC_F; c->pc = rd16(c, 0xfffc); c->in_fetch = 0;
        c->cycles += 19; return 19;
    }
    if (c->firq && !(c->cc & CC_F)) {
        c->halted = 0;
        if (c->cwai) { c->cwai = 0; c->cc |= CC_I | CC_F; c->pc = rd16(c, 0xfff6); c->cycles += 7; return 7; }
        int n = take_irq(c, 0xfff6, 0); c->cycles += n; return n;
    }
    if (c->irq && !c->irq_delay && !(c->cc & CC_I)) {
        c->halted = 0;
        if (c->cwai) { c->cwai = 0; c->cc |= CC_I; c->pc = rd16(c, 0xfff8); c->cycles += 7; return 7; }
        int n = take_irq(c, 0xfff8, 1); c->cycles += n; return n;
    }
    if (c->halted) {
        if (c->firq || c->irq || c->nmi) c->halted = 0;   /* masked interrupt line ends SYNC, execution continues */
        else { c->cycles += 1; return 1; }
    }
    if (c->cwai) { c->cycles += 1; return 1; }

    if (c->nmi_delay) c->nmi_delay--;
    if (c->irq_delay) c->irq_delay--;
    c->ppc = c->pc;
    uint8_t op = fetch(c);
    c->last_op = op;
    int cyc = 0, extra = 0;
    uint16_t ea;
    uint8_t v, r;
    int wr_;

    if (op == 0x10 || op == 0x11) {
        uint8_t o2 = fetch(c);
        int pre = op;
        if (pre == 0x10 && o2 >= 0x21 && o2 <= 0x2f) {      /* long branches */
            int16_t off = (int16_t)fetch16(c);
            if (cond(c, o2)) { c->pc += off; cyc = 6; } else cyc = 5;
            c->cycles += cyc; return cyc;
        }
        if (o2 == 0x3f) {   /* SWI2/SWI3 */
            c->cc |= CC_E; push_all(c);
            c->pc = rd16(c, pre == 0x10 ? 0xfff4 : 0xfff2);
            c->cycles += 20; return 20;
        }
        int hi = o2 >> 4, low = o2 & 0xf, mode = hi & 3;
        if (hi < 8) { c->illegal++; c->cycles += 2; return 2; }
        /* kind: 1 = compare, 2 = load, 3 = store */
        int kind = 0; uint16_t *reg = NULL;
        if (pre == 0x10) {
            if (low == 0x3 && hi <= 0xB) { kind = 1; }                                  /* CMPD */
            else if (low == 0xC && hi <= 0xB) { kind = 1; reg = &c->y; }                /* CMPY */
            else if (low == 0xE) { kind = 2; reg = (hi <= 0xB) ? &c->y : &c->s; }      /* LDY / LDS */
            else if (low == 0xF && hi >= 9) { kind = 3; reg = (hi <= 0xB) ? &c->y : &c->s; } /* STY / STS */
        } else {
            if (low == 0x3 && hi <= 0xB) { kind = 1; reg = &c->u; }                     /* CMPU */
            else if (low == 0xC && hi <= 0xB) { kind = 1; reg = &c->s; }                /* CMPS */
        }
        if (!kind || (kind == 3 && mode == 0)) { c->illegal++; c->cycles += 2; return 2; }
        uint16_t val = 0;
        if (mode == 0) { val = fetch16(c); ea = 0; }
        else if (mode == 1) ea = (c->dp << 8) | fetch(c);
        else if (mode == 2) ea = indexed(c, &extra);
        else ea = fetch16(c);
        if (kind == 1) { if (mode) val = rd16(c, ea); sub16(c, reg ? *reg : D(c), val); }
        else if (kind == 2) { if (mode) val = rd16(c, ea); *reg = val; setnz16(c, val); setv(c, 0); }
        else { wr16(c, ea, *reg); setnz16(c, *reg); setv(c, 0); }
        static const int bc[4] = {5, 7, 7, 8};   /* compare */
        static const int bl[4] = {4, 6, 6, 7};   /* load / store */
        cyc = (kind == 1 ? bc[mode] : bl[mode]) + (mode == 2 ? extra : 0);
        c->cycles += cyc; return cyc;
    }

    switch (op >> 4) {
    case 0x0: /* direct RMW */
        ea = (c->dp << 8) | fetch(c);
        if (op == 0x0E) { c->pc = ea; cyc = 3; break; }
        v = RD(ea);
        if (!rmw_op(c, op & 0xf, v, &r, &wr_)) { c->illegal++; cyc = 6; break; }
        if (wr_) WR(ea, r);
        cyc = 6; break;
    case 0x1:
        switch (op & 0xf) {
        case 0x2: cyc = 2; break;                                    /* NOP */
        case 0x3: c->halted = 1; cyc = 4; break;                     /* SYNC */
        case 0x6: { int16_t off = (int16_t)fetch16(c); c->pc += off; cyc = 5; break; }  /* LBRA */
        case 0x7: { int16_t off = (int16_t)fetch16(c); push16(c, &c->s, c->pc); c->pc += off; cyc = 9; break; } /* LBSR */
        case 0x9: { /* DAA */
            unsigned cf = 0, lo = c->a & 0xf, hi = c->a >> 4;
            if (lo > 9 || (c->cc & CC_H)) cf |= 0x06;
            if (hi > 9 || (c->cc & CC_C) || (hi > 8 && lo > 9)) cf |= 0x60;
            unsigned sum = c->a + cf;
            c->a = (uint8_t)sum;
            c->cc &= ~(CC_N | CC_Z | CC_V); /* C is only ever set; V cleared (MAME CLR_NZV) */
            if (sum & 0x100) c->cc |= CC_C;
            setnz8(c, c->a);
            cyc = 2; break; }
        case 0xA: c->cc |= fetch(c); cyc = 3; break;                /* ORCC */
        case 0xC: c->cc &= fetch(c); cyc = 3; break;               /* ANDCC */
        case 0xD: { uint16_t d = (int8_t)c->b; SETD(c, d); setnz16(c, d); } cyc = 2; break; /* SEX */
        case 0xE: { uint8_t pb = fetch(c); int r1 = pb >> 4, r2 = pb & 15;
            uint16_t a1 = exg_read(c, r1), a2 = exg_read(c, r2);
            exg_write(c, r1, a2); exg_write(c, r2, a1); cyc = 8; break; }
        case 0xF: { uint8_t pb = fetch(c); int r1 = pb >> 4, r2 = pb & 15;
            exg_write(c, r2, exg_read(c, r1)); cyc = 6; break; }
        default: c->illegal++; cyc = 2; break;
        }
        break;
    case 0x2: { /* short branches */
        int8_t off = (int8_t)fetch(c);
        if (cond(c, op)) c->pc += off;
        cyc = 3; break; }
    case 0x3:
        switch (op & 0xf) {
        case 0x0: case 0x1: case 0x2: case 0x3: {
            ea = indexed(c, &extra);
            uint16_t *rp = (op & 3) == 0 ? &c->x : (op & 3) == 1 ? &c->y : (op & 3) == 2 ? &c->s : &c->u;
            *rp = ea;
            if ((op & 3) < 2) c->cc = (c->cc & ~CC_Z) | (ea == 0 ? CC_Z : 0);
            cyc = 4 + extra; break; }
        case 0x4: case 0x6: { /* PSHS / PSHU */
            uint8_t m = fetch(c); uint16_t *sp = (op == 0x34) ? &c->s : &c->u; uint16_t other = (op == 0x34) ? c->u : c->s;
            cyc = 5;
            if (m & 0x80) { push16(c, sp, c->pc); cyc += 2; }
            if (m & 0x40) { push16(c, sp, other); cyc += 2; }
            if (m & 0x20) { push16(c, sp, c->y); cyc += 2; }
            if (m & 0x10) { push16(c, sp, c->x); cyc += 2; }
            if (m & 0x08) { push8(c, sp, c->dp); cyc++; }
            if (m & 0x04) { push8(c, sp, c->b); cyc++; }
            if (m & 0x02) { push8(c, sp, c->a); cyc++; }
            if (m & 0x01) { push8(c, sp, c->cc); cyc++; }
            break; }
        case 0x5: case 0x7: { /* PULS / PULU */
            uint8_t m = fetch(c); uint16_t *sp = (op == 0x35) ? &c->s : &c->u;
            cyc = 5;
            if (m & 0x01) { c->cc = pull8(c, sp); cyc++; }
            if (m & 0x02) { c->a = pull8(c, sp); cyc++; }
            if (m & 0x04) { c->b = pull8(c, sp); cyc++; }
            if (m & 0x08) { c->dp = pull8(c, sp); cyc++; }
            if (m & 0x10) { c->x = pull16(c, sp); cyc += 2; }
            if (m & 0x20) { c->y = pull16(c, sp); cyc += 2; }
            if (m & 0x40) { uint16_t t = pull16(c, sp); if (op == 0x35) c->u = t; else c->s = t; cyc += 2; }
            if (m & 0x80) { c->pc = pull16(c, sp); cyc += 2; }
            break; }
        case 0x9: c->pc = pull16(c, &c->s); cyc = 5; break;         /* RTS */
        case 0xA: c->x += c->b; cyc = 3; break;                      /* ABX */
        case 0xB:                                                     /* RTI */
            c->cc = pull8(c, &c->s); cyc = 6;
            if (c->cc & CC_E) { c->a = pull8(c, &c->s); c->b = pull8(c, &c->s); c->dp = pull8(c, &c->s);
                c->x = pull16(c, &c->s); c->y = pull16(c, &c->s); c->u = pull16(c, &c->s); cyc = 15; }
            c->pc = pull16(c, &c->s); break;
        case 0xC: { /* CWAI */
            c->cc &= fetch(c); c->cc |= CC_E; push_all(c); c->cwai = 1; cyc = 20; break; }
        case 0xD: { uint16_t p = (uint16_t)(c->a * c->b); SETD(c, p);
            c->cc &= ~(CC_Z | CC_C); if (!p) c->cc |= CC_Z; if (c->b & 0x80) c->cc |= CC_C; cyc = 11; break; }
        case 0xF: c->cc |= CC_E; push_all(c); c->cc |= CC_I | CC_F; c->pc = rd16(c, 0xfffa); cyc = 19; break; /* SWI */
        default: c->illegal++; cyc = 2; break;
        }
        break;
    case 0x4: case 0x5: { /* inherent A / B */
        uint8_t *reg = (op & 0x10) ? &c->b : &c->a;
        if (!rmw_op(c, op & 0xf, *reg, &r, &wr_)) { c->illegal++; cyc = 2; break; }
        if (wr_) *reg = r;
        cyc = 2; break; }
    case 0x6: case 0x7:
        if ((op >> 4) == 6) ea = indexed(c, &extra); else ea = fetch16(c);
        if ((op & 0xf) == 0xE) { c->pc = ea; cyc = ((op >> 4) == 6) ? 3 + extra : 4; break; }
        v = RD(ea);
        if (!rmw_op(c, op & 0xf, v, &r, &wr_)) { c->illegal++; cyc = 6; break; }
        if (wr_) WR(ea, r);
        cyc = ((op >> 4) == 6) ? 6 + extra : 7; break;
    case 0x8: case 0x9: case 0xA: case 0xB:
    case 0xC: case 0xD: case 0xE: case 0xF: {
        int mode = (op >> 4) & 3;   /* 0 imm, 1 direct, 2 indexed, 3 extended */
        int isB = (op >> 4) >= 0xC;
        int low = op & 0xf;
        int is16 = 0;
        if (!isB) is16 = (low == 0x3 || low == 0xC || low == 0xE || low == 0xF || low == 0xD);
        else is16 = (low == 0x3 || low == 0xC || low == 0xD || low == 0xE || low == 0xF);
        /* BSR / JSR */
        if (!isB && low == 0xD) {
            if (mode == 0) { int8_t off = (int8_t)fetch(c); push16(c, &c->s, c->pc); c->pc += off; cyc = 7; break; }
            if (mode == 1) ea = (c->dp << 8) | fetch(c); else if (mode == 2) ea = indexed(c, &extra); else ea = fetch16(c);
            push16(c, &c->s, c->pc); c->pc = ea;
            cyc = mode == 1 ? 7 : mode == 2 ? 7 + extra : 8; break;
        }
        /* operand address */
        uint16_t imm16 = 0; uint8_t imm8 = 0;
        if (mode == 0) { if (is16) imm16 = fetch16(c); else imm8 = fetch(c); ea = 0; }
        else if (mode == 1) ea = (c->dp << 8) | fetch(c);
        else if (mode == 2) ea = indexed(c, &extra);
        else ea = fetch16(c);
        int mc = (mode == 0) ? 0 : (mode == 1) ? 1 : (mode == 2) ? 2 : 3;
        if (!is16) {
            uint8_t *reg = isB ? &c->b : &c->a;
            uint8_t m = 0;
            int store = (low == 0x7);
            if (!store) m = (mode == 0) ? imm8 : RD(ea);
            switch (low) {
            case 0x0: *reg = sub8(c, *reg, m, 0); break;
            case 0x1: sub8(c, *reg, m, 0); break;
            case 0x2: *reg = sub8(c, *reg, m, (c->cc & CC_C) ? 1 : 0); break;
            case 0x4: *reg = and8(c, *reg, m); break;
            case 0x5: and8(c, *reg, m); break;
            case 0x6: *reg = ld8(c, m); break;
            case 0x7: if (mode == 0) { c->illegal++; } else { WR(ea, *reg); setnz8(c, *reg); setv(c, 0); } break;
            case 0x8: *reg = eor8(c, *reg, m); break;
            case 0x9: *reg = add8(c, *reg, m, (c->cc & CC_C) ? 1 : 0); break;
            case 0xA: *reg = or8(c, *reg, m); break;
            case 0xB: *reg = add8(c, *reg, m, 0); break;
            default: c->illegal++; break;
            }
            static const int base8[4] = {2, 4, 4, 5};
            cyc = base8[mc] + (mode == 2 ? extra : 0);
        } else {
            /* 16-bit ops */
            uint16_t m = 0;
            int store = 0;
            uint16_t *regp = NULL;
            int kind = 0; /* 1 = sub/add/cmp class (6,7 base), 2 = load/store (5,6 base) */
            if (!isB) {
                switch (low) {
                case 0x3: kind = 1; break;           /* SUBD */
                case 0xC: kind = 1; regp = &c->x; break; /* CMPX */
                case 0xE: kind = 2; regp = &c->x; break; /* LDX */
                case 0xF: kind = 2; regp = &c->x; store = 1; break; /* STX */
                }
            } else {
                switch (low) {
                case 0x3: kind = 1; break;           /* ADDD */
                case 0xC: kind = 2; break;           /* LDD */
                case 0xD: kind = 2; store = 1; break;/* STD */
                case 0xE: kind = 2; regp = &c->u; break; /* LDU */
                case 0xF: kind = 2; regp = &c->u; store = 1; break; /* STU */
                }
            }
            if (!store) m = (mode == 0) ? imm16 : rd16(c, ea);
            if (!isB && low == 0x3) { SETD(c, sub16(c, D(c), m)); }
            else if (!isB && low == 0xC) { sub16(c, c->x, m); }
            else if (isB && low == 0x3) { SETD(c, add16(c, D(c), m)); }
            else if (low == 0xC && isB) { SETD(c, m); setnz16(c, m); setv(c, 0); }
            else if (low == 0xD && isB) { if (mode == 0) c->illegal++; else { wr16(c, ea, D(c)); setnz16(c, D(c)); setv(c, 0); } }
            else if (low == 0xE) { *regp = m; setnz16(c, m); setv(c, 0); }
            else if (low == 0xF) { if (mode == 0) c->illegal++; else { wr16(c, ea, *regp); setnz16(c, *regp); setv(c, 0); } }
            else c->illegal++;
            static const int b1[4] = {4, 6, 6, 7};
            static const int b2[4] = {3, 5, 5, 6};
            cyc = (kind == 1 ? b1[mc] : b2[mc]) + (mode == 2 ? extra : 0);
            if (kind == 0) cyc = 4;
        }
        break; }
    }
    c->cycles += cyc;
    return cyc;
}
