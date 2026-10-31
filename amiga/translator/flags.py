"""6809 condition-code semantics for the translator: which flags each instruction reads / writes.
Flags are bits in a small mask: N Z V C H."""
N, Z, V, C, H = 1, 2, 4, 8, 16
ALL = N | Z | V | C | H
NZVC = N | Z | V | C

COND_READS = {
    'BRA': 0, 'BRN': 0, 'BHI': C | Z, 'BLS': C | Z, 'BCC': C, 'BCS': C, 'BNE': Z, 'BEQ': Z, 'BVC': V, 'BVS': V,
    'BPL': N, 'BMI': N, 'BGE': N | V, 'BLT': N | V, 'BGT': N | V | Z, 'BLE': N | V | Z,
}
def cond_reads(m):
    b = m[1:] if m.startswith('LB') and m not in ('LBRA', 'LBSR') else m
    if m.startswith('LB') and m not in ('LBRA', 'LBSR'): b = 'B' + m[2:]
    return COND_READS.get(b, 0)

# (reads, writes) ; flags not in writes are "unaffected"
def effect(i):
    """returns (reads, writes) masks for decoded instruction i"""
    m = i.mnem
    base = m
    # strip register suffix for A/B/D variants
    if m in COND_READS or (m.startswith('LB') and m not in ('LBRA', 'LBSR')):
        return cond_reads(m), 0
    if m in ('BRA', 'LBRA', 'BSR', 'LBSR', 'JMP', 'JSR', 'NOP', 'SYNC', 'RTS', 'ABX', 'SWI', 'SWI2', 'SWI3'):
        return 0, 0
    if m in ('RTI',): return 0, ALL
    if m in ('LDA', 'LDB', 'LDD', 'LDX', 'LDY', 'LDU', 'LDS', 'STA', 'STB', 'STD', 'STX', 'STY', 'STU', 'STS'):
        return 0, N | Z | V
    if m in ('TSTA', 'TSTB', 'TST'): return 0, N | Z | V
    if m in ('ANDA', 'ANDB', 'ORA', 'ORB', 'EORA', 'EORB', 'BITA', 'BITB'): return 0, N | Z | V
    if m in ('CLRA', 'CLRB', 'CLR'): return 0, NZVC
    if m in ('ADDA', 'ADDB'): return 0, ALL
    if m in ('ADCA', 'ADCB'): return C, ALL
    if m in ('SUBA', 'SUBB', 'CMPA', 'CMPB', 'NEGA', 'NEGB', 'NEG'): return 0, NZVC
    if m in ('SBCA', 'SBCB'): return C, NZVC
    if m in ('ADDD', 'SUBD', 'CMPD', 'CMPX', 'CMPY', 'CMPU', 'CMPS'): return 0, NZVC
    if m in ('INCA', 'INCB', 'INC', 'DECA', 'DECB', 'DEC'): return 0, N | Z | V
    if m in ('COMA', 'COMB', 'COM'): return 0, NZVC
    if m in ('LSRA', 'LSRB', 'LSR', 'ASRA', 'ASRB', 'ASR'): return 0, N | Z | C
    if m in ('ASLA', 'ASLB', 'ASL', 'LSLA', 'LSLB', 'LSL'): return 0, NZVC
    if m in ('ROLA', 'ROLB', 'ROL'): return C, NZVC
    if m in ('RORA', 'RORB', 'ROR'): return C, N | Z | C
    if m in ('LEAX', 'LEAY'): return 0, Z
    if m in ('LEAS', 'LEAU'): return 0, 0
    if m == 'MUL': return 0, Z | C
    if m == 'SEX': return 0, N | Z
    if m == 'DAA': return C | H, N | Z | V | C
    if m in ('PSHS', 'PSHU'):
        return (ALL if i.raw[-1] & 1 else 0), 0
    if m in ('PULS', 'PULU'):
        return 0, (ALL if i.raw[-1] & 1 else 0)
    if m in ('TFR', 'EXG'):
        r = i.operand.split(',')
        rd = ALL if 'CC' in r else 0
        wr = ALL if 'CC' in r else 0
        if m == 'TFR': rd = ALL if r[0] == 'CC' else 0; wr = ALL if r[1] == 'CC' else 0
        return rd, wr
    if m == 'ANDCC':
        mask = i.raw[-1]; wr = 0
        for bit, f in ((0, C), (1, V), (2, Z), (3, N), (5, H)):
            if not (mask & (1 << bit)): wr |= f      # cleared bits are written
        return 0, wr
    if m == 'ORCC':
        mask = i.raw[-1]; wr = 0
        for bit, f in ((0, C), (1, V), (2, Z), (3, N), (5, H)):
            if mask & (1 << bit): wr |= f
        return 0, wr
    if m == 'CWAI': return 0, ALL
    return ALL, ALL    # unknown: be conservative
