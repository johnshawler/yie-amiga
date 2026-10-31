"""6809 instruction model for the Yie Ar Kung-Fu translator: ROM image, computed-jump tables, cycle counts (identical to ref/src/m6809.c),
code discovery and basic blocks."""
import os, sys, collections
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))
import z6809

ROM_DIR = os.path.join(os.path.dirname(__file__), '..', '..', 'roms')

def load_rom(d=ROM_DIR):
    return open(os.path.join(d, '407_i08.10d'), 'rb').read() + open(os.path.join(d, '407_i07.8d'), 'rb').read()

# Computed-jump tables (base address, number of entries).  Read from the ROM; the extent of each table follows from the index range the
# code can produce (see amiga/docs/FACTS.md, "computed jumps").  Entries whose target is not code are not listed.
TABLES = [
    (0xD562, 4),    # 809F JMP [A,X]: main program state machine
    (0xD465, 9),    # 844C JMP [A,X]
    (0xD9F0, 4),    # 8B2E JMP [A,X]
    (0xD9F8, 7),    # 8BE3 JMP [A,X]
    (0xDA66, 25),   # 8D0F / 8DE8 / 8E24 / 8EFC JMP [A,X] (overlapping sub-tables DA66..DA97)
    (0xDABA, 3),    # 9250 JMP [A,X]
    (0xE733, 3),    # 9863 JMP [A,X]
    (0xEA0E, 3),    # A680 JSR [B,X]
    (0xF908, 6),    # B895 JMP [A,U]
    (0xC95E, 8),    # C95C JMP [B,Y]
    (0xD30B, 16),   # C7EB JSR [B,Y]  NMI sound command interpreter
    (0xD32B, 16),   # C805 JSR [B,Y]
    (0xF37A, 12), (0xF3BE, 10), (0xF3F6, 12), (0xF43A, 9), (0xF46C, 7),    # AB32 JSR [D,X]  per-opponent state handlers
]
EXTRA_SEEDS = [0x8242, 0x807C]       # 8242: entry 3 of the 8B2E table (inside an otherwise unreferenced area); 807C: return address pushed before JMP [A,X] at 809F

def w16(rom, a): return (rom[a - 0x8000] << 8) | rom[a - 0x8000 + 1]

def vectors(rom):
    names = ['res3', 'swi3', 'swi2', 'firq', 'irq', 'swi', 'nmi', 'reset']
    return {n: w16(rom, 0xFFF0 + 2 * i) for i, n in enumerate(names)}

def table_targets(rom):
    out = []
    for base, n in TABLES:
        for k in range(n): out.append(w16(rom, base + 2 * k))
    return out

def indexed_extra(i):
    """extra cycles of an indexed operand (same as indexed() in m6809.c)"""
    info = i.idx; k = info['kind']
    if k == 'off5': e = 1
    else:
        e = {'inc1': 2, 'inc2': 3, 'dec1': 2, 'dec2': 3, 'zero': 0, 'accB': 1, 'accA': 1, 'off8': 1, 'off16': 4, 'accD': 4, 'pc8': 1, 'pc16': 5, 'extind': 2}[k]
    if (info['pb'] & 0x80) and (info['pb'] & 0x10): e += 3
    return e

