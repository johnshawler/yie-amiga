/* Board model of the Konami GX407 "Yie Ar Kung-Fu" board: memory map, I/O, interrupts, timing, scheduler of the two interrupt sources.
 * Portable (host reference emulator, qemu harness and Amiga build share this file).  Memory map / I/O / interrupts from MAME
 * src/mame/konami/yiear.cpp (pinned snapshot): see amiga/docs/FACTS.md. */
#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>
#include "m6809.h"

#define MEM_SIZE 65536

enum { TIMING_PINNED = 0, TIMING_MAME0264 = 1 };

typedef struct board_hooks {
    /* all times are CPU cycles (1.536 MHz) since reset; called at the cycle of the instruction start */
    void (*sn_write)(uint64_t now, uint8_t data);             /* byte written to the SN76489A (via $4800 latch / $4900 strobe) */
    void (*vlm_start)(uint64_t now, int phrase);              /* VLM5030 ST rising edge: phrase number */
    void (*vlm_reset)(uint64_t now);
    void (*frame)(uint64_t now, uint32_t frame);              /* end of frame == start of vertical blank (before the IRQ line is raised) */
    void (*io_log)(char kind, uint64_t now, uint16_t addr, uint8_t v);   /* optional trace of I/O accesses */
} board_hooks;

extern uint8_t *mem;                 /* 64 KB flat image: RAM $5000-$5FFF, ROM $8000-$FFFF (and I/O latches are not stored) */
extern m6809_t cpu;
extern board_hooks hooks;
extern uint8_t in_sys, in_p1, in_p2; /* active-low input ports $4E00-$4E02 */
extern uint8_t dsw1, dsw2, dsw3;

void board_init(uint8_t *image, int timing);       /* image: 64 KB buffer with the ROM already at $8000 */
void board_reset(void);

/* scheduler */
uint64_t board_next_event(void);                  /* floor of the time of the next event, in cycles */
void board_fire(void);                            /* process every event whose time <= current cpu.cycles (call at an instruction boundary) */
uint32_t board_frame_count(void);
int board_flip(void);                              /* screen flip latch (bit 0 of the control port $4000) */

extern uint8_t board_sn_latch;
void board_wr_ctrl(uint8_t v);
/* I/O */
uint8_t board_io_rd(uint16_t a, uint64_t now);
void board_io_wr(uint16_t a, uint8_t v, uint64_t now);

/* interpreter bus (used by the 6809 core) */
uint8_t board_rd(void *ctx, uint16_t a);
void board_wr(void *ctx, uint16_t a, uint8_t v);

/* run the pure interpreter until cpu.cycles reaches `until` (events are handled on the way) */
void board_run(uint64_t until);
#endif
