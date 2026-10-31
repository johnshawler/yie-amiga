#!/usr/bin/env python3
"""Read the debug HUD (main.c -DHUD: five rows of eight hex digits, 3x5 font, white on black, top-left of the picture) from FS-UAE native
screenshots (1 lores pixel = 2 x 2 screenshot pixels).  usage: hudread.py shot_full.png ...   prints the five 32-bit values"""
import sys
from PIL import Image
FONT = [0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x7BED, 0x6BAE, 0x7127, 0x6B6E, 0x79E7, 0x79E4]
def glyph_bits(im, ox, oy, s):          # 15 bits, row-major, from the 2x2-scaled picture
    g = 0
    for r in range(5):
        for c in range(3):
            p = im.getpixel((ox + c * s, oy + r * s))
            g = (g << 1) | (1 if sum(p) > 380 else 0)
    return g
def decode(im, ox, oy, row, s=2):
    v = 0; err = 0
    for i in range(8):
        g = glyph_bits(im, ox + (2 + i * 4) * s, oy + (1 + row * 6) * s, s)
        best = min(range(16), key=lambda d: bin(FONT[d] ^ g).count('1'))
        err += bin(FONT[best] ^ g).count('1'); v = (v << 4) | best
    return v, err
for fn in sys.argv[1:]:
    im = Image.open(fn).convert('RGB'); W, H = im.size
    best = None
    for oy in range(0, min(H - 80, 220)):
        for ox in range(0, min(W - 100, 260)):
            v, e = decode(im, ox, oy, 0)
            if best is None or e < best[0]: best = (e, ox, oy)
            if e == 0: break
        if best[0] == 0: break
    e, ox, oy = best
    print(fn.split('/')[-1], 'origin', (ox, oy), 'err', e, ' '.join('%08X' % decode(im, ox, oy, r)[0] for r in range(int(__import__('os').environ.get('ROWS', '5')))))
