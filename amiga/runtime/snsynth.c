/* See snsynth.h.  State per channel is kept lazily: (cnt, out) are valid at tick `tref`; an audible channel (non-zero volume) additionally has the
 * tick `nxt` of its next toggle scheduled, a silent channel is brought up to date only when something needs it (write, becoming audible) using the closed form
 *   k ticks from (cnt, per): first toggle after a = max(cnt,1) ticks, then every q = max(per,1) ticks;  cnt' = per - (k-a) mod q
 * and for the noise LFSR (17 bits, linear over GF(2) for a given mode) a jump by matrix powers.  All tick numbers are relative to the start of the frame
 * being rendered (1584 ticks per frame); tick n happens at model sample 7 + 8n. */
#include "snsynth.h"

#define TICKS_PER_FRAME 1584
#define SAMPLES_PER_FRAME 12672
#define NEVER 0x7FFFFFFF

/* volume table: MAME 0.264 builds it from MAX_OUTPUT/4 = 8191 with 2 dB steps; entries are ints, 15 = off */
static const uint16_t voltab[16] = { 8191, 6506, 5168, 4105, 3260, 2589, 2057, 1634, 1298, 1031, 819, 650, 516, 410, 326, 0 };

typedef struct { int32_t cnt; int32_t per; int32_t tref; int32_t nxt; uint8_t out; uint8_t aud; } chan_t;
static chan_t ch[4];
static uint32_t nt[4];                                      /* time (model sample within the frame) of the next toggle of an audible channel, NEVER otherwise */
static uint16_t reg[8];
static uint8_t last_reg;
static uint32_t vol[4];
static uint32_t rng;                                       /* 17-bit LFSR state */
static uint32_t amp;                                       /* current output amplitude (sum of audible high channels) */

uint32_t sns_stat_events;

/* ---- write queue ---- */
#define WQ 256
static uint64_t wq_cyc[WQ]; static uint8_t wq_dat[WQ]; static unsigned wq_head, wq_tail;
void sns_write(uint64_t cycle, uint8_t byte) {
    if (((wq_tail + 1) & (WQ - 1)) == wq_head) return;      /* full: cannot happen with one render per frame (the game writes < 100 bytes per frame) */
    wq_cyc[wq_tail] = cycle; wq_dat[wq_tail] = byte; wq_tail = (wq_tail + 1) & (WQ - 1);
}

/* ---- LFSR jump tables: pw[mode][i][j] = image of basis bit j after 2^i shifts ---- */
static uint32_t pw[2][32][17];
static uint32_t lfsr_step1(uint32_t s, int white) {
    uint32_t fb = ((s >> 2) & 1) ^ (white ? ((s >> 3) & 1) : 0);
    return (s >> 1) | (fb << 16);
}
static void lfsr_tables(void) {
    for (int m = 0; m < 2; m++) {
        for (int j = 0; j < 17; j++) pw[m][0][j] = lfsr_step1(1u << j, m);
        for (int i = 1; i < 32; i++)
            for (int j = 0; j < 17; j++) {
                uint32_t v = pw[m][i - 1][j], r = 0;
                for (int b = 0; b < 17; b++) if (v >> b & 1) r ^= pw[m][i - 1][b];
                pw[m][i][j] = r;
            }
    }
}
static uint32_t lfsr_jump(uint32_t s, int white, uint32_t n) {
    for (int i = 0; n; i++, n >>= 1)
        if (n & 1) { uint32_t r = 0; for (int b = 0; b < 17; b++) if (s >> b & 1) r ^= pw[white][i][b]; s = r; }
    return s;
}

void sns_init(void) {
    static int tables_done;
    if (!tables_done) { lfsr_tables(); tables_done = 1; }
    for (int i = 0; i < 4; i++) { ch[i].cnt = 0; ch[i].per = 0; ch[i].tref = 0; ch[i].nxt = NEVER; ch[i].out = 0; ch[i].aud = 0; vol[i] = 0; nt[i] = NEVER; }
    for (int i = 0; i < 8; i++) reg[i] = 0;
    last_reg = 3;                                           /* "sega style" power-on selection in MAME 0.264 (SN76489A) */
    rng = 0x10000; ch[3].out = (uint8_t)(rng & 1);
    amp = 0; wq_head = wq_tail = 0; sns_stat_events = 0;
}

static inline int32_t imax1(int32_t x) { return x > 1 ? x : 1; }
static inline void sched(int c) { nt[c] = ch[c].aud ? 7u + 8u * (uint32_t)ch[c].nxt : (uint32_t)NEVER; }

/* bring a channel up to date at `T` (= number of ticks already processed in this frame; all toggles before T were handled) */
static void catchup(int c, int32_t T) {
    chan_t *h = &ch[c];
    int32_t k = T - h->tref;
    if (k <= 0) return;
    int32_t a = imax1(h->cnt);
    if (k < a) { h->cnt -= k; }
    else {
        int32_t q = imax1(h->per), rest = k - a;
        uint32_t nt = 1 + (uint32_t)rest / (uint32_t)q;
        int32_t rem = rest % q;
        if (c == 3) {
            rng = lfsr_jump(rng, (reg[6] >> 2) & 1, nt);
            h->out = (uint8_t)(rng & 1);
        } else if (nt & 1) h->out ^= 1;
        h->cnt = h->per - rem;
    }
    h->tref = T;
    if (h->aud) { h->nxt = T + imax1(h->cnt) - 1; sched(c); }
}

