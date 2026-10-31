/* Game sound for the Amiga port: SN76489A synthesis (runtime/snsynth.c) into the Paula ring, and the VLM5030 speech phrases (captured from MAME, LZ4) played
 * on Paula channels 2/3 at the moment the corresponding stream block starts.
 *   - one stream block per emulated frame (25344 cycles = 16.5 ms) with the number of samples Paula consumes in that time at period 124
 *     (PAL 28603.99 Hz: 471.95 samples per frame -> 472 or 470, always an even count);
 *   - a VLM START at cycle c in frame f plays `lead` = (c - f*25344) * 8135 / 1536000 zero samples followed by the phrase, started when block f starts to play,
 *     so the speech has the same position relative to the SN76489A as in the arcade (to one speech sample = 123 us). */
#include <stdint.h>
#include "platform.h"
#include "board.h"
#include "snsynth.h"
#include "lz4.h"
#include "gamesound.h"

extern const uint8_t vlm_blob[];

#define VLM_BUF_BYTES 7424
#define VLM_NBUF 4
static int8_t vlm_buf[VLM_NBUF][VLM_BUF_BYTES] __attribute__((section(".chipbuf"), aligned(4)));
static uint8_t vlm_next;

typedef struct { uint32_t frame; uint8_t kind; uint8_t buf; uint16_t words; } vreq;   /* kind 1 = start, 2 = stop */
#define VQ 16
static vreq vq[VQ]; static volatile uint8_t vq_head, vq_tail;
static uint32_t cur_frame;                            /* index of the frame being emulated (the next frame hook call has fr == cur_frame) */
static uint32_t wps24, wfrac;                         /* words per frame, 8.24 fixed point; fractional accumulator */
uint32_t gs_vlm_started, gs_vlm_cut;

static void vq_put(uint32_t frame, uint8_t kind, uint8_t buf, uint16_t words) {
    uint8_t n = (uint8_t)((vq_tail + 1) & (VQ - 1));
    if (n == vq_head) return;                          /* full (cannot happen: a few requests per second) */
    vq[vq_tail].frame = frame; vq[vq_tail].kind = kind; vq[vq_tail].buf = buf; vq[vq_tail].words = words; vq_tail = n;
}

/* audio interrupt: block `tag` has just started playing */
static void on_block(uint32_t tag) {
    while (vq_head != vq_tail && (tag == 0xFFFFFFFFu || (int32_t)(tag - vq[vq_head].frame) >= 0)) {
        vreq *r = &vq[vq_head];
        if (r->kind == 1) {
            unsigned per = plat_is_pal ? 436 : 440;      /* Paula clock / 8135 Hz (the integer stream rate of MAME's VLM5030) */
            plat_audio_play(3, vlm_buf[r->buf], r->words, per, 64, 0);
            plat_audio_play(2, vlm_buf[r->buf], r->words, per, 64, 0);
        } else { plat_audio_stop(3); plat_audio_stop(2); }
        vq_head = (uint8_t)((vq_head + 1) & (VQ - 1));
    }
}

static void on_sn_write(uint64_t now, uint8_t d) { sns_write(now, d); }

static void on_vlm_start(uint64_t now, int phrase) {
    uint32_t off = (uint32_t)(now - (uint64_t)cur_frame * SNS_FRAME_CYCLES);          /* cycles into the current frame */
    if (off > SNS_FRAME_CYCLES - 1) off = SNS_FRAME_CYCLES - 1;
    uint32_t lead = off * 8135u / 1536000u;
    const uint8_t *h = vlm_blob + (phrase & 127) * 8;
    uint32_t o = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
    uint32_t clen = ((uint32_t)h[4] << 8) | h[5], n = ((uint32_t)h[6] << 8) | h[7];
    uint8_t b = vlm_next; vlm_next = (uint8_t)((vlm_next + 1) % VLM_NBUF);
    int8_t *d = vlm_buf[b];
    for (uint32_t i = 0; i < lead; i++) d[i] = 0;
    if (lz4_decode(vlm_blob + o, (int)clen, (uint8_t *)d + lead, (int)n) < 0) return;
    uint32_t total = lead + n; if (total & 1) d[total++] = 0;
    vq_put(cur_frame, 1, b, (uint16_t)(total / 2));
    gs_vlm_started++;
}
static void on_vlm_reset(uint64_t now) { (void)now; vq_put(cur_frame, 2, 0, 0); gs_vlm_cut++; }

void gs_init(void) {
    /* words per frame = 25344 cycles / 1.536 MHz * (Paula clock / period 124) / 2, 8.24 fixed point (PAL clock 3546895, NTSC 3579545) */
    wps24 = plat_is_pal ? WPS24_PAL : WPS24_NTSC;
    sns_init();
    hooks.sn_write = on_sn_write; hooks.vlm_start = on_vlm_start; hooks.vlm_reset = on_vlm_reset;
    plat_snd_block_hook = on_block;
    plat_snd_start();
}

/* The block is synthesised as soon as the frame has been emulated and queued for Paula when the real-time tick of the frame arrives, so that the stream
 * is fed at the regular 60.6 Hz tick and not at the (jittering) moment at which the emulation of the frame happens to finish. */
static uint32_t pend_fr; static unsigned pend_words; static int pend_idle;
uint32_t gs_lines_sum, gs_lines_max;
void gs_frame_prepare(uint32_t fr) {
    uint32_t l0 = plat_scanlines();
    cur_frame = fr + 1;
    wfrac += wps24 & 0xFFFFFFu;
    pend_words = (wps24 >> 24) + (wfrac >> 24); wfrac &= 0xFFFFFFu;
    /* synthesise into fast RAM, then copy to the chip RAM slot in long words: byte writes to chip RAM are very slow while the display DMA is running */
    static int8_t scratch[PLAT_SND_SLOT_BYTES] __attribute__((aligned(4)));
    pend_idle = sns_render(fr, (int)(pend_words * 2), scratch, 0);
    plat_copy_to_chip(plat_snd_slot(), scratch, pend_words * 2);
    pend_fr = fr;
    { uint32_t d = plat_scanlines() - l0; gs_lines_sum += d; if (d > gs_lines_max) gs_lines_max = d; }
}
void gs_frame_commit(void) { plat_snd_commit(pend_words, pend_fr, pend_idle); }