def cycles(i, taken=True):
    """cycles of instruction i (for a long conditional branch `taken` selects 6 or 5)"""
    m, md = i.mnem, i.mode
    ex = indexed_extra(i) if md == 'idx' else 0
    if m.startswith('LB') and m not in ('LBRA', 'LBSR'): return 6 if taken else 5
    if m == 'LBRA': return 5
    if m == 'LBSR': return 9
    if md == 'rel8' and m not in ('BSR',): return 3
    if m == 'BSR': return 7
    if m == 'JSR': return {'dir': 7, 'idx': 7 + ex, 'ext': 8}[md]
    if m == 'JMP': return {'dir': 3, 'idx': 3 + ex, 'ext': 4}[md]
    if m == 'NOP': return 2
    if m == 'SYNC': return 4
    if m == 'DAA': return 2
    if m in ('ORCC', 'ANDCC'): return 3
    if m == 'SEX': return 2
    if m == 'EXG': return 8
    if m == 'TFR': return 6
    if m in ('LEAX', 'LEAY', 'LEAS', 'LEAU'): return 4 + ex
    if m in ('PSHS', 'PSHU', 'PULS', 'PULU'):
        mask = i.raw[-1]; n = 5
        for bit in range(8): n += (2 if bit >= 4 else 1) if mask & (1 << bit) else 0
        return n
    if m == 'RTS': return 5
    if m == 'ABX': return 3
    if m == 'RTI': return 15
    if m == 'MUL': return 11
    if m in ('SWI', 'SWI2', 'SWI3'): return 19 if m == 'SWI' else 20
    if m == 'CWAI': return 20
    # RMW
    rmw = ('NEG', 'COM', 'LSR', 'ROR', 'ASR', 'ASL', 'LSL', 'ROL', 'DEC', 'INC', 'TST', 'CLR')
    if m in rmw: return {'dir': 6, 'idx': 6 + ex, 'ext': 7}[md]
    if m[:-1] in rmw and m[-1] in 'AB': return 2
    # 16-bit and 8-bit ALU / load / store
    two = m in ('CMPD', 'CMPY', 'CMPU', 'CMPS') or i.op > 0xFF
    sixteen_cmp = m in ('SUBD', 'ADDD', 'CMPX', 'CMPY', 'CMPU', 'CMPS', 'CMPD')
    sixteen_ld = m in ('LDD', 'LDX', 'LDY', 'LDU', 'LDS', 'STD', 'STX', 'STY', 'STU', 'STS')
    if i.op > 0xFF:     # prefixed
        if sixteen_cmp: return {'imm16': 5, 'dir': 7, 'idx': 7 + ex, 'ext': 8}[md]
        return {'imm16': 4, 'dir': 6, 'idx': 6 + ex, 'ext': 7}[md]
    if sixteen_cmp: return {'imm16': 4, 'dir': 6, 'idx': 6 + ex, 'ext': 7}[md]
    if sixteen_ld: return {'imm16': 3, 'dir': 5, 'idx': 5 + ex, 'ext': 6}[md]
    return {'imm8': 2, 'dir': 4, 'idx': 4 + ex, 'ext': 5}[md]

def decode_at(rom, a):
    mem = lambda x: rom[x - 0x8000] if 0x8000 <= x < 0x10000 else 0xFF
    return z6809.decode(mem, a, 0)

# instructions the translator hands to the interpreter (they change the interrupt mask or are never used by this program)
def interp_only(i):
    m = i.mnem
    if m in ('RTI', 'ANDCC', 'ORCC', 'SYNC', 'CWAI', 'SWI', 'SWI2', 'SWI3', 'EXG'): return True
    if m in ('PULS', 'PULU') and (i.raw[-1] & 0x01): return True        # restores CC
    if m in ('TFR',) and 'CC' in i.operand: return True
    return False

def discover(rom, seeds=()):
    vec = vectors(rom)
    work = [vec[n] for n in ('reset', 'irq', 'nmi')] + list(table_targets(rom)) + list(EXTRA_SEEDS) + list(seeds)
    code = {}
    while work:
        a = work.pop()
        while 0x8000 <= a < 0x10000 and a not in code:
            i = decode_at(rom, a)
            if not i.legal: break
            code[a] = i
            if i.target is not None and 0x8000 <= i.target < 0x10000: work.append(i.target)
            if i.ends_flow or interp_only(i) and i.mnem == 'RTI': break
            a += i.length
    return code, vec

K_MAX = 64      # a block never exceeds this many cycles (so the budget guard in rt_refill is safe)

def leaders_of(code, vec, rom):
    L = set()
    for n in ('reset', 'irq', 'nmi'): L.add(vec[n])
    for t in table_targets(rom): L.add(t)
    for s in EXTRA_SEEDS: L.add(s)
    for a, i in code.items():
        nxt = a + i.length
        if i.target is not None: L.add(i.target)
        is_cond = i.is_branch and i.mnem not in ('BRA', 'LBRA', 'BSR', 'LBSR')
        if is_cond or i.is_call or i.ends_flow or interp_only(i):
            if nxt in code: L.add(nxt)
        if i.mnem in ('JMP', 'JSR', 'RTS', 'RTI', 'PULS') and nxt in code: L.add(nxt)
    return L

def blocks_of(code, leaders):
    """list of blocks: each a list of instructions starting at a leader, ending at a terminator, the next leader or the K_MAX cycle limit.
    Extra leaders created by the cycle limit are added to `leaders`."""
    blocks = []
    for start in sorted(a for a in leaders if a in code):
        pass
    done = set()
    work = sorted(a for a in leaders if a in code)
    out = []
    i = 0
    while i < len(work):
        a = work[i]; i += 1
        if a in done: continue
        blk = []; cyc = 0
        while True:
            ins = code[a]
            c = cycles(ins)
            if blk and cyc + c > K_MAX:
                leaders.add(a); work.append(a); work.sort(); break
            blk.append(ins); cyc += c; done.add(a)
            nxt = a + ins.length
            term = ins.ends_flow or ins.is_branch or ins.is_call or interp_only(ins)
            if term or nxt not in code: break
            if nxt in leaders: break
            a = nxt
        out.append(blk)
    out.sort(key=lambda b: b[0].addr)
    return out
