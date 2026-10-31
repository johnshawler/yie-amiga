#!/usr/bin/env python3
"""Compare two per-frame traces (MAME script vs yref).  usage: cmp_trace.py a.bin b.bin
Prints per-frame RAM difference counts (frames with differences), summary and the first frame with more than --tol differing bytes."""
import sys, struct
REC = 12 + 14 + 4096
def load(p):
    d = open(p, 'rb').read(); n = len(d) // REC
    return [d[i * REC:(i + 1) * REC] for i in range(n)]
a = load(sys.argv[1]); b = load(sys.argv[2])
tol = int(sys.argv[3]) if len(sys.argv) > 3 else 4
n = min(len(a), len(b))
ndiff = 0; first = None; maxd = 0
for i in range(n):
    ra, rb = a[i], b[i]
    diff = [j for j in range(4096) if ra[26 + j] != rb[26 + j]]
    if diff:
        ndiff += 1; maxd = max(maxd, len(diff))
        if len(diff) > tol and first is None:
            first = i
            ma = struct.unpack('<HHHHHBBBB', ra[12:26]); mb = struct.unpack('<HHHHHBBBB', rb[12:26])
            print("frame %d: %d bytes differ, first %04x (mame %02x ref %02x); regs mame %s ref %s" % (i, len(diff), 0x5000 + diff[0], ra[26 + diff[0]], rb[26 + diff[0]], [hex(x) for x in ma], [hex(x) for x in mb]))
print("frames compared %d, frames with any RAM difference %d, max differing bytes in a frame %d, first frame > tol(%d): %s" % (n, ndiff, maxd, tol, first))
