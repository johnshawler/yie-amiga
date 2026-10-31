/* Reference emulator of the Konami "Yie Ar Kung-Fu" board (GX407): one MC6809E at 1.536 MHz, RAM $5000-$5FFF, ROM $8000-$FFFF.
 * Memory map, I/O, interrupts and timing from MAME src/mame/konami/yiear.cpp (pinned snapshot in yie/sources/mame).
 *
 * usage: yref [-n frames] [-i inputs.txt] [-t pinned|mame0264] [-o prefix] [-r romdir] [-S] [-C covfile]
 * Output (same layout as ref/trace/mame_trace.lua): prefix.bin  per frame: u32 frame, u64 cycles, u16 pc x y u s, u8 a b dp cc, 4096 bytes RAM $5000-$5FFF
 *                                                    prefix.io   "W cycle addr data" / "R cycle addr data" for $4000-$4FFF writes, $4C00-$4E03 and $0000 reads
 * -S : sound-only mode, write also the stream of sound events (prefix.snd) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "m6809.h"
#include "vlm_dur.h"

static uint8_t rom[0x8000];
static uint8_t ram[0x1000];
static m6809_t cpu;
static int nmi_enable, irq_enable;
static uint8_t in_sys = 0xff, in_p1 = 0xff, in_p2 = 0xff;
static uint8_t dsw1 = 0xff, dsw2 = 0x5b, dsw3 = 0xff;
static FILE *fio, *fbin;
static int vlm_busy;                /* modelled in speech.c later; here: see vlm_* below */
static uint64_t cov[256];

/* ------ VLM5030 busy model (what the program can observe) ------ */
static uint8_t vlm_ctl, vlm_data;
static double vlm_busy_until = -1;      /* in cpu cycles */
static int vlm_busy_now(void) { return (double)cpu.cycles < vlm_busy_until; }

static int trace_until = 0;
static uint8_t cov_x[65536];   /* instruction start executed */
static uint32_t cov_r[65536];  /* data reads (non-fetch) per address */
static uint32_t cov_w[65536];  /* data writes */
static const char *cov_out = NULL;
static uint8_t rd(void *ctx, uint16_t a) {
    (void)ctx;
    if (!cpu.in_fetch) cov_r[a]++;
    if (a >= 0x8000) { if ((a == 0xfff8 || a == 0xfffc) && fio) fprintf(fio, "V %llu %04x %02x\n", (unsigned long long)cpu.cycles, a, rom[a - 0x8000]); return rom[a - 0x8000]; }
    if (a >= 0x5000 && a < 0x6000) return ram[a - 0x5000];
    uint8_t v = 0;
    if (cpu.in_fetch) return 0;
    switch (a) {
    case 0x0000: v = vlm_busy_now(); break;
    case 0x4c00: v = dsw2; break;
    case 0x4d00: v = dsw3; break;
    case 0x4e00: v = in_sys; break;
    case 0x4e01: v = in_p1; break;
    case 0x4e02: v = in_p2; break;
    case 0x4e03: v = dsw1; break;
    default: return 0;
    }
    if (fio) fprintf(fio, "R %llu %04x %02x\n", (unsigned long long)cpu.cycles, a, v);
    return v;
}

static void wr(void *ctx, uint16_t a, uint8_t v) {
    (void)ctx;
    cov_w[a]++;
    if (a >= 0x5000 && a < 0x6000) { ram[a - 0x5000] = v; return; }
    if (a >= 0x4000 && a < 0x5000) {
        if (fio) fprintf(fio, "W %llu %04x %02x\n", (unsigned long long)cpu.cycles, a, v);
        switch (a) {
        case 0x4000:
            if (nmi_enable && !(v & 2)) cpu.nmi = 0;
            nmi_enable = (v >> 1) & 1;
            if (irq_enable && !(v & 4)) cpu.irq = 0;
            irq_enable = (v >> 2) & 1;
            break;
        case 0x4a00:
            /* ST rising edge (bit 1) with RST (bit 2) low starts the phrase latched in $4B00; RST clears BSY */
            if ((v & 4)) vlm_busy_until = -1;
            else if ((v & 2) && !(vlm_ctl & 2)) vlm_busy_until = (double)cpu.cycles + 5 + vlm_dur[(vlm_data >> 1) & 127];
            vlm_ctl = v; break;
        case 0x4b00: vlm_data = v; break;
        default: break;
        }
    }
}

static uint8_t cjt[65536];   /* computed jump/call: bitmap of (target) with source in cjs */
static uint16_t cjs[65536];
static void tstep(void) {
    uint16_t pc0 = cpu.pc; uint8_t op0 = (pc0 >= 0x8000) ? rom[pc0 - 0x8000] : 0;
    cov_x[pc0] = 1;
    if (trace_until && cpu.cycles < (uint64_t)trace_until && pc0 >= 0x8000)
        fprintf(stderr, "%llu pc=%04x op=%02x %02x %02x a=%02x b=%02x x=%04x s=%04x cc=%02x\n", (unsigned long long)cpu.cycles, pc0, rom[pc0-0x8000], rom[pc0-0x8000+1], rom[pc0-0x8000+2], cpu.a, cpu.b, cpu.x, cpu.s, cpu.cc);
    m6809_step(&cpu);
    if ((op0 == 0x6E || op0 == 0xAD) && cpu.ppc == pc0) { cjt[cpu.pc] = 1; cjs[cpu.pc] = pc0; }
}
static int nmi_dly = 0, irq_dly = 0, reset_cyc = 4;
static void set_nmi_line(int level) {
    if (level && !cpu.nmi) { cpu.nmi_pending = 1; cpu.nmi_delay = nmi_dly; }      /* edge */
    cpu.nmi = level;
}

