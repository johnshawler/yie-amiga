#!/usr/bin/env python3
"""usage: hudspeed.py f1_full.png f2_full.png  -> speed (frames / ticks) and idle fraction from two HUD readings (build with -DHUD)"""
import sys, subprocess, os
here = os.path.dirname(os.path.abspath(__file__))
def rd(fn):
    out = subprocess.check_output([sys.executable, os.path.join(here, 'hudread.py'), fn]).decode().split()
    return [int(x, 16) for x in out[6:]]
a, b = rd(sys.argv[1]), rd(sys.argv[2])
df, dt, ds, cal = b[0] - a[0], b[1] - a[1], b[2] - a[2], a[3]
print('frames %d ticks %d  speed %.1f%%   idle (spare CPU) %.1f%%   [spin calibration %d per tick]' % (df, dt, 100 * df / dt, 100 * ds / (dt * cal), cal))
