#!/usr/bin/env python3
"""Annotated 6809 disassembly of the Yie Ar Kung-Fu program ROM ($8000-$FFFF): recursive descent from the vectors, plus optional execution
coverage from yref (-C).  usage: disasm_yie.py roms_dir out.asm [cov.bin]"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
import z6809

def load_rom(d):
    return open(os.path.join(d, '407_i08.10d'), 'rb').read() + open(os.path.join(d, '407_i07.8d'), 'rb').read()

def vectors(rom):
    names = ['res3', 'swi3', 'swi2', 'firq', 'irq', 'swi', 'nmi', 'reset']
    return {n: (rom[0x7FF0 + 2 * i] << 8) | rom[0x7FF1 + 2 * i] for i, n in enumerate(names)}

def discover(rom, extra_seeds=()):
    mem = lambda a: rom[a - 0x8000] if 0x8000 <= a < 0x10000 else 0xFF
    vec = vectors(rom)
    work = [vec[n] for n in ('reset', 'irq', 'nmi', 'firq', 'swi', 'swi2', 'swi3')] + list(extra_seeds)
    code = {}
    while work:
        a = work.pop()
        while 0x8000 <= a < 0x10000 and a not in code:
            i = z6809.decode(mem, a)
            if not i.legal: break
            code[a] = i
            if i.target is not None and 0x8000 <= i.target < 0x10000: work.append(i.target)
            if i.ends_flow: break
            a += i.length
    return code, vec, mem

if __name__ == '__main__':
    rom = load_rom(sys.argv[1])
    cov = None
    seeds = []
    if len(sys.argv) > 3:
        d = open(sys.argv[3], 'rb').read()
        cov = d[:65536]
        seeds = [a for a in range(0x8000, 0x10000) if cov[a]]
    code, vec, mem = discover(rom, seeds)
    out = []
    labels = {a: 'loc' for a in set(i.target for i in code.values() if i.target is not None)}
    for a, i in code.items():
        if i.is_call and i.target is not None: labels[i.target] = 'sub'
    for n, v in vec.items(): labels[v] = 'V_' + n
    a = 0x8000
    ncode = sum(i.length for i in code.values())
    out.append(f"; code bytes {ncode} of 32768 ({100*ncode/32768:.1f}%), {len(code)} instructions; vectors " + ' '.join(f"{n}={v:04X}" for n, v in vec.items()))
    while a < 0x10000:
        if a in code:
            i = code[a]
            if a in labels: out.append(f"{labels[a]}_{a:04X}:")
            mark = '' if cov is None else ('' if cov[a] else '   ; NOT EXECUTED')
            out.append(f"  {z6809.fmt(i)}{mark}")
            a += i.length
        else:
            s = a
            while a < 0x10000 and a not in code: a += 1
            run = bytes(mem(x) for x in range(s, a))
            out.append(f"; data {s:04X}-{a-1:04X} ({a-s} bytes)")
            for off in range(0, len(run), 16):
                out.append(f"  {s+off:04X}: .byte " + ','.join(f"${b:02X}" for b in run[off:off + 16]))
    open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
    print(out[0])
