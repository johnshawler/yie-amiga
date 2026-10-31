/* minimal C-side symbols for the per-instruction tests (no board, no scheduler): helper executor, unused stubs */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "m6809.h"
static m6809_t hcpu; static uint8_t hmem[4];
static uint8_t h_rd(void *c, uint16_t a) { (void)c; return a < 4 ? hmem[a] : 0; }
static void h_wr(void *c, uint16_t a, uint8_t v) { (void)c; (void)a; (void)v; }
void rt_helper_c(uint8_t *s) {
    memset(&hcpu, 0, sizeof hcpu);
    hcpu.rd = h_rd; hcpu.wr = h_wr;
    hcpu.a = s[0]; hcpu.b = s[1]; hcpu.x = (s[2] << 8) | s[3]; hcpu.cc = s[4] | CC_I | CC_F; hcpu.s = 0x1000;
    hmem[0] = s[5]; hmem[1] = s[6]; hcpu.pc = 0;
    m6809_step(&hcpu);
    s[0] = hcpu.a; s[1] = hcpu.b; s[2] = hcpu.x >> 8; s[3] = hcpu.x; s[4] = (hcpu.cc & ~(CC_I | CC_F)) | (s[4] & (CC_I | CC_F));
}
uint32_t rt_io_rd_c(uint32_t a, uint32_t b, uint32_t c) { printf("unexpected I/O read %x\n", a); exit(9); }
void rt_io_wr_c(uint32_t a, uint32_t d, uint32_t b, uint32_t c) { printf("unexpected I/O write %x\n", a); exit(9); }
void rt_slow_c(void *r) { printf("unexpected slow path\n"); exit(9); }
void rt_chk_c(uint32_t ea, uint32_t pc) { printf("chk %x %x\n", ea, pc); exit(9); }
void rt_trap_c(void) { exit(9); }

uint8_t in_sys, in_p1, in_p2, dsw1, dsw2, dsw3, board_sn_latch;
void board_wr_ctrl(uint8_t v) { (void)v; }
