/* Play-through bot on the reference emulator.  Every DECIDE frames it forks one child per candidate action; each child plays HORIZON frames with that action held
 * and reports a score (points gained, lives lost, stage progress); the parent adopts the best action.  All inputs are recorded (yref -i format) so that the translated
 * build can replay the run (crash / divergence test).  usage: ybot frames out_inputs [seed]   env INFLIVES=1 for infinite lives.  Output: progress on stderr. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "board.h"
#define INFLIVES_DEFAULT
static uint8_t image[MEM_SIZE];
#include "../../harness/cheat.h"
static int is_child, child_target, child_pipe; static uint32_t child_start;
static int cur_action = -1; static FILE *fin; static int nframes;
static long last_hist[8];
/* actions: p1 byte (active low): 01 left 02 right 04 up 08 down 10 kick 20 punch */
static const uint8_t ACT[] = { 0xFF, 0xFD, 0xFE, 0xFB, 0xF7, 0xED, 0xDD, 0xEE, 0xDE, 0xE5, 0xD5, 0xF5, 0xE7, 0xD7, 0xEF, 0xDF, 0xFD & 0xEF & 0xFB, 0xFE & 0xEF & 0xFB };
#define NACT ((int)sizeof ACT)
static int DECIDE = 8, HORIZON = 40;
static uint32_t score_of(void) {   /* player score: tile digits of row 3, columns 2..8 */
    uint32_t v = 0; const uint8_t *vr = image + 0x5800;
    for (int c = 2; c <= 8; c++) { uint8_t t = vr[(3 * 32 + c) * 2 + 1]; if (t <= 9) v = v * 10 + t; }
    return v;
}
static int m_e0; static uint32_t m_score0; static unsigned m_deaths0; static uint32_t m_stage0, m_best_stage;
static uint8_t seed_rng;
static unsigned rng_state = 1; static unsigned rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 16; }
static void set_inputs(int a, int coin_start) { in_p1 = ACT[a]; in_sys = coin_start; }
static int started;
static int decide(void) {
    int best = 0; double bs = -1e18;
    int order[NACT]; for (int i = 0; i < NACT; i++) order[i] = i;
    for (int i = NACT - 1; i > 0; i--) { int j = rnd() % (i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
    int pfd[NACT][2]; pid_t pid[NACT];
    for (int k = 0; k < NACT; k++) {
        int a = order[k]; pipe(pfd[k]); fflush(NULL);
        pid[k] = fork();
        if (pid[k] == 0) { is_child = 1; child_pipe = pfd[k][1]; child_target = board_frame_count() + HORIZON; child_start = board_frame_count(); cur_action = a; m_e0 = image[0x51AA]; m_score0 = score_of(); m_deaths0 = cheat_deaths; m_stage0 = image[0x51C6]; return -1 - a; }
        close(pfd[k][1]);
    }
    for (int k = 0; k < NACT; k++) {
        double sc = -1e9; read(pfd[k][0], &sc, sizeof sc); close(pfd[k][0]); waitpid(pid[k], 0, 0);
        sc += (rnd() % 100) * 0.001;
        if (sc > bs) { bs = sc; best = order[k]; }
    }
    return best;
}
static int sys_pulse;
static void frame_cb(uint64_t now, uint32_t fr) {
    (void)now;
    cheat_frame(image);
    if (is_child) {
        if (fr + 1 >= (uint32_t)child_target) {
            double sc = (double)(score_of() - m_score0) - 5000.0 * (cheat_deaths - m_deaths0) + 3000.0 * ((int)image[0x51C6] - (int)m_stage0);
            { int e1 = image[0x51AA]; if (m_e0 > 0 && e1 < m_e0) sc -= 60.0 * (m_e0 - e1); }
            if (image[0x51C3] != 2) sc -= 1e6;
            write(child_pipe, &sc, sizeof sc); _exit(0);
        }
        return;
    }
    if (fr + 1 >= (uint32_t)nframes) {
        fprintf(stderr, "done: %u frames, deaths %u, score %u state %02x stage %02x\n", fr + 1, cheat_deaths, score_of(), image[0x51C3], image[0x51C6]); fclose(fin); exit(0);
    }
    uint8_t st = image[0x51C3];
    { static int ls = -1; if (st != ls) { fprintf(stderr, "  frame %u: state %02x -> %02x (score %u stage %02x lives %u)\n", fr, ls, st, score_of(), image[0x51C6], image[0x51A8]); ls = st; } }
    int sys = 0xFF;
    if (st != 2) {                                   /* attract / game over / waiting: coin then start */
        if ((fr / 30) % 4 == 0) sys = 0xFE; else if ((fr / 30) % 4 == 2) sys = 0xF7;
        in_p1 = 0xFF; in_sys = sys;
        static int lastsys = -1, lastp1 = -1; if (sys != lastsys || lastp1 != 0xFF) { fprintf(fin, "%u %02x %02x ff\n", fr + 1, sys, 0xFF); lastsys = sys; lastp1 = 0xFF; }
        return;
    }
    static int act = 0; static uint32_t next_dec; static int lastsys = -1, lastp1 = -1;
    if (fr >= next_dec) {
        int r = decide();
        if (r < 0) { act = -1 - r; set_inputs(act, 0xFF); return; }
        act = r; next_dec = fr + DECIDE;
    }
    set_inputs(act, 0xFF);
    if (ACT[act] != lastp1 || lastsys != 0xFF) { fprintf(fin, "%u ff %02x ff\n", fr + 1, ACT[act]); lastp1 = ACT[act]; lastsys = 0xFF; }
    static uint32_t rep; if ((fr % 600) == 0) fprintf(stderr, "frame %u score %u stage %02x lives %u deaths %u\n", fr, score_of(), image[0x51C6], image[0x51A8], cheat_deaths);
}
static uint8_t rom_at(uint16_t pc) { return image[pc]; }
int main(int argc, char **argv) {
    nframes = atoi(argv[1]); fin = fopen(argv[2], "w"); rng_state = argc > 3 ? atoi(argv[3]) : 1;
    if (getenv("DECIDE")) DECIDE = atoi(getenv("DECIDE")); if (getenv("HORIZON")) HORIZON = atoi(getenv("HORIZON"));
    char path[512]; FILE *f;
    snprintf(path, sizeof path, "roms/407_i08.10d"); f = fopen(path, "rb"); if (!f || fread(image + 0x8000, 1, 0x4000, f) != 0x4000) { perror(path); return 1; } fclose(f);
    snprintf(path, sizeof path, "roms/407_i07.8d"); f = fopen(path, "rb"); if (!f || fread(image + 0xC000, 1, 0x4000, f) != 0x4000) { perror(path); return 1; } fclose(f);
    fprintf(fin, "0 ff ff ff\n");
    board_init(image, TIMING_PINNED);
    cpu.rd = board_rd; cpu.wr = board_wr;
    hooks.frame = frame_cb;
    board_reset();
    while (1) {
        uint64_t ev_t = board_next_event();
        while (cpu.cycles < ev_t) m6809_step(&cpu);
        board_fire();
    }
}
