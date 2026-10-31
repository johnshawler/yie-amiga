/* SN76489A reference model: transcription of MAME 0.264 sn76496_base_device (SN76489A: LFSR 0x10000, taps 0x04/0x08, divider 8, "sega" flag set => reg 0 -> period 0x400),
 * run at the stream rate clock/2 = 768 kHz.  Result: 0 mismatches over 14.6 M samples of MAME audio (1500 frames of scripted play, outside VLM5030 spans).
 * Observed: MAME output = sum(vol) - 1 (when > 0) delayed by 1 sample; write at CPU cycle c takes effect for stream sample c/2; chip tick phase: first tick at sample 7.
 * usage: snref io.log wav [dt] [phase] [shift] */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../runtime/vlm_dur.h"
static double voltab[16];
static int reg[8], last_reg, period[4], count[4], output[4], cur_clock; static uint32_t rng; static int vol[4];
static void init(void) {
    double out = 32767 / 4; /* int division: 8191 */ out = 8191; for (int i = 0; i < 15; i++) { voltab[i] = (int)out; out /= 1.258925412; } voltab[15] = 0;
    for (int i = 0; i < 4; i++) { output[i] = 0; count[i] = 0; period[i] = 0; }
    for (int i = 0; i < 8; i += 2) { reg[i] = 0; reg[i + 1] = 0; }
    rng = 0x10000; output[3] = rng & 1; cur_clock = 7; last_reg = 3; for (int i = 0; i < 4; i++) vol[i] = 0;
}
static void wr(int data) {
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
        vol[c] = (int)voltab[data & 15]; if (!(data & 0x80)) reg[r] = (reg[r] & 0x3f0) | (data & 15); break;
    case 6: {
        if (!(data & 0x80)) reg[r] = (reg[r] & 0x3f0) | (data & 15);
        int n = reg[6]; period[3] = ((n & 3) == 3) ? period[2] << 1 : 1 << (5 + (n & 3)); rng = 0x10000; } break;
    }
}
static int16_t step(void) {
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
static int SH = 0;
int main(int argc, char **argv) {
    int dt = argc > 3 ? atoi(argv[3]) : 0; init(); if (argc > 5) SH = atoi(argv[5]); if (argc > 4) cur_clock = atoi(argv[4]);
    FILE *f = fopen(argv[2], "rb"); fseek(f, 44, SEEK_SET);
    static int16_t *w; fseek(f, 0, SEEK_END); long n = (ftell(f) - 44) / 4; fseek(f, 44, SEEK_SET);
    w = malloc(n * 4); fread(w, 4, n, f);
    FILE *io = fopen(argv[1], "r"); char line[128]; static long wc[400000]; static int wd[400000]; int nw = 0, latch = 0;
    while (fgets(line, sizeof line, io)) { char k; long long cy; unsigned a, v; if (sscanf(line, "%c %lld %x %x", &k, &cy, &a, &v) == 4 && k == 'W') { if (a == 0x4800) latch = v; else if (a == 0x4900) { wc[nw] = cy; wd[nw++] = latch; } } }
    static long vs[4000], ve[4000]; int nv = 0; { rewind(io); int ctl = 0, dat = 0;
      while (fgets(line, sizeof line, io)) { char k; long long cy; unsigned a, v; if (sscanf(line, "%c %lld %x %x", &k, &cy, &a, &v) == 4 && k == 'W') {
        if (a == 0x4b00) dat = v; else if (a == 0x4a00) { if ((v & 4)) { if (nv && ve[nv-1] > cy / 2) ve[nv-1] = cy / 2 + 4000; } else if ((v & 2) && !(ctl & 2)) { vs[nv] = cy / 2; ve[nv] = cy / 2 + (5 + vlm_dur[(dat >> 1) & 127]) / 2 + 400000; nv++; } ctl = v; } } } }
    long bad = 0, nskip = 0, vi = 0, first = -1; int wi = 0; double sumerr = 0;
    for (long t = 0; t < n - 4; t++) {
        while (wi < nw && wc[wi] + dt <= 2 * t) wr(wd[wi++]);
        int16_t s = step(); int16_t m = w[2 * (t + SH)]; if (m > 0) m++; if (getenv("DBG") && t >= atol(getenv("DBG")) - 40 && t < atol(getenv("DBG")) + 40) printf("%ld model=%d mame=%d cnt %d %d %d %d per %d %d %d out %d%d%d%d vol %d %d %d %d\n", t, s, m, count[0],count[1],count[2],count[3],period[0],period[1],period[2],output[0],output[1],output[2],output[3],vol[0],vol[1],vol[2],vol[3]);
        while (vi < nv && ve[vi] < t) vi++; int inv = vi < nv && vs[vi] <= t; if (inv) nskip++; if (!inv && t > 2000 && s != m) {  bad++; if (first < 0) first = t; if (bad < 8 || (t>8536270 && t<8536300)) printf("t=%ld model=%d mame=%d\n", t, s, m); }
    }
    printf("vlm-skipped=%ld ", nskip); printf("dt=%d samples=%ld writes=%d mismatches=%ld first=%ld\n", dt, n, nw, bad, first);
    return 0;
}