/* ------ timing ------ */
typedef struct { double frame; int lines; double line; int vb; } timing_t;

int main(int argc, char **argv) {
    int nframes = 600; const char *inp = NULL, *prefix = "yref", *romdir = "roms", *timing = "pinned";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) nframes = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i")) inp = argv[++i];
        else if (!strcmp(argv[i], "-o")) prefix = argv[++i];
        else if (!strcmp(argv[i], "-r")) romdir = argv[++i];
        else if (!strcmp(argv[i], "-t")) timing = argv[++i];
        else if (!strcmp(argv[i], "-d")) { nmi_dly = atoi(argv[++i]); irq_dly = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-R")) reset_cyc = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-C")) cov_out = argv[++i];
        else if (!strcmp(argv[i], "-T")) trace_until = atoi(argv[++i]);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/407_i08.10d", romdir); FILE *f = fopen(path, "rb"); if (!f || fread(rom, 1, 0x4000, f) != 0x4000) { perror(path); return 1; } fclose(f);
    snprintf(path, sizeof path, "%s/407_i07.8d", romdir); f = fopen(path, "rb"); if (!f || fread(rom + 0x4000, 1, 0x4000, f) != 0x4000) { perror(path); return 1; } fclose(f);

    timing_t T;
    if (!strcmp(timing, "mame0264")) { T.frame = 1536000.0 / 60.58; T.lines = 256; }
    else { T.frame = 25344.0; T.lines = 264; }          /* 18.432/3 MHz pixel clock, 384 x 264 */
    T.line = T.frame / T.lines; T.vb = 240;

    /* input script */
    static struct { int f; uint8_t s, a, b; } ev[100000]; int nev = 0;
    if (inp) { FILE *fi = fopen(inp, "r"); if (!fi) { perror(inp); return 1; } unsigned fr, s, a, b; while (fscanf(fi, "%u %x %x %x", &fr, &s, &a, &b) == 4) { ev[nev].f = fr; ev[nev].s = s; ev[nev].a = a; ev[nev].b = b; nev++; } fclose(fi); }
    int evi = 0;

    snprintf(path, sizeof path, "%s.bin", prefix); fbin = fopen(path, "wb");
    snprintf(path, sizeof path, "%s.io", prefix); fio = fopen(path, "w");

    cpu.rd = rd; cpu.wr = wr; cpu.ctx = NULL;
    m6809_reset(&cpu); cpu.cycles = reset_cyc;     /* MAME: reset sequence takes 9 cycles before the first opcode fetch */
    nmi_enable = irq_enable = 0;

    /* event schedule.  mame0264: frame period 1/60.58 s, vblank (frame end) at k*P, NMI periodic 480 Hz (the installed MAME's driver, used to cross-check this emulator).
     * pinned: pixel clock 18.432/3 MHz, 384 x 264, vblank at line 240, NMI = scanline timer 16, 32 (8 per frame, 40-line gap) from the pinned driver. */
    int pin = strcmp(timing, "mame0264") != 0;
    double next_irq = pin ? T.vb * T.line : T.frame;
    int nmi_k = 0;       /* pinned: index within the current frame */
    double nmi_frame0 = 0;
    double next_nmi = pin ? (16 * T.line) : 3200.0;
    for (int fr = 0; fr < nframes; fr++) {
        double tend = next_irq;              /* frame boundary == vblank start */
        for (;;) {
            double tn = next_nmi;
            if (tn >= tend) break;
            while ((double)cpu.cycles < floor(tn)) tstep();
            if (nmi_enable) set_nmi_line(1);
            if (pin) { nmi_k++; if (16 + 32 * nmi_k >= T.lines) { nmi_k = 0; nmi_frame0 += T.frame; } next_nmi = nmi_frame0 + (16 + 32 * nmi_k) * T.line; }
            else next_nmi += 3200.0;
        }
        while ((double)cpu.cycles < floor(tend)) tstep();
        /* frame boundary: dump, then the vblank IRQ is raised */
        uint32_t fn = fr; uint64_t cy = (uint64_t)floor(tend + 0.5);
        fwrite(&fn, 4, 1, fbin); fwrite(&cy, 8, 1, fbin);
        uint16_t r16[5] = { cpu.pc, cpu.x, cpu.y, cpu.u, cpu.s }; fwrite(r16, 2, 5, fbin);
        uint8_t r8[4] = { cpu.a, cpu.b, cpu.dp, cpu.cc }; fwrite(r8, 1, 4, fbin);
        fwrite(ram, 1, 0x1000, fbin);
        if (irq_enable) { if (!cpu.irq) cpu.irq_delay = irq_dly; cpu.irq = 1; }
        next_irq += T.frame;
        /* inputs for the next frame (applied when frame fr+1 starts, as in the MAME script) */
        while (evi < nev && ev[evi].f <= fr + 1) { in_sys = ev[evi].s; in_p1 = ev[evi].a; in_p2 = ev[evi].b; evi++; }
    }
    fclose(fbin); fclose(fio);
    if (cov_out) { FILE *fc = fopen(cov_out, "wb"); fwrite(cov_x, 1, 65536, fc); fwrite(cov_r, 4, 65536, fc); fwrite(cov_w, 4, 65536, fc); fwrite(cjt, 1, 65536, fc); fwrite(cjs, 2, 65536, fc); fclose(fc); }
    fprintf(stderr, "illegal opcodes: %d\n", cpu.illegal);
    return 0;
}
