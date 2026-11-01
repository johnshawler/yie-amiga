/* Reference emulator driver for the Konami "Yie Ar Kung-Fu" board (GX407): pure interpreter (m6809.c) on the shared board model (runtime/board.c).
 *
 * usage: yref [-n frames] [-i inputs.txt] [-t pinned|mame0264] [-o prefix] [-r romdir] [-C covfile] [-T cycles]
 * Output (same layout as ref/trace/mame_trace.lua): prefix.bin  per frame: u32 frame, u64 cycles, u16 pc x y u s, u8 a b dp cc, 4096 bytes RAM $5000-$5FFF
 *                                                    prefix.io   "W cycle addr data" / "R cycle addr data" / "V cycle vector-addr data"
 *  -C : write execution / data-access coverage (instruction starts, reads, writes, computed jump targets)
 *  -T : print an instruction trace until the given cycle count (stderr) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "board.h"

static uint8_t image[MEM_SIZE];
static FILE *fio, *fbin;
static int trace_until = 0;
static uint8_t cov_x[65536];
static uint32_t cov_r[65536], cov_w[65536];
static uint8_t cjt[65536]; static uint16_t cjs[65536];
static uint64_t prof_cyc[65536], prof_cnt[65536]; static const char *prof_out = NULL;
static const char *cov_out = NULL;
static struct { int f; uint8_t s, a, b; } ev[100000]; static int nev, evi;

static uint8_t y_rd(void *ctx, uint16_t a) {
    if (!cpu.in_fetch) cov_r[a]++;
    if ((a == 0xfff8 || a == 0xfffc) && fio) fprintf(fio, "V %llu %04x %02x\n", (unsigned long long)cpu.cycles, a, image[a]);
    return board_rd(ctx, a);
}
static int watch_addr = -1;
static void y_wr(void *ctx, uint16_t a, uint8_t v) { cov_w[a]++; if (a == watch_addr) fprintf(stderr, "WATCH %04x <- %02x at cycle %llu pc=%04x ppc=%04x cc=%02x\n", a, v, (unsigned long long)cpu.cycles, cpu.pc, cpu.ppc, cpu.cc); board_wr(ctx, a, v); }

static void io_log(char k, uint64_t now, uint16_t a, uint8_t v) { if (fio) fprintf(fio, "%c %llu %04x %02x\n", k, (unsigned long long)now, a, v); }

#include "../../harness/cheat.h"
static void frame_cb(uint64_t now, uint32_t fr) {
    if (cheat_on < 0) cheat_frame(image);
    uint64_t cy = now;
    /* the dump is stamped with the (rounded) event time like the MAME script does; use the exact cycle of the boundary */
    if (fr % dump_every == 0) {
    uint32_t fn = fr; fwrite(&fn, 4, 1, fbin); fwrite(&cy, 8, 1, fbin);
    uint16_t r16[5] = { cpu.pc, cpu.x, cpu.y, cpu.u, cpu.s }; fwrite(r16, 2, 5, fbin);
    uint8_t r8[4] = { cpu.a, cpu.b, cpu.dp, cpu.cc }; fwrite(r8, 1, 4, fbin);
    fwrite(image + 0x5000, 1, 0x1000, fbin); }
    cheat_frame(image);
    while (evi < nev && ev[evi].f <= (int)fr + 1) { in_sys = ev[evi].s; in_p1 = ev[evi].a; in_p2 = ev[evi].b; evi++; }
}

static uint8_t rom_at(uint16_t pc) { return image[pc]; }

int main(int argc, char **argv) {
    int nframes = 600, timing = TIMING_PINNED; const char *inp = NULL, *prefix = "yref", *romdir = "roms";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) nframes = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i")) inp = argv[++i];
        else if (!strcmp(argv[i], "-o")) prefix = argv[++i];
        else if (!strcmp(argv[i], "-r")) romdir = argv[++i];
        else if (!strcmp(argv[i], "-t")) timing = !strcmp(argv[++i], "mame0264") ? TIMING_MAME0264 : TIMING_PINNED;
        else if (!strcmp(argv[i], "-C")) cov_out = argv[++i];
        else if (!strcmp(argv[i], "-P")) prof_out = argv[++i];
        else if (!strcmp(argv[i], "-W")) watch_addr = strtol(argv[++i], 0, 16);
        else if (!strcmp(argv[i], "-T")) trace_until = atoi(argv[++i]);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/407_i08.10d", romdir); FILE *f = fopen(path, "rb"); if (!f || fread(image + 0x8000, 1, 0x4000, f) != 0x4000) { perror(path); return 1; } fclose(f);
    snprintf(path, sizeof path, "%s/407_i07.8d", romdir); f = fopen(path, "rb"); if (!f || fread(image + 0xC000, 1, 0x4000, f) != 0x4000) { perror(path); return 1; } fclose(f);
    if (inp) { FILE *fi = fopen(inp, "r"); if (!fi) { perror(inp); return 1; } unsigned fr, s, a, b; while (fscanf(fi, "%u %x %x %x", &fr, &s, &a, &b) == 4) { ev[nev].f = fr; ev[nev].s = s; ev[nev].a = a; ev[nev].b = b; nev++; } fclose(fi); }
    snprintf(path, sizeof path, "%s.bin", prefix); fbin = fopen(path, "wb");
    snprintf(path, sizeof path, "%s.io", prefix); fio = fopen(path, "w");
    board_init(image, timing);
    cpu.rd = y_rd; cpu.wr = y_wr;
    hooks.frame = frame_cb; hooks.io_log = io_log;
    board_reset();
    while (board_frame_count() < (uint32_t)nframes) {
        uint64_t ev_t = board_next_event();
        while (cpu.cycles < ev_t) {
            uint16_t pc0 = cpu.pc; uint8_t op0 = rom_at(pc0);
            cov_x[pc0] = 1;
            if (trace_until && cpu.cycles < (uint64_t)trace_until && pc0 >= 0x8000)
                fprintf(stderr, "%llu pc=%04x op=%02x %02x %02x a=%02x b=%02x x=%04x s=%04x cc=%02x\n", (unsigned long long)cpu.cycles, pc0, image[pc0], image[pc0+1], image[pc0+2], cpu.a, cpu.b, cpu.x, cpu.s, cpu.cc);
            { uint64_t c0 = cpu.cycles; m6809_step(&cpu); prof_cyc[pc0] += cpu.cycles - c0; prof_cnt[pc0]++; }
            if ((op0 == 0x6E || op0 == 0xAD) && cpu.ppc == pc0) { cjt[cpu.pc] = 1; cjs[cpu.pc] = pc0; }
        }
        board_fire();
    }
    fclose(fbin); fclose(fio);
    if (cov_out) { FILE *fc = fopen(cov_out, "wb"); fwrite(cov_x, 1, 65536, fc); fwrite(cov_r, 4, 65536, fc); fwrite(cov_w, 4, 65536, fc); fwrite(cjt, 1, 65536, fc); fwrite(cjs, 2, 65536, fc); fclose(fc); }
    if (prof_out) { FILE *fp = fopen(prof_out, "w"); for (int a = 0; a < 65536; a++) if (prof_cnt[a]) fprintf(fp, "%04x %llu %llu\n", a, (unsigned long long)prof_cnt[a], (unsigned long long)prof_cyc[a]); fclose(fp); }
    fprintf(stderr, "illegal opcodes: %d\n", cpu.illegal);
    return 0;
}
