/* SN76489A synthesis for the Konami GX407 board: exact event-driven model of MAME 0.264's sn76496 (see amiga/sound/snref.c, which is checked
 * against MAME audio sample by sample), integrated over output boxes (box filter) instead of being sampled.  Portable (no Amiga names).
 *
 * Time base: CPU cycle c of the 6809 (1.536 MHz) maps to model sample s = ceil(c/2) of the 768 kHz stream MAME runs the chip at; the chip ticks
 * at s = 7 + 8n (96 kHz); one game frame = 25344 cycles = 12672 model samples = 1584 ticks.
 * Output: for each frame, N signed 8-bit samples; sample i = box average of the chip output over [i*12672/N, (i+1)*12672/N) of the frame,
 * mapped 0..32767 -> -128..127 with first-order error feedback (the quantisation error of one output sample is carried into the next). */
#ifndef SNSYNTH_H
#define SNSYNTH_H
#include <stdint.h>

#define SNS_FRAME_CYCLES 25344u

void sns_init(void);                                      /* chip power-on state, MAME 0.264 */
void sns_write(uint64_t cycle, uint8_t byte);             /* record a byte written to the chip (cycle of the strobe write) */
/* synthesise game frame `frame` (cycles frame*25344 .. (frame+1)*25344) into n samples (n even, 2..512).  avg15 (optional, debug): the
 * un-quantised box average 0..32767 << 16 (i.e. 32.0 fixed point of the 15-bit value) per sample. */
int  sns_render(uint32_t frame, int n, int8_t *out, uint32_t *avg16);     /* returns 1 if every sample of the block is the idle level (-128) */
extern uint32_t sns_stat_events;                          /* toggles processed (diagnostic) */
#endif
