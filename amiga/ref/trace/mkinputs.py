#!/usr/bin/env python3
"""Deterministic input script: attract, coin, start, then pseudo-random play.  Lines: frame system p1 p2 (hex, active low, held until next line).
usage: mkinputs.py nframes [seed] > inputs.txt"""
import sys, random
n = int(sys.argv[1]); seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
rng = random.Random(seed)
ev = [(0, 0xff, 0xff, 0xff)]
COIN_AT, START_AT = 700, 760
ev += [(COIN_AT, 0xfe, 0xff, 0xff), (COIN_AT + 6, 0xff, 0xff, 0xff), (START_AT, 0xf7, 0xff, 0xff), (START_AT + 6, 0xff, 0xff, 0xff)]
f = 800
while f < n:
    p1 = 0xff
    r = rng.random()
    d = rng.choice([0, 0, 1, 2, 4, 8, 5, 6, 9, 10])        # none / left / right / up / down / diagonals
    p1 &= ~d
    if rng.random() < 0.5: p1 &= ~0x10
    if rng.random() < 0.35: p1 &= ~0x20
    ev.append((f, 0xff, p1 & 0xff, 0xff))
    f += rng.randint(3, 25)
    # occasionally re-coin / start (game over, continue)
    if rng.random() < 0.01: ev += [(f, 0xfe, 0xff, 0xff), (f + 5, 0xf7, 0xff, 0xff), (f + 11, 0xff, 0xff, 0xff)]; f += 12
for e in sorted(ev, key=lambda e: e[0]): print("%d %02x %02x %02x" % e)