static void recalc_amp(void) { uint32_t a = 0; for (int i = 0; i < 4; i++) if (ch[i].out) a += vol[i]; amp = a; }

static void set_vol(int c, int32_t T, uint32_t v) {
    chan_t *h = &ch[c];
    vol[c] = v;
    if (v && !h->aud) { catchup(c, T); h->aud = 1; h->nxt = h->tref + imax1(h->cnt) - 1; sched(c); }
    else if (!v) { h->aud = 0; h->nxt = NEVER; sched(c); }
}

static void apply_write(uint8_t d, int32_t T) {
    int r;
    if (d & 0x80) r = (d >> 4) & 7; else r = last_reg;
    int c = r >> 1;
    if (r == 6) catchup(3, T);                              /* the LFSR mode (bit 2 of reg 6) is about to change */
    else if (r == 0 || r == 2 || r == 4) catchup(c, T);
    if (d & 0x80) { last_reg = (uint8_t)r; reg[r] = (uint16_t)((reg[r] & 0x3F0) | (d & 0x0F)); }
    switch (r) {
    case 0: case 2: case 4:
        if (!(d & 0x80)) reg[r] = (uint16_t)((reg[r] & 0x0F) | ((d & 0x3F) << 4));
        ch[c].per = reg[r] ? reg[r] : 0x400;
        if (r == 4 && (reg[6] & 3) == 3) { catchup(3, T); ch[3].per = ch[2].per << 1; }
        break;
    case 1: case 3: case 5: case 7:
        set_vol(c, T, voltab[d & 15]);
        if (!(d & 0x80)) reg[r] = (uint16_t)((reg[r] & 0x3F0) | (d & 0x0F));
        break;
    case 6: {
        if (!(d & 0x80)) reg[6] = (uint16_t)((reg[6] & 0x3F0) | (d & 0x0F));
        int n = reg[6];
        ch[3].per = ((n & 3) == 3) ? (ch[2].per << 1) : (1 << (5 + (n & 3)));
        rng = 0x10000;                                      /* the output bit is NOT updated until the next shift */
        break; }
    }
    recalc_amp();
}

/* ---- box integration ----
 * Box i covers [i*bw, (i+1)*bw) (16.16 positions in the frame); the last box also takes the remainder up to the frame end (its own reciprocal bk_last). */
static uint32_t box_start, box_end, bw, bk, bk_last, last_w, acc, err_fb;
static unsigned bi, bn;
static int8_t *bout; static uint32_t *bavg;

static inline void emit_box(void) {
    if (bavg) bavg[bi] = acc;
    uint32_t t = acc + err_fb;
    uint32_t q = t >> 23;
    if (q > 255) { q = 255; err_fb = 0; } else err_fb = t & 0x7FFFFF;
    bout[bi] = (int8_t)((int)q - 128);
    acc = 0;
}
static inline void next_box(void) {                          /* after emitting box bi: advance to the next one */
    bi++; box_start = box_end;
    if (bi == bn - 1) { box_end = (uint32_t)SAMPLES_PER_FRAME << 16; bk = bk_last; } else box_end += bw;
}
/* m consecutive boxes of constant amplitude a (a full box: acc = a * 65536).  The quantiser with error feedback is a phase accumulator: with
 * av = a*65536 = c*2^23 + f the output is c or c+1, the +1 being the carry of a 32-bit accumulator that adds f*2^9 per box. */
static void fill_boxes(uint32_t a, unsigned m) {
    int8_t *o = bout + bi;
    if (bavg) for (unsigned j = 0; j < m; j++) bavg[bi + j] = a << 16;
    if (a == 0) { while (m--) *o++ = (int8_t)-128; return; }     /* silence: the error term stays below one step, q = 0 */
    uint32_t av = a << 16, c = av >> 23, f32 = (av & 0x7FFFFF) << 9, e32 = err_fb << 9;
    if (c >= 255) {                                           /* full scale: needs the clamp */
        uint32_t e = err_fb, f = av & 0x7FFFFF;
        while (m--) { uint32_t q = c + ((e + f) >> 23); e = (e + f) & 0x7FFFFF; if (q > 255) { q = 255; e = 0; } *o++ = (int8_t)((int)q - 128); }
        err_fb = e; return;
    }
    {
        int8_t base = (int8_t)((int)c - 128), up = (int8_t)(base + 1);
        while (m--) { uint32_t n = e32 + f32; *o++ = n < e32 ? up : base; e32 = n; }
    }
    err_fb = e32 >> 9;
}

