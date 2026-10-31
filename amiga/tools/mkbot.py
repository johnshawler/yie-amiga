#!/usr/bin/env python3
"""Scripted play for the soak test: coin, start, then random forward attacks (a seed that reaches round 2 within 3000 frames: 1).
   usage: mkbot.py seed frames inputs.txt   (format of yref -i: "frame sys p1 p2", hex, active low)"""
import random, sys
seed=int(sys.argv[1]); n=int(sys.argv[2]); out=sys.argv[3]
random.seed(seed)
ev=[(0,0xff,0xff,0xff),(700,0xfe,0xff,0xff),(706,0xff,0xff,0xff),(760,0xf7,0xff,0xff),(766,0xff,0xff,0xff)]
f=800
acts=[0xed,0xdd,0xed,0xfd,0xe9,0xd9,0xfb^0x10,0xf5,0xef,0xdf]  # forward+kick, forward+punch, forward, crouch/up variants
while f<n:
    a=random.choice(acts); d=random.randint(4,14)
    ev.append((f,0xff,a,0xff)); f+=d
    if random.random()<0.25: ev.append((f,0xff,0xff,0xff)); f+=random.randint(1,4)
open(out,'w').write(''.join("%d %02x %02x %02x\n"%e for e in ev))
