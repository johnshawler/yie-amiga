#!/usr/bin/env python3
"""Compare an audio recording of the Amiga build (FS-UAE through OpenAL's wave backend, 48 kHz stereo) with the stream the build is supposed to play.
   Expected stream (sound/sndpc.c): one int8 block per frame at Paula's period-124 rate (the SN76489A synthesis), plus the speech phrases (blob) started at the same
   block.  The check fits   rec(t) = g * expected(t - D) + c   by least squares in windows of W seconds (D searched by cross-correlation) and reports the
   correlation coefficient and the relative residual per window.  Windows containing an underrun (silence blocks) or a dropped block do not fit and are counted as such.
   usage: audiocheck.py rec.wav expected.sn [expected.ev vlm_u16.bin] [pal|ntsc]"""
import sys, wave, array, struct
import numpy as np
rec_path, sn_path = sys.argv[1], sys.argv[2]
ev_path = sys.argv[3] if len(sys.argv) > 3 else None
vlm_path = sys.argv[4] if len(sys.argv) > 4 else None
ntsc = len(sys.argv) > 5 and sys.argv[5] == 'ntsc'
FS = (3579545 if ntsc else 3546895) / 124.0
FV = 8135.0
w = wave.open(rec_path); RATE = w.getframerate(); a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
L, R = a[0::2], a[1::2]
sn = np.fromfile(sn_path, dtype=np.int8).astype(np.float64)
T = len(sn) / FS
# speech timeline (8-bit speech samples as played: (u+2)>>2) on a 1/RATE grid
vl = np.zeros(int(T * RATE) + RATE)
if ev_path and vlm_path:
    d = open(vlm_path, 'rb').read(); pos = 0; ph = []
    for p in range(128):
        n = struct.unpack_from('<I', d, pos)[0]; u = np.frombuffer(d[pos + 4:pos + 4 + 2 * n], dtype='<i2').astype(np.int64); pos += 4 + 2 * n
        ph.append(np.clip((u + 2) >> 2, -128, 127).astype(np.float64))
    # block frame f starts at sample index of the stream: sum of block lengths; here approximate by frame time (blocks have FS*0.0165 samples each)
    evs = []
    for l in open(ev_path):
        t = l.split()
        if t[0] == 'S': evs.append((int(t[1]), int(t[2]), int(t[3])))
        else: evs.append((int(t[1]), -1, -1))
    cur = None   # (start_index_in_48k, data)
    seg = []     # list of (start, end, data)
    for f, lead, p in evs:
        t0 = f * 0.0165 + (lead / FV if lead >= 0 else 0)
        i0 = int(round(t0 * RATE))
        if seg and seg[-1][1] > i0: seg[-1] = (seg[-1][0], i0, seg[-1][2])
        if lead >= 0:
            n = len(ph[p]); idx = np.arange(int(n * RATE / FV) + 1); src = np.minimum((idx * FV / RATE).astype(int), n - 1)
            seg.append((i0, i0 + len(idx), ph[p][src]))
    for s, e, dd in seg:
        e = min(e, s + len(dd), len(vl))
        if e > s: vl[s:e] += dd[:e - s]
