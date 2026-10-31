#!/usr/bin/env python3
"""Generate per-instruction translation tests: for every distinct (instruction bytes, live-flag mask) used by the program, the translated snippet
(build/xltest/tests.s) and its description (tests.inc).  Run by run.sh against the reference core under qemu-m68k."""
import os, sys, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import model, flags as F
from emit import Tr, Unsupported, NZVC
import ytrans

CONTROL = {'BRA','BRN','BHI','BLS','BCC','BCS','BNE','BEQ','BVC','BVS','BPL','BMI','BGE','BLT','BGT','BLE','BSR','JSR','JMP','RTS','RTI',
           'SWI','SWI2','SWI3','SYNC','CWAI','LBRA','LBSR','LBRN','LBHI','LBLS','LBCC','LBCS','LBNE','LBEQ','LBVC','LBVS','LBPL','LBMI',
           'LBGE','LBLT','LBGT','LBLE'}

def ccrmask(live): return sum(b for f, b in ((F.N, 8), (F.Z, 4), (F.V, 2), (F.C, 1)) if live & f)

def main():
    out = os.path.join(HERE, '..', '..', '..', 'build', 'xltest'); os.makedirs(out, exist_ok=True)
    rom = model.load_rom()
    g = ytrans.Gen(rom)
    tests = []; seen = set(); unsup = collections.Counter()
    for blk in g.blocks:
        if g.is_interp(blk[0]): continue
        live = NZVC; lives = [0] * len(blk)
        for k in range(len(blk) - 1, -1, -1):
            lives[k] = live
            rd, wr = ytrans.effect(blk[k]); live = (live & ~wr) | rd
        for k, i in enumerate(blk):
            if i.mnem in CONTROL or g.is_interp(i): continue
            if i.mnem == 'PULS' and i.raw[-1] & 0x80: continue
            if i.mode == 'ext' and i.ea < 0x5000: continue          # I/O operand: covered by the whole-program runs
            if i.mode == 'idx' and i.idx['kind'] in ('pc8', 'pc16') and i.ea < 0x5000: continue
            for lv in sorted({lives[k], NZVC}):
                key = (i.raw, lv)
                if key in seen: continue
                seen.add(key)
                t = Tr(); t.blk_total = 10; t.blk_off = 0
                try: t.tr(i, lv)
                except Unsupported as ex: unsup[i.mnem] += 1; continue
                tests.append((i, lv, list(t.out)))
    with open(os.path.join(out, 'tests.s'), 'w') as f:
        f.write('\t.include "yvm.inc"\n\t.text\n\t.even\n')
        for n, (i, lv, lines) in enumerate(tests):
            f.write(f"\t.globl tf_{n}\ntf_{n}:\n")
            f.write('\n'.join(lines) + '\n\trts\n')
    with open(os.path.join(out, 'tests.inc'), 'w') as f:
        for n, (i, lv, lines) in enumerate(tests):
            f.write(f'{{"{i.addr:04X} {i.mnem} {i.operand}", tf_{n}, {len(i.raw)}, {{{",".join(str(b) for b in i.raw)}}}, 0x{ccrmask(lv):x}, {model.cycles(i)}, {1 if (i.mode == "idx" and i.idx["kind"] in ("accD",)) else (2 if (i.mode == "idx" and i.idx["kind"] in ("accA", "accB")) else 0)}}},\n')
    with open(os.path.join(out, 'tests_decl.inc'), 'w') as f:
        for n in range(len(tests)): f.write(f"void tf_{n}(void);\n")
    print(len(tests), "tests; unsupported:", dict(unsup))
main()
