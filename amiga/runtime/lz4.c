#include "lz4.h"
int lz4_decode(const uint8_t *src, int srclen, uint8_t *dst, int dstlen) {
    const uint8_t *ip = src, *iend = src + srclen; uint8_t *op = dst, *oend = dst + dstlen;
    while (ip < iend) {
        unsigned tok = *ip++, lit = tok >> 4, ml = tok & 15;
        if (lit == 15) { unsigned b; do { if (ip >= iend) return -1; b = *ip++; lit += b; } while (b == 255); }
        if ((int)lit > iend - ip || (int)lit > oend - op) return -1;
        while (lit--) *op++ = *ip++;
        if (ip >= iend) break;                                  /* the last sequence has literals only */
        if (iend - ip < 2) return -1;
        unsigned off = ip[0] | (ip[1] << 8); ip += 2;
        if (off == 0 || off > (unsigned)(op - dst)) return -1;
        if (ml == 15) { unsigned b; do { if (ip >= iend) return -1; b = *ip++; ml += b; } while (b == 255); }
        ml += 4;
        if ((int)ml > oend - op) return -1;
        const uint8_t *mp = op - off;
        while (ml--) *op++ = *mp++;                             /* byte-wise: overlapping copies (RLE) are intended */
    }
    return (op == oend) ? (int)(ip - src) : -1;
}