# expected SN at the recording rate (zero-order hold)
# (UAE resamples Paula's 28.6 kHz output to the recording rate with interpolation and dither: expected = linear interpolation of the stream, and both
#  signals are low-passed (boxcar of 12 samples, about 4 kHz) before they are compared)
tt = np.arange(int(T * RATE)) * FS / RATE
exp = np.interp(tt, np.arange(len(sn)), sn)
WSEC = float(__import__('os').environ.get('WSEC', '1.0'))
W = int(WSEC * RATE)
good = bad = 0; res = []
# global delay: the first sample at which the stream leaves its idle level (-128 for the SN stream, DC level of the recording after the stream has started)
BOX = np.ones(12) / 12.0
tot_exp = np.convolve(exp + vl[:len(exp)], BOX, mode='same')
L = np.convolve(L, BOX, mode='same')
t1 = int(np.argmax(np.abs(exp + 128) > 0.5))
Lraw = L; dc = np.median(L[int(len(L) * 0.2):int(len(L) * 0.3)]) if False else None
# recording: the stream starts when the level steps to the idle level; take the most common value after the step as DC
vals, cnts = np.unique(L[int(24 * RATE):].astype(np.int64), return_counts=True); dc = vals[np.argmax(cnts)]
t2 = int(np.argmax(np.abs(L - dc) > 3 * 1 * (1 if False else 1)) if False else 0)
idle = (np.abs(L - dc) < 8).astype(np.int32)
run = np.convolve(idle, np.ones(int(0.2 * RATE), dtype=np.int32), mode='valid')            # 0.2 s all idle
t_step = int(np.argmax(run >= int(0.2 * RATE)))
nz = np.nonzero(np.abs(L[t_step:] - dc) > 60)[0]
t2 = int(t_step + nz[0]) if len(nz) else 0
print("stream starts (idle level reached) at %.3f s" % (t_step / RATE))
lag = t2 - t1
print("rate %d Hz, recording %.1f s, expected %.1f s; idle level %d; first sound: recording %.3f s, expected %.3f s -> delay %.3f s" % (RATE, len(L) / RATE, T, dc, t2 / RATE, t1 / RATE, lag / RATE))
tot = tot_exp
SCALES = np.arange(-0.006, 0.0061, 0.0005)      # the emulator's audio clock is adapted to real time: allow a rate error of +-0.6 % between stream and recording
tpos = np.arange(W)
SPAN = float(__import__('os').environ.get('SPAN', '0.1'))   # search range around the global delay (seconds): the emulator's audio clock drifts slowly against the stream
cur = lag
for k in range(0, min(len(tot), len(L) - lag - W) - W, W):
    x = tot[k:k + W]
    if x.std() < 1.0: continue
    # coarse: normalised cross-correlation (FFT) of the window with the recording around the tracked delay
    lo = max(0, cur + k - int(SPAN * RATE)); hi = min(len(L), cur + k + W + int(SPAN * RATE)); y = L[lo:hi]; m = W
    if len(y) < m + 8: continue
    n = 1 << int(np.ceil(np.log2(len(y) + m)))
    cc = np.fft.irfft(np.fft.rfft(y, n) * np.conj(np.fft.rfft(x - x.mean(), n)), n)[:len(y) - m + 1]
    cs = np.concatenate([[0], np.cumsum(y ** 2)]); cs1 = np.concatenate([[0], np.cumsum(y)])
    ws_ = cs1[m:] - cs1[:-m]; wq = cs[m:] - cs[:-m]; var = wq - ws_ ** 2 / m; var[var < 1e-6] = 1e-6
    rr_ = cc / (np.sqrt(((x - x.mean()) ** 2).sum()) * np.sqrt(var)); k0 = int(np.argmax(rr_)); j0 = lo + k0
    # fine: delay +-40 samples and rate error
    best = None
    for sc in SCALES:
        xs = np.interp(tpos * (1.0 + sc) + k, np.arange(len(tot)), tot)
        for d in range(-24, 25):
            j = j0 + d
            if j < 0 or j + W > len(L): continue
            yy = L[j:j + W]
            r = np.corrcoef(xs, yy)[0, 1]
            if best is None or r > best[0]: best = (r, j, sc, xs)
    r, j, sc, xs = best; y = L[j:j + W]
    A = np.vstack([xs, np.ones_like(xs)]).T; g, c = np.linalg.lstsq(A, y, rcond=None)[0]
    resid = y - (g * xs + c); rel = resid.std() / max(y.std(), 1e-9)
    res.append((k / RATE, r, g, rel, int((j - (cur + k)))))
    if r > 0.9: good += 1
    else: bad += 1
print("windows (%.2f s) with signal: %d   correlation > 0.9: %d   lower: %d" % (WSEC, good + bad, good, bad))
if res:
    rs = np.array([r[1] for r in res]); gs = np.array([r[2] for r in res if r[1] > 0.98]); rel = np.array([r[3] for r in res if r[1] > 0.98])
    print("median correlation %.4f   gain (recording units per stream unit) median %.3f   residual/std median %.4f" % (np.median(rs), np.median(gs) if len(gs) else 0, np.median(rel) if len(rel) else 0))
    print("first windows:", [("%.0fs r=%.3f" % (t, r)) for t, r, g, rl, dd in res[:6]])
    if __import__("os").environ.get("VERBOSE"): print("\n".join("%7.2fs r=%.3f gain=%.1f resid=%.3f lag=%+d" % (t, r, g, rl, dd) for t, r, g, rl, dd in res))
