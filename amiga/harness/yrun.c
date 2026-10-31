/* qemu-m68k harness: runs the translated game on the shared board model and writes the same trace files as ref/src/yref.c.
 * usage: yrun [-n frames] [-i inputs.txt] [-t pinned|mame0264] [-o prefix] [-r romdir] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "board.h"
#include "yrt.h"
static uint8_t *image;
static FILE *fio, *fbin;
#include <signal.h>
#include <ucontext.h>
static void segv(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = ctx; fprintf(stderr, "SIGSEGV addr=%p pc=%08lx\n", si->si_addr, (unsigned long)uc->uc_mcontext.gregs[16]);
    for (int i = 0; i < 16; i++) fprintf(stderr, "%s%d=%08lx ", i < 8 ? "d" : "a", i & 7, (unsigned long)uc->uc_mcontext.gregs[i]);
    fprintf(stderr, "\ncycles=%llu\n", (unsigned long long)cpu.cycles); fflush(fio); exit(3);
}

static struct { int f; uint8_t s, a, b; } ev[100000]; static int nev, evi;
static int nframes = 600;
static const char *prefix = "yrun";

static void io_log(char k, uint64_t now, uint16_t a, uint8_t v) { fprintf(fio, "%c %llu %04x %02x\n", k, (unsigned long long)now, a, v); }
static void frame_cb(uint64_t now, uint32_t fr) {
    uint8_t hdr[12 + 14]; int k = 0;
    for (int i = 0; i < 4; i++) hdr[k++] = (fr >> (8 * i)) & 255;
    for (int i = 0; i < 8; i++) hdr[k++] = (now >> (8 * i)) & 255;
    uint16_t r16[5] = { cpu.pc, cpu.x, cpu.y, cpu.u, cpu.s };
    for (int i = 0; i < 5; i++) { hdr[k++] = r16[i] & 255; hdr[k++] = r16[i] >> 8; }
    hdr[k++] = cpu.a; hdr[k++] = cpu.b; hdr[k++] = cpu.dp; hdr[k++] = cpu.cc;
    fwrite(hdr, 1, sizeof hdr, fbin);
    fwrite(image + 0x5000, 1, 0x1000, fbin);
    while (evi < nev && ev[evi].f <= (int)fr + 1) { in_sys = ev[evi].s; in_p1 = ev[evi].a; in_p2 = ev[evi].b; evi++; }
    if ((int)fr + 1 >= nframes) {
        fclose(fbin); fclose(fio);
        fprintf(stderr, "slow-path entries %u, single interpreted instructions %u\n", rt_stat_slow, rt_stat_interp1);
        exit(0);
    }
}
void rt_fatal(const char *msg, uint32_t a, uint32_t b) { fprintf(stderr, "FATAL: %s (%x, %x)\n", msg, a, b); fflush(fio); exit(2); }

int main(int argc, char **argv) {
    int timing = TIMING_PINNED; const char *inp = NULL, *romdir = "roms";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) nframes = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i")) inp = argv[++i];
        else if (!strcmp(argv[i], "-o")) prefix = argv[++i];
        else if (!strcmp(argv[i], "-r")) romdir = argv[++i];
        else if (!strcmp(argv[i], "-t")) timing = !strcmp(argv[++i], "mame0264") ? TIMING_MAME0264 : TIMING_PINNED;
    }
    image = calloc(1, 65536 + 64);
    char path[512];
    snprintf(path, sizeof path, "%s/407_i08.10d", romdir); FILE *f = fopen(path, "rb"); if (!f || fread(image + 0x8000, 1, 0x4000, f) != 0x4000) { perror(path); return 1; } fclose(f);
    snprintf(path, sizeof path, "%s/407_i07.8d", romdir); f = fopen(path, "rb"); if (!f || fread(image + 0xC000, 1, 0x4000, f) != 0x4000) { perror(path); return 1; } fclose(f);
    if (inp) { FILE *fi = fopen(inp, "r"); if (!fi) { perror(inp); return 1; } unsigned fr, s, a, b; while (fscanf(fi, "%u %x %x %x", &fr, &s, &a, &b) == 4) { ev[nev].f = fr; ev[nev].s = s; ev[nev].a = a; ev[nev].b = b; nev++; } fclose(fi); }
    snprintf(path, sizeof path, "%s.bin", prefix); fbin = fopen(path, "wb");
    snprintf(path, sizeof path, "%s.io", prefix); fio = fopen(path, "w");
    board_init(image, timing);
    hooks.frame = frame_cb; hooks.io_log = io_log;
    { struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = segv; sa.sa_flags = SA_SIGINFO; sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGILL, &sa, 0); }
    rt_init(image);
    rt_run();
    return 0;
}
