#!/usr/bin/env python3
"""Extract the VLM5030 sample streams captured from MAME 0.264 (ref/trace/vlm_busy.lua run once per phrase with
   -samplerate 65080 -wavwrite pN.wav, YT_TAP=pN.tap).  MAME's VLM5030 stream rate is the integer clock/440 = 8135 Hz and the mixer holds each
   chip sample for exactly 8 output samples (edges always at j%8 == 1), so the chip samples are recovered exactly: src[n] = out[8n+1].
   The wav value is trunc(64*u*32767/32768) with u the integer lattice output (put_int_clamp(u, 512)), hence u = round(v/64).
   usage: vlmcap.py dir out.bin   -> out.bin: 128 x { u32 nsamples (LE) , int16 u[...] }   (u is the exact chip output, +-512 range)"""
import sys, wave, array, math, struct
d, out = sys.argv[1], sys.argv[2]
RS = 8135
res = []
for p in range(128):
    w = wave.open('%s/p%d.wav' % (d, p)); a = array.array('h'); a.frombytes(w.readframes(w.getnframes())); l = a[0::2]
    taps = [x.split() for x in open('%s/p%d.tap' % (d, p))]
    tf = float(taps[7][1]); assert taps[7][2] == '00'
    n0 = math.ceil(tf * RS)                          # first chip sample after the ST falling edge
    iters = int(open('%s/p%d.txt' % (d, p)).read().split()[1])
    nend = n0 + int(math.ceil((iters * 22 + 40) / 1536000.0 * RS)) + 2     # until BSY has fallen
    u = []
    for n in range(n0, nend):
        j = 8 * n + 1
        v = l[j] if j < len(l) else 0
        assert all(l[j + k] == v for k in range(8) if j + k < len(l)), (p, n)
        u.append(int(round(v / 64.0)))
    res.append(u)
nz = [max([abs(x) for x in u] or [0]) for u in res]
print("max |u| over all phrases:", max(nz), " total samples:", sum(len(u) for u in res), " phrases with sound:", sum(1 for z in nz if z))
with open(out, 'wb') as f:
    for u in res:
        f.write(struct.pack('<I', len(u))); f.write(array.array('h', u).tobytes())
