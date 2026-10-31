#!/usr/bin/env python3
"""MC6809 instruction decoder shared by the disassembler and the 6809->68000 translator.
decode(mem, addr) -> Insn.  `mem` is a callable addr->byte (0..255)."""
from dataclasses import dataclass, field

# ---- opcode tables: opcode -> (mnemonic, mode) ; modes: inh imm8 imm16 dir idx ext rel8 rel16 reg stk
RMW = {0x0: 'NEG', 0x3: 'COM', 0x4: 'LSR', 0x6: 'ROR', 0x7: 'ASR', 0x8: 'ASL', 0x9: 'ROL', 0xA: 'DEC', 0xC: 'INC', 0xD: 'TST', 0xE: 'JMP', 0xF: 'CLR'}
BR = ['BRA', 'BRN', 'BHI', 'BLS', 'BCC', 'BCS', 'BNE', 'BEQ', 'BVC', 'BVS', 'BPL', 'BMI', 'BGE', 'BLT', 'BGT', 'BLE']
A_OPS = {0x0: 'SUBA', 0x1: 'CMPA', 0x2: 'SBCA', 0x3: 'SUBD', 0x4: 'ANDA', 0x5: 'BITA', 0x6: 'LDA', 0x7: 'STA', 0x8: 'EORA', 0x9: 'ADCA',
         0xA: 'ORA', 0xB: 'ADDA', 0xC: 'CMPX', 0xD: 'JSR', 0xE: 'LDX', 0xF: 'STX'}
B_OPS = {0x0: 'SUBB', 0x1: 'CMPB', 0x2: 'SBCB', 0x3: 'ADDD', 0x4: 'ANDB', 0x5: 'BITB', 0x6: 'LDB', 0x7: 'STB', 0x8: 'EORB', 0x9: 'ADCB',
         0xA: 'ORB', 0xB: 'ADDB', 0xC: 'LDD', 0xD: 'STD', 0xE: 'LDU', 0xF: 'STU'}
P2 = {0x83: 'CMPD', 0x8C: 'CMPY', 0x8E: 'LDY', 0xCE: 'LDS'}
P3 = {0x83: 'CMPU', 0x8C: 'CMPS'}

def _build():
    t = {}
    for hi in (0x0, 0x6, 0x7):
        for lo, m in RMW.items():
            t[hi * 16 + lo] = (m, {0: 'dir', 6: 'idx', 7: 'ext'}[hi])
    for hi, reg in ((0x4, 'A'), (0x5, 'B')):
        for lo, m in RMW.items():
            if m != 'JMP': t[hi * 16 + lo] = (m + reg, 'inh')
    t[0x12] = ('NOP', 'inh'); t[0x13] = ('SYNC', 'inh'); t[0x16] = ('LBRA', 'rel16'); t[0x17] = ('LBSR', 'rel16')
    t[0x19] = ('DAA', 'inh'); t[0x1A] = ('ORCC', 'imm8'); t[0x1C] = ('ANDCC', 'imm8'); t[0x1D] = ('SEX', 'inh')
    t[0x1E] = ('EXG', 'reg'); t[0x1F] = ('TFR', 'reg')
    for i, m in enumerate(BR): t[0x20 + i] = (m, 'rel8')
    t[0x30] = ('LEAX', 'idx'); t[0x31] = ('LEAY', 'idx'); t[0x32] = ('LEAS', 'idx'); t[0x33] = ('LEAU', 'idx')
    t[0x34] = ('PSHS', 'stk'); t[0x35] = ('PULS', 'stk'); t[0x36] = ('PSHU', 'stk'); t[0x37] = ('PULU', 'stk')
    t[0x39] = ('RTS', 'inh'); t[0x3A] = ('ABX', 'inh'); t[0x3B] = ('RTI', 'inh'); t[0x3C] = ('CWAI', 'imm8'); t[0x3D] = ('MUL', 'inh'); t[0x3F] = ('SWI', 'inh')
    for base, ops in ((0x80, A_OPS), (0xC0, B_OPS)):
        for hi, mode in ((0, 'imm'), (1, 'dir'), (2, 'idx'), (3, 'ext')):
            for lo, m in ops.items():
                op = base + hi * 16 + lo
                md = mode
                if mode == 'imm':
                    if m in ('STA', 'STB', 'STX', 'STD', 'STU', 'JSR'): continue
                    md = 'imm16' if m in ('SUBD', 'ADDD', 'CMPX', 'LDX', 'LDD', 'LDU') else 'imm8'
                t[op] = (m, md)
    t[0x8D] = ('BSR', 'rel8')
    return t