/* amplitude `a` over [p0, p1) (16.16 sample positions within the frame) */
static void integrate(uint32_t p0, uint32_t p1, uint32_t a) {
    if (p1 <= box_end) { if (p1 > p0) acc += a * (uint32_t)(((uint64_t)(p1 - p0) * bk) >> 24); return; }     /* inside the current box (the usual case) */
    /* the segment crosses the end of the current box (so this is not the last box) */
    if (p0 == box_start) acc = a << 16; else acc += a * (uint32_t)(((uint64_t)(box_end - p0) * bk) >> 24);
    emit_box(); next_box();
    unsigned room = bn - 1 - bi;                              /* boxes that may still be completed before the last one */
    if (room && p1 >= box_start + bw) {
        unsigned m = (p1 - box_start) / bw; if (m > room) m = room;
        fill_boxes(a, m);
        bi += m; box_start += m * bw;
        if (bi == bn - 1) { box_end = (uint32_t)SAMPLES_PER_FRAME << 16; bk = bk_last; } else box_end = box_start + bw;
    }
    if (p1 > box_start) acc += a * (uint32_t)(((uint64_t)(p1 - box_start) * bk) >> 24);
}

/* model sample (within the frame) at which a write at cycle `cy` takes effect: ceil((cy - c0) / 2); NEVER if that is in a later frame */
static inline uint32_t wlocal(uint64_t cy, uint64_t c0, uint64_t c1) {
    if (cy >= c1 - 1) return (uint32_t)NEVER;
    if (cy < c0) return 0;                                  /* the one cycle before the frame start rounds up to its first sample */
    return (uint32_t)(((uint32_t)(cy - c0) + 1) >> 1);
}

void sns_render(uint32_t frame, int n, int8_t *out, uint32_t *avg16) {
    uint64_t c0 = (uint64_t)frame * SNS_FRAME_CYCLES, c1 = c0 + SNS_FRAME_CYCLES;
    bn = (unsigned)n; bout = out; bavg = avg16; bi = 0; acc = 0;
    bw = ((uint32_t)SAMPLES_PER_FRAME << 16) / (uint32_t)n;
    last_w = ((uint32_t)SAMPLES_PER_FRAME << 16) - (bn - 1) * bw;
    {   /* k = floor(2^40 / w) without 64-bit division: fraction of a box per 16.16 unit, scaled by 2^24: (ov * k) >> 24 -> 0..65536 */
        uint32_t q32 = 0xFFFFFFFFu / bw, r = 0xFFFFFFFFu - q32 * bw + 1;
        if (r >= bw) { q32++; r -= bw; }
        bk = (q32 << 8) + ((r << 8) / bw);
        q32 = 0xFFFFFFFFu / last_w; r = 0xFFFFFFFFu - q32 * last_w + 1;
        if (r >= last_w) { q32++; r -= last_w; }
        bk_last = (q32 << 8) + ((r << 8) / last_w);
    }
    box_start = 0; box_end = bw; if (bn == 1) { box_end = (uint32_t)SAMPLES_PER_FRAME << 16; bk = bk_last; }
    uint32_t tpos = 0;                                       /* current position, 768k samples within the frame */
    recalc_amp();
    for (;;) {
        uint32_t tw = (uint32_t)NEVER;
        if (wq_head != wq_tail) tw = wlocal(wq_cyc[wq_head], c0, c1);
        uint32_t m = nt[0];
        if (nt[1] < m) m = nt[1];
        if (nt[2] < m) m = nt[2];
        if (nt[3] < m) m = nt[3];
        uint32_t te = tw < m ? tw : m;
        if (te >= SAMPLES_PER_FRAME) te = SAMPLES_PER_FRAME;
        integrate(tpos << 16, te << 16, amp);
        tpos = te;
        if (te >= SAMPLES_PER_FRAME) break;
        if (tw == te) {                                      /* writes first */
            while (wq_head != wq_tail && wlocal(wq_cyc[wq_head], c0, c1) == te) {
                apply_write(wq_dat[wq_head], (int32_t)(te >> 3));      /* ticks already processed: those with 7+8n < s */
                wq_head = (wq_head + 1) & (WQ - 1);
            }
        }
        for (int i = 0; i < 4; i++) {                        /* toggles at this time (a write may have changed the schedule, so look at every channel) */
            if (nt[i] != te) continue;
            chan_t *h = &ch[i];
            sns_stat_events++;
            if (i == 3) { rng = lfsr_step1(rng, (reg[6] >> 2) & 1); h->out = (uint8_t)(rng & 1); }
            else h->out ^= 1;
            h->cnt = h->per; h->tref = h->nxt + 1; h->nxt = h->tref + imax1(h->per) - 1;
            nt[i] = 7u + 8u * (uint32_t)h->nxt;
        }
        recalc_amp();
    }
    while (bi < bn) { emit_box(); bi++; }                    /* the last box (box_end is the frame end) */
    /* lazy channels: bring them up to date now and then so that their tick counters cannot overflow */
    if ((frame & 255) == 255) for (int i = 0; i < 4; i++) if (!ch[i].aud) catchup(i, TICKS_PER_FRAME);
    /* shift the tick time base to the next frame */
    for (int i = 0; i < 4; i++) {
        chan_t *h = &ch[i];
        h->tref -= TICKS_PER_FRAME;
        if (h->aud) { h->nxt -= TICKS_PER_FRAME; sched(i); }
    }
}
