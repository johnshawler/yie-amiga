#!/usr/bin/env python3
"""Builds the VLM5030 speech blob for the Amiga from the exact MAME captures (ref/audio/vlm_u16.bin, see tools/vlmcap.py).
   Every one of the 128 phrases is kept in full (from the START strobe until the chip's BSY has fallen, including the leading and trailing
   silence); only the Paula quantisation (10-bit lattice output u -> signed 8 bit, round(u/4), clamp) and the LZ4 block compression are applied.
   Output vlm.bin:  128 x { u32 data offset, u16 compressed length, u16 samples }  followed by the LZ4 blocks (one per phrase, big endian header).
   usage: mkvlm.py ref/audio/vlm_u16.bin out.bin [check]"""
import sys, struct, array

def lz4_compress(data):
    """Greedy LZ4 block compressor (hash of 4 bytes, one candidate).  Conforms to the block format: last 5 bytes literals, last match starts >= 12 bytes before the end."""
    n = len(data); out = bytearray(); anchor = 0; i = 0; table = {}
    def emit(lit_end, match_len=0, offset=0, last=False):
        nonlocal anchor
        lit = lit_end - anchor
        tok = (min(lit, 15) << 4) | (0 if last else min(match_len - 4, 15))
        out.append(tok)
        if lit >= 15:
            r = lit - 15
            while r >= 255: out.append(255); r -= 255
            out.append(r)
        out.extend(data[anchor:lit_end])
        if not last:
            out.extend(struct.pack('<H', offset))
            if match_len - 4 >= 15:
                r = match_len - 4 - 15
                while r >= 255: out.append(255); r -= 255
                out.append(r)
    limit = n - 12
    while i < limit:
        key = bytes(data[i:i + 4]); cand = table.get(key); table[key] = i
        if cand is not None and i - cand <= 65535:
            ml = 4
            while i + ml < n - 5 and data[cand + ml] == data[i + ml]: ml += 1
            emit(i, ml, i - cand); i += ml; anchor = i
            continue
        i += 1
    emit(n, last=True)
    return bytes(out)

def lz4_decompress(src, n):
    out = bytearray(); ip = 0
    while ip < len(src):
        tok = src[ip]; ip += 1; lit = tok >> 4; ml = tok & 15
        if lit == 15:
            while True:
                b = src[ip]; ip += 1; lit += b
                if b != 255: break
        out += src[ip:ip + lit]; ip += lit
        if ip >= len(src): break
        off = src[ip] | (src[ip + 1] << 8); ip += 2
        if ml == 15:
            while True:
                b = src[ip]; ip += 1; ml += b
                if b != 255: break
        ml += 4
        for _ in range(ml): out.append(out[-off])
    assert len(out) == n, (len(out), n)
    return bytes(out)

def load_u(path):
    d = open(path, 'rb').read(); pos = 0; res = []
    for p in range(128):
        n = struct.unpack_from('<I', d, pos)[0]; a = array.array('h'); a.frombytes(d[pos + 4:pos + 4 + 2 * n]); pos += 4 + 2 * n; res.append(list(a))
    return res

def to_s8(u):
    return bytes(((max(-128, min(127, int(round(x / 4.0))))) & 255) for x in u)

if __name__ == '__main__':
    phrases = load_u(sys.argv[1]); out = sys.argv[2]
    blocks = []; hdr = bytearray(); off = 0; raw = 0
    for u in phrases:
        s8 = to_s8(u); c = lz4_compress(s8); assert lz4_decompress(c, len(s8)) == s8
        blocks.append(c); raw += len(s8)
    base = 128 * 8
    for u, c in zip(phrases, blocks):
        hdr += struct.pack('>IHH', base + off, len(c), len(u)); off += len(c)
    open(out, 'wb').write(bytes(hdr) + b''.join(blocks))
    print("phrases: 128  samples (8 bit): %d  compressed: %d bytes (%.1f%%)  blob: %d bytes" % (raw, off, 100.0 * off / raw, base + off))