T1 = _build()
T2 = {}
# prefixed (0x10/0x11) tables
for i in range(0x21, 0x30): T2[0x1000 + i] = (BR[i - 0x20].replace('B', 'LB', 1), 'rel16')
T2[0x103F] = ('SWI2', 'inh'); T2[0x113F] = ('SWI3', 'inh')
for hi, mode in ((0, 'imm16'), (1, 'dir'), (2, 'idx'), (3, 'ext')):
    b = 0x80 + hi * 16
    T2[0x1000 + b + 3] = ('CMPD', mode); T2[0x1000 + b + 0xC] = ('CMPY', mode); T2[0x1000 + b + 0xE] = ('LDY', mode)
    T2[0x1100 + b + 3] = ('CMPU', mode); T2[0x1100 + b + 0xC] = ('CMPS', mode)
    if hi: T2[0x1000 + b + 0xF] = ('STY', mode)
for hi, mode in ((0, 'imm16'), (1, 'dir'), (2, 'idx'), (3, 'ext')):
    b = 0xC0 + hi * 16
    T2[0x1000 + b + 0xE] = ('LDS', mode)
    if hi: T2[0x1000 + b + 0xF] = ('STS', mode)

REGS16 = {0: 'D', 1: 'X', 2: 'Y', 3: 'U', 4: 'S', 5: 'PC'}
REGS8 = {8: 'A', 9: 'B', 10: 'CC', 11: 'DP'}
def regname(n): return REGS16.get(n) or REGS8.get(n) or '?'
IDXREG = ['X', 'Y', 'U', 'S']

@dataclass
class Insn:
    addr: int
    length: int
    op: int            # opcode (0x10xx / 0x11xx for prefixed)
    mnem: str
    mode: str
    raw: bytes
    operand: str = ''
    target: int = None          # branch/jump/call target if static
    ea: int = None              # static absolute address (ext/dir with known DP) if any
    idx: dict = field(default_factory=dict)   # decoded indexed postbyte
    legal: bool = True

    @property
    def is_branch(self): return self.mode in ('rel8', 'rel16')
    @property
    def ends_flow(self):
        """True if execution never falls through to addr+length."""
        m = self.mnem
        if m in ('BRA', 'LBRA', 'RTS', 'RTI', 'JMP', 'SWI', 'SWI2', 'SWI3', 'CWAI'): return True
        if m in ('PULS', 'PULU') and (self.raw[-1] & 0x80): return True
        if m in ('EXG', 'TFR') and self.raw[-1] & 0x0F == 5: return True   # to PC
        return False
    @property
    def is_call(self): return self.mnem in ('JSR', 'BSR', 'LBSR')

def decode_indexed(mem, a):
    """returns (postbyte_len_total, text, info) for an indexed postbyte at address a"""
    pb = mem(a)
    r = IDXREG[(pb >> 5) & 3]
    info = {'pb': pb, 'reg': r}
    if not (pb & 0x80):
        off = pb & 0x1F
        if off & 0x10: off -= 32
        info.update(kind='off5', off=off)
        return 1, f"{off},{r}", info
    ind = bool(pb & 0x10)
    k = pb & 0x0F
    def wrap(s): return f"[{s}]" if ind else s
    if k == 0x0: info.update(kind='inc1'); return 1, wrap(f",{r}+"), info
    if k == 0x1: info.update(kind='inc2'); return 1, wrap(f",{r}++"), info
    if k == 0x2: info.update(kind='dec1'); return 1, wrap(f",-{r}"), info
    if k == 0x3: info.update(kind='dec2'); return 1, wrap(f",--{r}"), info
    if k == 0x4: info.update(kind='zero'); return 1, wrap(f",{r}"), info
    if k == 0x5: info.update(kind='accB'); return 1, wrap(f"B,{r}"), info
    if k == 0x6: info.update(kind='accA'); return 1, wrap(f"A,{r}"), info
    if k == 0x8:
        o = mem(a + 1); o = o - 256 if o & 0x80 else o
        info.update(kind='off8', off=o); return 2, wrap(f"{o},{r}"), info
    if k == 0x9:
        o = (mem(a + 1) << 8) | mem(a + 2); o = o - 65536 if o & 0x8000 else o
        info.update(kind='off16', off=o); return 3, wrap(f"{o},{r}"), info
    if k == 0xB: info.update(kind='accD'); return 1, wrap(f"D,{r}"), info
    if k == 0xC:
        o = mem(a + 1); o = o - 256 if o & 0x80 else o
        info.update(kind='pc8', off=o); return 2, wrap(f"{o},PC"), info
    if k == 0xD:
        o = (mem(a + 1) << 8) | mem(a + 2); o = o - 65536 if o & 0x8000 else o
        info.update(kind='pc16', off=o); return 3, wrap(f"{o},PC"), info
    if k == 0xF and ind:
        o = (mem(a + 1) << 8) | mem(a + 2)
        info.update(kind='extind', addr=o); return 3, f"[${o:04X}]", info
    info.update(kind='illegal')
    return 1, "?idx", info

