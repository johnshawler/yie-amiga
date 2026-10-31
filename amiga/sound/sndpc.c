/* Host model of the Amiga sound path (gamesound.c): from a yref I/O log produce the SN stream exactly as the Amiga queues it (one block per frame, PAL or NTSC
 * word counts, synthesised by runtime/snsynth.c) and the list of speech events.  Used by tools/audiocheck.py to compare with an FS-UAE audio recording.
 * usage: sndpc io.log frames out_prefix [pal|ntsc]   ->  out_prefix.sn (int8 stream), out_prefix.ev ("S frame lead phrase" / "R frame") */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../runtime/snsynth.h"
int main(int argc, char **argv) {
    int nframes = atoi(argv[2]); const char *pre = argv[3]; int ntsc = argc > 4 && !strcmp(argv[4], "ntsc");
    uint32_t wps24 = ntsc ? 3995581428u : 3959136647u, wfrac = 0;
    char path[512]; snprintf(path, sizeof path, "%s.sn", pre); FILE *fs = fopen(path, "wb");
    snprintf(path, sizeof path, "%s.ev", pre); FILE *fe = fopen(path, "w");
    FILE *io = fopen(argv[1], "r"); char line[128];
    static long wc[600000]; static int wd[600000], wk[600000]; int nw = 0, latch = 0, vdata = 0, vctl = 0;
    while (fgets(line, sizeof line, io)) {
        char k; long long cy; unsigned a, v;
        if (sscanf(line, "%c %lld %x %x", &k, &cy, &a, &v) != 4 || k != 'W') continue;
        long fr = cy / 25344;
        if (a == 0x4800) latch = v;
        else if (a == 0x4900) { wc[nw] = cy; wd[nw] = latch; wk[nw++] = 0; }
        else if (a == 0x4b00) vdata = v;
        else if (a == 0x4a00) {
            if (v & 4) fprintf(fe, "R %ld\n", fr);
            else if ((v & 2) && !(vctl & 2)) fprintf(fe, "S %ld %lld %d\n", fr, (long long)((cy - fr * 25344) * 8135 / 1536000), (vdata >> 1) & 127);
            vctl = v;
        }
    }
    sns_init(); int fed = 0; static int8_t blk[512]; long total = 0;
    for (int f = 0; f < nframes; f++) {
        while (fed < nw && wc[fed] < (long)(f + 1) * 25344 + 2) { sns_write((uint64_t)wc[fed], (uint8_t)wd[fed]); fed++; }
        wfrac += wps24 & 0xFFFFFF; unsigned words = (wps24 >> 24) + (wfrac >> 24); wfrac &= 0xFFFFFF;
        sns_render((uint32_t)f, words * 2, blk, 0); fwrite(blk, 1, words * 2, fs); total += words * 2;
    }
    fclose(fs); fclose(fe);
    printf("%d frames, %ld stream samples, %d chip writes\n", nframes, total, nw);
    return 0;
}
