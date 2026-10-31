/* Per-instruction translation tests: translated 68020 snippets vs the 6809 reference core, run under qemu-m68k. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "m6809.h"

struct regs { uint32_t d[8]; uint32_t a[7]; uint32_t ccr; };
extern void call_native(struct regs *r, void (*fn)(void));
extern uint8_t rt_vm[16];
#include "tests_decl.inc"
typedef struct { const char *name; void (*fn)(void); int len; uint8_t raw[8]; unsigned live; int cycles; int accmode; } test_t;
static const test_t TESTS[] = {
#include "tests.inc"
};
#define NT (sizeof TESTS / sizeof TESTS[0])

static uint8_t refmem[65536 + 512], natmem[65536 + 512];
static uint32_t rs = 12345; static int inited;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint8_t rd(void *c, uint16_t a) { return refmem[a]; }
static void wr(void *c, uint16_t a, uint8_t v) { refmem[a] = v; }
extern uint8_t *image_base;

int main(int argc, char **argv) {
    int iters = argc > 1 ? atoi(argv[1]) : 40;
    int only = argc > 2 ? atoi(argv[2]) : -1;
    long fails = 0, runs = 0; int failing_tests = 0;
    image_base = natmem;
    for (unsigned t = 0; t < NT; t++) {
        if (only >= 0 && (int)t != only) continue;
        const test_t *T = &TESTS[t]; int tf = 0;
        for (int it = 0; it < iters; it++) {
            if (!inited) { for (int k = 0; k < 65536 + 512; k++) refmem[k] = rnd() >> 11; inited = 1; }
            for (int k = 0x5000; k < 0x6000; k++) refmem[k] = rnd() >> 11;
            { int base = 0x8000 + (rnd() % 0x7000); for (int k = 0; k < 0x600; k++) refmem[base + k] = rnd() >> 11; }
            if (rnd() & 1) for (int k = 0x5000; k < 0x6000; k += 1 + (rnd() & 7)) refmem[k] = (rnd() & 2) ? 0 : (rnd() & 4 ? 0xFF : 0x80);
            m6809_t c; memset(&c, 0, sizeof c);
            c.rd = rd; c.wr = wr;
            uint16_t pcaddr = 0x9F00;
            for (int k = 0; k < T->len; k++) refmem[pcaddr + k] = T->raw[k];
            c.a = rnd() >> 7; c.b = rnd() >> 9;
            if (rnd() & 3) { if (rnd() & 1) c.a = 0, c.b = rnd() & 1 ? 0 : 0x80; }
            if (T->accmode == 1) { c.a = (rnd() & 1) ? 0 : 0xFF; }                   /* D,r : keep the effective address inside RAM */
            if (T->accmode == 2) { c.a = (int8_t)((int)(rnd() % 100) - 50); c.b = (int8_t)((int)(rnd() % 100) - 50); }
            c.x = 0x5100 + (rnd() % 0xC00); c.y = 0x5100 + (rnd() % 0xC00); c.u = 0x5100 + (rnd() % 0xC00);
            if (rnd() % 4 == 0) { c.x = 0x8100 + (rnd() % 0x7C00); c.y = 0x8100 + (rnd() % 0x7C00); c.u = 0x8100 + (rnd() % 0x7C00); }
            c.s = 0x5400 + (rnd() % 0x800);
            int dp = 0x50 + (rnd() % 16); c.dp = dp; c.cc = (rnd() & 0xFF); c.pc = pcaddr; c.irq = c.firq = 0;
            memcpy(natmem, refmem, 65536);
            uint8_t cc0 = c.cc;
            struct regs r; memset(&r, 0, sizeof r);
            r.d[0] = c.a; r.d[1] = c.b; r.d[2] = c.x; r.d[3] = c.y; r.d[4] = c.u;
            r.d[5] = rnd(); r.d[6] = rnd(); r.d[7] = rnd();
            r.a[2] = (uint32_t)natmem + (dp << 8); r.a[1] = (uint32_t)natmem; r.a[4] = (uint32_t)natmem + c.s; r.a[5] = (uint32_t)natmem + 0x8000; r.a[6] = (uint32_t)rt_vm;
            r.a[0] = rnd(); r.a[3] = rnd();
            r.ccr = (cc0 & 0x0F) | ((rnd() & 1) << 4);
            rt_vm[0] = cc0 & 0xF0; rt_vm[1] = dp;
            int cyc = m6809_step(&c);
            call_native(&r, T->fn);
            int bad = 0; char why[160] = "";
            if (cyc != T->cycles) { bad = 1; snprintf(why, sizeof why, "cycles core %d model %d", cyc, T->cycles); }
            else if ((uint8_t)r.d[0] != c.a) { bad = 1; snprintf(why, sizeof why, "A %02x vs %02x", (uint8_t)r.d[0], c.a); }
            else if ((uint8_t)r.d[1] != c.b) { bad = 1; snprintf(why, sizeof why, "B %02x vs %02x", (uint8_t)r.d[1], c.b); }
            else if (r.d[2] != c.x) { bad = 1; snprintf(why, sizeof why, "X %x vs %04x", r.d[2], c.x); }
            else if (r.d[3] != c.y) { bad = 1; snprintf(why, sizeof why, "Y %x vs %04x", r.d[3], c.y); }
            else if (r.d[4] != c.u) { bad = 1; snprintf(why, sizeof why, "U %x vs %04x", r.d[4], c.u); }
            else if ((uint32_t)(r.a[4] - (uint32_t)natmem) != c.s) { bad = 1; snprintf(why, sizeof why, "S %x vs %04x", (uint32_t)(r.a[4] - (uint32_t)natmem), c.s); }
            else {
                unsigned ref_f = c.cc & 0x0F, nat_f = r.ccr & 0x0F;
                if ((ref_f ^ nat_f) & T->live) { bad = 1; snprintf(why, sizeof why, "flags NZVC ref=%x nat=%x live=%x (cc0=%02x)", ref_f, nat_f, T->live, cc0); }
                else if (rt_vm[0] != (c.cc & 0xF0)) { bad = 1; snprintf(why, sizeof why, "vm_cc %02x vs ref %02x", rt_vm[0], c.cc & 0xF0); }
                else if (rt_vm[1] != c.dp) { bad = 1; snprintf(why, sizeof why, "dp %02x vs %02x", rt_vm[1], c.dp); }
                else if (memcmp(refmem + 0x4000, natmem + 0x4000, 0x2000)) {
                    for (int k = 0x4000; k < 0x6000; k++) if (refmem[k] != natmem[k]) { bad = 1; snprintf(why, sizeof why, "mem[%04x] %02x vs %02x", k, natmem[k], refmem[k]); break; }
                }
            }
            runs++;
            if (bad) { fails++; if (tf++ == 0 && failing_tests < 60) { failing_tests++; printf("FAIL test %u '%s' (live %x): %s\n", t, T->name, T->live, why); } }
        }
    }
    printf("tests=%u runs=%ld fails=%ld failing_tests=%d\n", (unsigned)NT, runs, fails, failing_tests);
    return fails != 0;
}
