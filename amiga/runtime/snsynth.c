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

typedef struct { int32_t cnt, per, tref, nxt; uint32_t out, aud, pad[2]; } chan_t;       /* 32 bytes: indexed by shifts in the assembly core */
static chan_t ch[4];
static uint16_t reg[8];
static uint8_t last_reg;
/* The state shared with the assembly core (snsynth_m68k.inc); offsets are used there. */
struct core {
    uint32_t A, acc, P0, bi;                                /*  0: amplitude, accumulator of the current box, position (box units, 22 fractional bits), current box */
    uint32_t bn, tw, K;                                     /* 16: boxes in the frame, time of the next queued write (model samples), box units per model sample */
    int8_t *out; uint32_t Aor;                              /* 28: output block, OR of the amplitudes seen (0 = idle block) */
    uint32_t rng, white;                                    /* 36: noise LFSR (17 bits), 1 = white noise mode (bit 2 of register 6) */
    uint32_t nt[4];                                         /* 44: time (model sample within the frame) of the next toggle of an audible channel, NEVER otherwise */
    uint32_t vol[4];                                        /* 60: amplitude of a channel whose output is high */
    chan_t *ch; const uint32_t *dit; const uint32_t *pat;   /* 76 */
};
static struct core R;
#define nt R.nt
#define vol R.vol
#define rng R.rng
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

