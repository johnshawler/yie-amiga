/* Minimal LZ4 block decoder (lossless; the format of https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md).  Portable. */
#ifndef LZ4_H
#define LZ4_H
#include <stdint.h>
/* decodes exactly `dstlen` bytes from src; returns the number of source bytes consumed, or -1 on malformed input */
int lz4_decode(const uint8_t *src, int srclen, uint8_t *dst, int dstlen);
#endif