def decode(mem, addr, dp=0):
    op = mem(addr)
    n = 1
    code = op
    if op in (0x10, 0x11):
        code = (op << 8) | mem(addr + 1); n = 2
        ent = T2.get(code)
    else:
        ent = T1.get(op)
    if ent is None:
        raw = bytes(mem(addr + i) & 0xFF for i in range(n))
        return Insn(addr, n, code, 'FCB', 'illegal', raw, f"${op:02X}", legal=False)
    mnem, mode = ent
    a = addr + n
    operand = ''; target = None; ea = None; idxinfo = {}
    if mode == 'inh': pass
    elif mode == 'imm8': operand = f"#${mem(a):02X}"; n += 1
    elif mode == 'imm16': operand = f"#${(mem(a) << 8) | mem(a + 1):04X}"; n += 2
    elif mode == 'dir': operand = f"<${mem(a):02X}"; ea = (dp << 8) | mem(a); n += 1
    elif mode == 'ext': ea = (mem(a) << 8) | mem(a + 1); operand = f">${ea:04X}"; n += 2
    elif mode == 'rel8':
        o = mem(a); o = o - 256 if o & 0x80 else o
        n += 1; target = (addr + n + o) & 0xFFFF; operand = f"${target:04X}"
    elif mode == 'rel16':
        o = (mem(a) << 8) | mem(a + 1); o = o - 65536 if o & 0x8000 else o
        n += 2; target = (addr + n + o) & 0xFFFF; operand = f"${target:04X}"
    elif mode == 'idx':
        l, operand, idxinfo = decode_indexed(mem, a); n += l
        if idxinfo.get('kind') in ('pc8', 'pc16'):
            ea = (addr + n + idxinfo['off']) & 0xFFFF
        if idxinfo.get('kind') == 'extind': ea = idxinfo['addr']
        if idxinfo.get('kind') == 'illegal': pass
    elif mode == 'reg':
        pb = mem(a); operand = f"{regname(pb >> 4)},{regname(pb & 15)}"; n += 1
    elif mode == 'stk':
        pb = mem(a); n += 1
        names = ['CC', 'A', 'B', 'DP', 'X', 'Y', 'U' if mnem in ('PSHS', 'PULS') else 'S', 'PC']
        operand = ','.join(nm for i, nm in enumerate(names) if pb & (1 << i))
    raw = bytes(mem(addr + i) & 0xFF for i in range(n))
    if mnem in ('JMP', 'JSR') and (mode in ('dir', 'ext') or (mode == 'idx' and idxinfo.get('kind') in ('pc8', 'pc16'))):
        target = ea            # static control-flow target (indirect [addr] forms stay dynamic)
    if mnem in ('JMP', 'JSR') and mode == 'idx' and idxinfo.get('kind') == 'extind' and ea is not None and ea >= 0x8000:
        target = (mem(ea) << 8) | mem(ea + 1)          # pointer lives in ROM: the target is static
    ins = Insn(addr, n, code, mnem, mode, raw, operand, target, ea, idxinfo)
    if mode == 'idx' and idxinfo.get('kind') == 'illegal': ins.legal = False
    return ins

def fmt(ins):
    return f"{ins.addr:04X}: {ins.raw.hex().upper():<12} {ins.mnem:<6} {ins.operand}".rstrip()

if __name__ == '__main__':
    import sys
    data = open(sys.argv[1], 'rb').read(); base = int(sys.argv[2], 16)
    a = base; end = base + len(data)
    mem = lambda x: data[x - base] if base <= x < end else 0xFF
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    for _ in range(n):
        i = decode(mem, a); print(fmt(i)); a += i.length