static void quant_tables(void);
void sns_init(void) {
    static int tables_done;
    if (!tables_done) { lfsr_tables(); quant_tables(); tables_done = 1; }
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
    if (h->aud) { h->cnt -= k; h->tref = T; return; }        /* audible: every toggle before T has been processed, so none lies in (tref, T) and nxt does not change */
    int32_t a = imax1(h->cnt);
    if (k < a) { h->cnt -= k; }
    else {
        int32_t q = imax1(h->per), rest = k - a;
        uint32_t ntog = 1 + (uint32_t)rest / (uint32_t)q;
        int32_t rem = rest % q;
        if (c == 3) {
            rng = lfsr_jump(rng, (reg[6] >> 2) & 1, ntog);
            h->out = (uint8_t)(rng & 1);
        } else if (ntog & 1) h->out ^= 1;
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
}

/* ---- box integration ----
 * Box i covers [i, i+1) in box units (n boxes per frame of 12672 model samples).
 * Quantiser: q = (acc + DIT[i & 15]) >> 23 (clamped to 255), acc = box average * 2^23 / 256 (0..2^31): a stateless 16-phase ordered dither, DIT[j] = (2*D[j]+1) * 2^18,
 * D = bit reversal of j (so the thresholds are (D+0.5)/16 of a step: mean error within 1/32 step).  For a run of boxes of constant amplitude the output is
 * c or c+1 with c = acc >> 23, the +1 where F5 = (acc >> 18) & 31 >= 31 - 2*D[j]: a 32 x 16 table of 0/1 bytes, applied four boxes at a time with long writes. */
static uint32_t DIT[16];
static uint32_t pat[32][4];                                  /* carry patterns as long words (bytes 0/1, memory order) */
static uint8_t pat2[32][32];                                /* the same as bytes, each pattern twice (the assembly core copies chunks of up to 16) */

static void quant_tables(void) {
    static const uint8_t D[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
    uint8_t t[32][16];
    for (int j = 0; j < 16; j++) DIT[j] = (uint32_t)(2 * D[j] + 1) << 18;
    for (int f = 0; f < 32; f++) for (int j = 0; j < 16; j++) t[f][j] = (uint8_t)(f >= 31 - 2 * D[j]);
    for (int f = 0; f < 32; f++) for (int j = 0; j < 32; j++) pat2[f][j] = t[f][j & 15];
    for (int f = 0; f < 32; f++) for (int w = 0; w < 4; w++) { uint32_t v; for (int k = 0; k < 4; k++) ((uint8_t *)&v)[k] = t[f][w * 4 + k]; pat[f][w] = v; }
}

#define BAVG 0   /* the debug average output (avg16) is only produced by the C-visible slow path below */
/* m consecutive boxes (m >= 1) starting at box `bi` of constant amplitude a (a full box: acc = a * 65536); returns nothing, the caller advances bi */
static void fill_boxes(int8_t *out, unsigned bi, uint32_t a, unsigned m) {
    int8_t *o = out + bi;
    if (a == 0) {                                             /* silence: q = 0 for every phase */
        while (m && ((uintptr_t)o & 3)) { *o++ = (int8_t)-128; m--; }
        { uint32_t *l = (uint32_t *)o; while (m >= 4) { *l++ = 0x80808080u; m -= 4; } o = (int8_t *)l; }
        while (m--) *o++ = (int8_t)-128;
        return;
    }
    uint32_t av = a << 16, c = av >> 23, F = (av >> 18) & 31;
    unsigned ph = bi & 15;
    if (c >= 255) { while (m--) *o++ = (int8_t)127; return; }  /* full scale: q = 255 or 256, both clamp to 255 */
    const uint8_t *tp = (const uint8_t *)pat[F];
    int8_t base = (int8_t)((int)c - 128);
    if (m < 12) { while (m--) { *o++ = (int8_t)(base + tp[ph]); ph = (ph + 1) & 15; } return; }  /* short run: byte loop */
    while (ph & 3) { *o++ = (int8_t)(base + tp[ph]); ph = (ph + 1) & 15; m--; }          /* up to a long-word boundary of the pattern */
    {
        uint32_t cl = c * 0x01010101u, *l = (uint32_t *)o; unsigned w = ph >> 2; const uint32_t *pp = pat[F];
        /* (o is 4-aligned here because out is, and ph = bi & 15 tracks the byte index) */
        do { *l++ = (pp[w] + cl) ^ 0x80808080u; w = (w + 1) & 3; m -= 4; } while (m >= 4);
        o = (int8_t *)l; ph = w << 2;
    }
    while (m--) { *o++ = (int8_t)(base + tp[ph]); ph = (ph + 1) & 15; }
}

/* model sample (within the frame) at which a write at cycle `cy` takes effect: ceil((cy - c0) / 2); NEVER if that is in a later frame */
static inline uint32_t wlocal(uint64_t cy, uint64_t c0, uint64_t c1) {
    if (cy >= c1 - 1) return (uint32_t)NEVER;
    if (cy < c0) return 0;                                  /* the one cycle before the frame start rounds up to its first sample */
    return (uint32_t)(((uint32_t)(cy - c0) + 1) >> 1);
}

#if defined(__mc68000__) && !defined(SNS_C_ONLY)
#define SNS_ASM 1
extern uint32_t sns_core(struct core *r);                   /* snsynth_m68k.inc: the event loop below in 68020 assembly (no debug average output) */
#endif
int sns_force_c;                                            /* test hook: use the C loop even where the assembly core exists */

int sns_render(uint32_t frame, int n, int8_t *out, uint32_t *avg16) {
    uint64_t c0 = (uint64_t)frame * SNS_FRAME_CYCLES, c1 = c0 + SNS_FRAME_CYCLES;
    const unsigned bn = (unsigned)n;
    const uint32_t K = ((uint32_t)n << 22) / SAMPLES_PER_FRAME;                    /* box units (22 fractional bits) per model sample */
    recalc_amp();
    uint32_t tw = (wq_head != wq_tail) ? wlocal(wq_cyc[wq_head], c0, c1) : (uint32_t)NEVER;     /* time of the next queued write (cached) */
#ifdef SNS_ASM
    if (!avg16 && !sns_force_c) {
        R.A = amp; R.acc = 0; R.P0 = 0; R.bi = 0; R.bn = bn; R.K = K; R.out = out; R.Aor = 0; R.tw = tw;
        R.ch = ch; R.dit = DIT; R.pat = (const uint32_t *)&pat2[0][0];
        for (;;) {
            R.white = (reg[6] >> 2) & 1;
            if (sns_core(&R)) break;
            uint32_t te = R.tw;                              /* a write is due at model sample te (the core has integrated up to it) */
            do {
                apply_write(wq_dat[wq_head], (int32_t)(te >> 3));
                wq_head = (wq_head + 1) & (WQ - 1);
                tw = (wq_head != wq_tail) ? wlocal(wq_cyc[wq_head], c0, c1) : (uint32_t)NEVER;
            } while (tw == te);
            recalc_amp(); R.A = amp; R.tw = tw;
        }
        amp = R.A;
        goto finished;
    }
#endif
    /* Positions are in box units with 22 fractional bits: P = te * K (the frame end is exactly n * 2^22); box i covers [i, i+1).  A segment of constant amplitude a from P0 to P1
     * adds a * (P1 - P0) (16.16 box fractions) to the accumulator of the box; boxes it covers entirely are filled in one go.  Quantiser: see above. */
    uint32_t acc = 0, P0 = 0, Aor = 0;                       /* accumulator of the current box, current position, OR of all amplitudes (0 = idle block) */
    unsigned bi = 0;                                         /* current box */
    uint32_t A = amp;
    for (;;) {
        unsigned ci = 0; uint32_t m = nt[0];                 /* next toggle: time and channel */
        if (nt[1] < m) { m = nt[1]; ci = 1; }
        if (nt[2] < m) { m = nt[2]; ci = 2; }
        if (nt[3] < m) { m = nt[3]; ci = 3; }
        uint32_t te = tw < m ? tw : m;
        uint32_t P1 = te >= SAMPLES_PER_FRAME ? ((uint32_t)bn << 22) : te * K;
        Aor |= A;
        {   /* integrate: amplitude A over [P0, P1) */
            unsigned i1 = P1 >> 22;
            if (i1 == bi) acc += A * ((P1 - P0) >> 6);                                      /* inside the current box (the usual case) */
            else {
                acc += A * ((((bi + 1) << 22) - P0) >> 6);                                  /* finish the current box */
                { uint32_t q = (acc + DIT[bi & 15]) >> 23; if (q > 255) q = 255; out[bi] = (int8_t)((int)q - 128); if (avg16) avg16[bi] = acc; }
                bi++;
                if (i1 > bi) { unsigned mm = i1 - bi; if (avg16) for (unsigned j = 0; j < mm; j++) avg16[bi + j] = A << 16; fill_boxes(out, bi, A, mm); bi += mm; }     /* whole boxes */
                acc = A * ((P1 & 0x3FFFFF) >> 6);                                           /* start of the box in which the segment ends */
            }
            P0 = P1;
        }
        if (te >= SAMPLES_PER_FRAME) break;
        if (tw == te) {                                      /* writes first */
            do {
                apply_write(wq_dat[wq_head], (int32_t)(te >> 3));      /* ticks already processed: those with 7+8n < s */
                wq_head = (wq_head + 1) & (WQ - 1);
                tw = (wq_head != wq_tail) ? wlocal(wq_cyc[wq_head], c0, c1) : (uint32_t)NEVER;
            } while (tw == te);
            recalc_amp(); A = amp;
            ci = 0; m = nt[0];                               /* a write may have changed the schedule */
            if (nt[1] < m) { m = nt[1]; ci = 1; }
            if (nt[2] < m) { m = nt[2]; ci = 2; }
            if (nt[3] < m) { m = nt[3]; ci = 3; }
        }
        while (m == te) {                                    /* toggles at this time (several channels may coincide) */
            chan_t *h = &ch[ci];
            sns_stat_events++;
            uint8_t o = h->out;
            if (ci == 3) { rng = lfsr_step1(rng, (reg[6] >> 2) & 1); h->out = (uint8_t)(rng & 1); }
            else h->out = o ^ 1;
            if (h->out != o) { if (h->out) A += vol[ci]; else A -= vol[ci]; }
            h->cnt = h->per; h->tref = h->nxt + 1; h->nxt = h->tref + imax1(h->per) - 1;
            nt[ci] = 7u + 8u * (uint32_t)h->nxt;
            ci = 0; m = nt[0];
            if (nt[1] < m) { m = nt[1]; ci = 1; }
            if (nt[2] < m) { m = nt[2]; ci = 2; }
            if (nt[3] < m) { m = nt[3]; ci = 3; }
        }
        amp = A;
    }
    /* the last box (the segment up to the frame end was integrated above and ended exactly at box n) */
    amp = A;
#ifdef SNS_ASM
    R.Aor = Aor;
finished:
#else
    R.Aor = Aor;
#endif
    /* lazy channels: bring them up to date now and then so that their tick counters cannot overflow */
    if ((frame & 255) == 255) for (int i = 0; i < 4; i++) if (!ch[i].aud) catchup(i, TICKS_PER_FRAME);
    /* shift the tick time base to the next frame */
    for (int i = 0; i < 4; i++) {
        chan_t *h = &ch[i];
        h->tref -= TICKS_PER_FRAME;
        if (h->aud) { h->nxt -= TICKS_PER_FRAME; sched(i); }
    }
    return R.Aor == 0;
}
