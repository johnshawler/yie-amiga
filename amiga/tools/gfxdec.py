"""Yie Ar Kung-Fu graphics ROM decoding (MAME gfx_layout rules, src/mame/konami/yiear.cpp) and the palette PROM."""
import os
ROMS = os.path.join(os.path.dirname(__file__), '..', '..', 'roms')
def rd(n): return open(os.path.join(ROMS, n), 'rb').read()

def readbit(buf, bitnum): return (buf[bitnum >> 3] >> (7 - (bitnum & 7))) & 1

def decode(region, w, h, planes, xoff, yoff, charinc, n):
    """MAME gfx decode: planes[0] is the most significant pen bit; offsets are bit offsets (bit 0 = MSB of the first byte)."""
    out = []
    for c in range(n):
        base = c * charinc
        ch = [[0] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                v = 0
                for p in planes: v = (v << 1) | readbit(region, base + p + xoff[x] + yoff[y])
                ch[y][x] = v
        out.append(ch)
    return out

def tiles_region(): return rd('407_c01.6h') + rd('407_c02.7h')
def sprites_region(): return rd('407_d05.16h') + rd('407_d06.17h') + rd('407_d03.14h') + rd('407_d04.15h')

def decode_tiles():
    reg = tiles_region(); half = len(reg) * 8 // 2
    return decode(reg, 8, 8, [4, 0, half + 4, half + 0], [0, 1, 2, 3, 64, 65, 66, 67], [0, 8, 16, 24, 32, 40, 48, 56], 16 * 8, 512)

def decode_sprites():
    reg = sprites_region(); half = len(reg) * 8 // 2
    xo = [0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195]
    yo = [0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312]
    return decode(reg, 16, 16, [4, 0, half + 4, half + 0], xo, yo, 64 * 8, 512)

def palette(variant='pinned'):
    """variant 'pinned': blue weights 0x52/0xad (pinned driver); 'mame0264': 0x47/0x97 (installed MAME 0.264, used only to cross-check the renderer)"""
    prom = rd('407c10.1g'); out = []
    for b in prom:
        r = 0x21 * (b & 1) + 0x47 * ((b >> 1) & 1) + 0x97 * ((b >> 2) & 1)
        g = 0x21 * ((b >> 3) & 1) + 0x47 * ((b >> 4) & 1) + 0x97 * ((b >> 5) & 1)
        bl = (0x52 * ((b >> 6) & 1) + 0xad * ((b >> 7) & 1)) if variant == 'pinned' else (0x47 * ((b >> 6) & 1) + 0x97 * ((b >> 7) & 1))
        out.append((r, g, bl))
    return out

def render(ram, flip=0):
    """reference renderer: ram = 4096 bytes ($5000-$5FFF); returns 224 rows x 256 colour indices (0..31)"""
    T = decode_tiles() if not hasattr(render, 'T') else render.T
    S = decode_sprites() if not hasattr(render, 'S') else render.S
    render.T, render.S = T, S
    vram = ram[0x800:0x1000]; spr1 = ram[0x000:0x030]; spr2 = ram[0x400:0x430]
    fb = [[0] * 256 for _ in range(256)]               # tilemap 256x256 -> pens 16 + pen
    for ti in range(1024):
        attr = vram[ti * 2]; code = vram[ti * 2 + 1] | ((attr & 0x10) << 4)
        fx = bool(attr & 0x80); fy = bool(attr & 0x40)
        if flip: fx = not fx; fy = not fy
        col, row = ti % 32, ti // 32
        if flip: col, row = 31 - col, 31 - row
        ch = T[code]
        for y in range(8):
            sy = 7 - y if fy else y
            for x in range(8):
                sx = 7 - x if fx else x
                fb[row * 8 + y][col * 8 + x] = 16 + ch[sy][sx]
    for offs in range(0x2e, -1, -2):
        attr = spr1[offs]; code = spr2[offs + 1] + 256 * (attr & 1)
        flipx = not (attr & 0x40); flipy = bool(attr & 0x80)
        sy = 240 - spr1[offs + 1]; sx = spr2[offs]
        if flip: sy = 240 - sy; flipy = not flipy
        if offs < 0x26: sy += 1
        ch = S[code]
        for y in range(16):
            yy = sy + y
            if not (16 <= yy < 240): continue
            srow = ch[15 - y if flipy else y]
            for x in range(16):
                xx = sx + x
                if not (0 <= xx < 256): continue
                p = srow[15 - x if flipx else x]
                if p: fb[yy][xx] = p
    return fb[16:240]

if __name__ == '__main__':
    import sys, struct
    pix = open(sys.argv[1], 'rb').read(); ramf = open(sys.argv[2], 'rb').read()
    pal = palette('mame0264'); rgb = [(0xFF << 24) | (r << 16) | (g << 8) | b for r, g, b in pal]
    REC = 12 + 14 + 4096
    off = 0; bad = 0; n = 0
    while off < len(pix):
        fr, nb, w, h = struct.unpack_from('<IIII', pix, off)
        px = struct.unpack_from('<%dI' % (nb // 4), pix, off + 16); off += 16 + nb
        ram = ramf[fr * REC + 26: fr * REC + 26 + 4096]
        flip = ram[0x1C1] & 1
        fb = render(ram, flip)
        diff = sum(1 for y in range(224) for x in range(256) if rgb[fb[y][x]] != px[y * 256 + x])
        n += 1
        if diff: bad += 1; print("frame", fr, "pixels differ:", diff, flush=True)
    print("frames checked", n, "with differences", bad)
