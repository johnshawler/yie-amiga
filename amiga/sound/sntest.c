/* Checks runtime/snsynth.c (event-driven, lazy, box-integrated) against the per-sample reference model of snref.c (itself 0-mismatch vs MAME):
 * for every frame of a yref run the box averages of the reference stream must equal the synthesiser's un-quantised averages.
 * usage: sntest yref.io frames [n_samples_per_frame (even)]  */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "../runtime/snsynth.h"

/* ---- reference: transcription of MAME 0.264 sn76496 (SN76489A) at 768 kHz, as in snref.c ---- */
static int reg[8], last_reg, period[4], count[4], output[4], cur_clock; static uint32_t rng; static int vol[4];
static const int voltab[16] = { 8191, 6506, 5168, 4105, 3260, 2589, 2057, 1634, 1298, 1031, 819, 650, 516, 410, 326, 0 };
static void rinit(void) {
    for (int i = 0; i < 4; i++) { output[i] = 0; count[i] = 0; period[i] = 0; vol[i] = 0; }
    for (int i = 0; i < 8; i++) reg[i] = 0;
    rng = 0x10000; output[3] = rng & 1; cur_clock = 7; last_reg = 3;
}
static void rwr(int data) {
    int r;
    if (data & 0x80) { r = (data & 0x70) >> 4; last_reg = r; reg[r] = (reg[r] & 0x3f0) | (data & 0x0f); } else r = last_reg;
    int c = r >> 1;
    switch (r) {
    case 0: case 2: case 4:
        if (!(data & 0x80)) reg[r] = (reg[r] & 0x0f) | ((data & 0x3f) << 4);
        period[c] = reg[r] ? reg[r] : 0x400;
        if (r == 4 && (reg[6] & 3) == 3) period[3] = period[2] << 1;
        break;
    case 1: case 3: case 5: case 7:
        vol[c] = voltab[data & 15]; if (!(data & 0x80)) reg[r] = (reg[r] & 0x3f0) | (data & 15); break;
    case 6: {
        if (!(data & 0x80)) reg[r] = (reg[r] & 0x3f0) | (data & 15);
        int n = reg[6]; period[3] = ((n & 3) == 3) ? period[2] << 1 : 1 << (5 + (n & 3)); rng = 0x10000; } break;
    }
}
static int rstep(void) {
    if (cur_clock > 0) cur_clock--;
    else {
        cur_clock = 7;
        for (int i = 0; i < 3; i++) { if (--count[i] <= 0) { output[i] ^= 1; count[i] = period[i]; } }
        if (--count[3] <= 0) {
            int fb = ((rng & 4) != 0) != (((rng & 8) != 0) && ((reg[6] >> 2) & 1));
            rng >>= 1; if (fb) rng |= 0x10000;
            output[3] = rng & 1; count[3] = period[3];
        }
    }
    int o = 0; for (int i = 0; i < 4; i++) if (output[i]) o += vol[i];
    return o;
}

int main(int argc, char **argv) {
    int nframes = atoi(argv[2]); int N = argc > 3 ? atoi(argv[3]) : 472;
    static long wc[600000]; static int wd[600000]; int nw = 0, latch = 0;
    FILE *io = strcmp(argv[1], "rand") ? fopen(argv[1], "r") : NULL; char line[128];
    if (!io) {   /* random write stream stressing every register / mode (seed = argv[4]) */
        srand(argc > 4 ? atoi(argv[4]) : 1); long cy = 100;
        while (cy < (long)nframes * 25344 - 100 && nw < 590000) {
            int r = rand() % 100, b;
            if (r < 30) b = 0x80 | ((rand() % 3) * 2 << 4) | (rand() & 15);               /* tone period low */
            else if (r < 50) b = rand() & 0x3F;                                                  /* data byte (period high / volume / noise by latch) */
            else if (r < 75) b = 0x80 | ((rand() % 4) * 2 + 1) << 4 | (rand() % 6);              /* volume, mostly audible */
            else if (r < 90) b = 0xE0 | (rand() & 7);                                            /* noise control */
            else b = rand() & 0xFF;
            wc[nw] = cy; wd[nw++] = b; cy += 5 + rand() % (rand() % 4 == 0 ? 20000 : 600);
        }
    } else while (fgets(line, sizeof line, io)) { char k; long long cy; unsigned a, v; if (sscanf(line, "%c %lld %x %x", &k, &cy, &a, &v) == 4 && k == 'W') { if (a == 0x4800) latch = v; else if (a == 0x4900) { wc[nw] = cy; wd[nw++] = latch; } } }
    sns_init(); rinit();
    sns_init();
    int wi = 0, fed = 0; double maxd = 0, sumd = 0; long cnt = 0; long maxev = 0; int badq = 0, qbad = 0;
    static int16_t samp[12672]; static uint32_t avg[600]; static int8_t out[600] __attribute__((aligned(4)));
    static uint32_t s_all;
    for (int f = 0; f < nframes; f++) {
        long c1 = (long)(f + 1) * 25344;
        /* feed writes that happen before the end of the frame (+1 to catch the rounding cases) */
        while (fed < nw && wc[fed] < c1 + 2) { sns_write((uint64_t)wc[fed], (uint8_t)wd[fed]); fed++; }
        for (int t = 0; t < 12672; t++) {
            long s = (long)f * 12672 + t;
            while (wi < nw && (wc[wi] + 1) / 2 <= s) rwr(wd[wi++]);
            samp[t] = (int16_t)rstep();
        }
        sns_render((uint32_t)f, N, out, avg);
        for (int i = 0; i < N; i++) {                      /* the quantiser: first-order error feedback of the box average */
            static const int D[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
            uint32_t q = (avg[i] + ((uint32_t)(2 * D[i & 15] + 1) << 18)) >> 23;
            if (q > 255) q = 255;
            if ((int8_t)((int)q - 128) != out[i]) { if (qbad < 5) printf("quantiser mismatch frame %d box %d: %d vs %d\n", f, i, (int)q - 128, out[i]); qbad++; }
        }
        for (int i = 0; i < N; i++) {
            double a0 = (double)i * 12672.0 / N, a1 = (double)(i + 1) * 12672.0 / N, sum = 0;
            for (int t = (int)a0; t < 12672 && t < a1; t++) { double lo = t > a0 ? t : a0, hi = t + 1 < a1 ? t + 1 : a1; if (hi > lo) sum += samp[t] * (hi - lo); }
            double ref = sum / (a1 - a0), got = avg[i] / 65536.0, d = fabs(ref - got);
            if (d > maxd) maxd = d; sumd += d; cnt++;
            if (d > 16.0 && badq < 10) { printf("frame %d box %d ref %.2f got %.2f\n", f, i, ref, got); badq++; }
        }
    }
    printf("frames=%d n=%d writes=%d  max |ref-synth| = %.3f (15-bit units, 1 LSB of the 8-bit output = 128 units)  mean = %.4f  toggle events=%u\n", nframes, N, nw, maxd, sumd / cnt, sns_stat_events);
    printf("quantiser mismatches: %d\n", qbad);
    (void)s_all; (void)maxev;
    return maxd > 16.0 || qbad;
}
