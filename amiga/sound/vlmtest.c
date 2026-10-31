/* Checks the LZ4 decoder and the speech blob: every one of the 128 phrases decoded from the blob (as the Amiga does) equals the quantised capture.
 * usage: vlmtest vlm.bin vlm_u16.bin */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../runtime/lz4.h"
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long bl = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *blob = malloc(bl); if (fread(blob, 1, bl, f) != (size_t)bl) return 2; fclose(f);
    f = fopen(argv[2], "rb"); fseek(f, 0, SEEK_END); long ul = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *u = malloc(ul); if (fread(u, 1, ul, f) != (size_t)ul) return 2; fclose(f);
    long pos = 0, bad = 0, total = 0, withsound = 0;
    for (int p = 0; p < 128; p++) {
        uint32_t n; memcpy(&n, u + pos, 4); const int16_t *s = (const int16_t *)(u + pos + 4); pos += 4 + 2 * (long)n;
        const uint8_t *h = blob + p * 8;
        uint32_t off = (h[0] << 24) | (h[1] << 16) | (h[2] << 8) | h[3], clen = (h[4] << 8) | h[5], hn = (h[6] << 8) | h[7];
        if (hn != n) { printf("phrase %d: length %u vs %u\n", p, hn, n); bad++; continue; }
        uint8_t *d = malloc(n + 16);
        if (lz4_decode(blob + off, clen, d, n) < 0) { printf("phrase %d: decode error\n", p); bad++; free(d); continue; }
        int snd = 0;
        for (uint32_t i = 0; i < n; i++) {
            int v = (s[i] + 2) >> 2; if (v < -128) v = -128; if (v > 127) v = 127;
            if ((int8_t)d[i] != v) { if (bad < 5) printf("phrase %d sample %u: %d vs %d\n", p, i, (int8_t)d[i], v); bad++; }
            if (s[i]) snd = 1;
        }
        withsound += snd; total += n; free(d);
    }
    printf("128 phrases, %ld samples (%ld phrases with sound), mismatches: %ld\n", total, withsound, bad);
    return bad != 0;
}
