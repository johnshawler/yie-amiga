"""6809 -> 68020 instruction templates for the Yie Ar Kung-Fu translator.

Register map:  A=%d0.b  B=%d1.b (upper bits unspecified)   X=%d2.w Y=%d3.w U=%d4.w (upper word always 0)   S=%a4 (host pointer into the 64 KB image)
               %a1 = image base, %a5 = image + $8000 (static 16-bit displacements), %a2 = image + DP*256, %a6 = VM context,
               %a3 = cycles left until the next scheduler event, %d7.w = block budget, %d5/%d6/%a0 scratch.
Condition codes: 6809 N Z V C = 68k N Z V C (same bit positions, 68k X is scratch); H E F I live in vm_cc (bits 5,7,6,4).
Every flag the 6809 defines is produced exactly where it is live (flags are always live at the end of a block: the block guard may hand
the state to the interpreter there).  Native code may use only %d5, %a0 and the native stack as scratch where a flag-preserving
wrapper (which owns %d6) encloses it.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import flags as F

N, Z, V, C = F.N, F.Z, F.V, F.C
NZV = N | Z | V
NZVC = N | Z | V | C
CCRBIT = {N: 8, Z: 4, V: 2, C: 1}
def ccr_mask(m): return sum(b for f, b in CCRBIT.items() if m & f)

class Unsupported(Exception): pass

class Mop:
    """memory operand (after its setup code has been emitted)"""
    def __init__(self, kind, fmt=None, addr=None):
        self.kind = kind          # 'mem' plain image memory | 'rom' static ROM | 'io' static non-memory address
        self.fmt = fmt; self.addr = addr
    def b(self, k=0): return self.fmt(k)

class Tr:
    """translates single instructions; the caller sets .blk_total / .blk_off (cycle position of the instruction inside its block)"""
    def __init__(self, check=False):
        self.out = []; self.uid = 0; self.check = check
        self.blk_total = 0; self.blk_off = 0
        self.cur = None

    def e(self, s): self.out.append('\t' + s)
    def lab(self):
        self.uid += 1; return f".Lt{self.uid}"
    def reg16(self, name): return {'X': '%d2', 'Y': '%d3', 'U': '%d4'}[name]
    def reg8(self, name): return {'A': '%d0', 'B': '%d1'}[name]

    # ------------------------------------------------------------------ flag preservation
    def pres_begin(self, mask):
        """native code that follows clobbers the flags in `mask` that the 6809 instruction leaves unchanged: save them"""
        mask &= NZVC
        if not mask: return None
        if mask == C: self.e("scs %d6"); return ('c',)
        self.e("move %ccr,%d6"); return ('m', mask)
    def pres_end(self, tok):
        if tok is None: return
        e = self.e
        if tok[0] == 'c':
            e("move %ccr,%d5"); e("and.b #0xFE,%d5"); e("sub.b %d6,%d5"); e("move %d5,%ccr")
        elif tok[1] == NZVC:
            e("move %d6,%ccr")
        else:
            m = ccr_mask(tok[1])
            e("move %ccr,%d5"); e(f"and.b #{0x1F & ~m},%d5"); e(f"and.b #{m},%d6"); e("or.b %d6,%d5"); e("move %d5,%ccr")

    # ------------------------------------------------------------------ I/O helpers
    def io_delay(self): return self.blk_total - self.blk_off
    def io_read(self, addr, dst):
        self.e(f"movea.w #{addr},%a0"); self.e("jbsr rt_io_rd"); self.e(f".word {self.io_delay()}")
        if dst != '%d5': self.e(f"move.b %d5,{dst}")
    def io_write(self, addr, src):
        if src != '%d5': self.e(f"move.b {src},%d5")
        self.e(f"movea.w #{addr},%a0"); self.e("jbsr rt_io_wr"); self.e(f".word {self.io_delay()}")

    # ------------------------------------------------------------------ operands
    def mop(self, i, size):
        md = i.mode
        if md == 'dir':
            off = i.raw[-1]
            return Mop('mem', lambda k, off=off: f"{off + k}(%a2)")
        if md == 'ext': return self.static_mop(i.ea)
        if md == 'idx': return self.indexed_mop(i, size)
        raise Unsupported(f"operand mode {md} for {i.mnem}")

    def static_mop(self, a):
        if a >= 0x8000: return Mop('rom', lambda k, a=a: f"{a + k - 0x8000}(%a5)", addr=a)
        if 0x5000 <= a < 0x6000: return Mop('mem', lambda k, a=a: f"{a + k - 0x8000}(%a5)", addr=a)
        return Mop('io', addr=a)

    def _dyn(self, base, off):
        """operand `off(%a1,base.l)`; with check builds the effective address is verified first"""
        if self.check:
            self.e(f"lea {off}(%a1,{base}.l),%a0"); self.e("jbsr rt_chk"); self.e(f".word 0x{self.cur.addr:04X}")
        return Mop('mem', lambda k, base=base, off=off: f"{off + k}(%a1,{base}.l)" if off + k else f"(%a1,{base}.l)")

    def idx_base(self, i):
        """emit the setup of an indexed operand and return (base, off, indirect): the address (before indirection) is image[base.l + off]"""
        info = i.idx; kind = info['kind']; reg = info['reg']
        indirect = bool(info['pb'] & 0x10) and kind != 'off5'
        e = self.e
        r = self.reg16(reg)
        base = r; off = 0
        if kind in ('inc1', 'inc2'):
            n = 1 if kind == 'inc1' else 2
            e(f"addq.w #{n},{r}"); off = -n               # access at X-n after the increment (the access instruction sets the flags last)
        elif kind in ('dec1', 'dec2'):
            e(f"subq.w #{1 if kind == 'dec1' else 2},{r}")
        elif kind == 'zero': pass
        elif kind in ('off5', 'off8', 'off16'):
            off = info['off']
            if not (-128 <= off <= 127):
                e(f"move.w {r},%d5"); e(f"add.w #{off & 0xFFFF},%d5"); e("and.l #0xFFFF,%d5")
                base = '%d5'; off = 0
        elif kind in ('accA', 'accB'):
            e(f"move.b {'%d0' if kind == 'accA' else '%d1'},%d5"); e("extb.l %d5"); e(f"add.l {r},%d5"); base = '%d5'
        elif kind == 'accD':
            e("move.b %d0,%d5"); e("lsl.w #8,%d5"); e("move.b %d1,%d5"); e("ext.l %d5"); e(f"add.l {r},%d5"); base = '%d5'
        else: raise Unsupported(f"indexed kind {kind}")
        return base, off, indirect

    def indexed_mop(self, i, size):
        info = i.idx; kind = info['kind']; reg = info['reg']
        e = self.e
        if kind in ('pc8', 'pc16'):
            if bool(info['pb'] & 0x10): raise Unsupported("indirect pc-relative")
            return self.static_mop(i.ea)
        if kind == 'extind':
            m = self.static_mop(info['addr'])
            if m.kind == 'io': raise Unsupported("indirect via I/O")
            e("moveq #0,%d5"); e(f"move.b {m.b(0)},%d5"); e("lsl.w #8,%d5"); e(f"move.b {m.b(1)},%d5")
            return self._dyn('%d5', 0)
        if reg == 'S':
            if bool(info['pb'] & 0x10) and kind != 'off5': raise Unsupported("indirect via S")
            off = info.get('off', 0) if kind != 'zero' else 0
            if kind not in ('off5', 'off8', 'off16', 'zero'): raise Unsupported(f"S-indexed kind {kind}")
            return Mop('mem', lambda k, off=off: f"{off + k}(%a4)" if off + k else "(%a4)")
        base, off, indirect = self.idx_base(i)
        if indirect:
            # operand address = word stored at base+off
            if base == '%d5':
                e("movea.l %d5,%a0"); e("moveq #0,%d5")
                e(f"move.b {off}(%a1,%a0.l),%d5"); e("lsl.w #8,%d5"); e(f"move.b {off + 1}(%a1,%a0.l),%d5")
            else:
                e(f"move.b {off}(%a1,{base}.l),%d5"); e("lsl.w #8,%d5"); e(f"move.b {off + 1}(%a1,{base}.l),%d5")
                # d5 upper bits are unspecified here: clear them
                e("and.l #0xFFFF,%d5")
            return self._dyn('%d5', 0)
        return self._dyn(base, off)

    def ea_to_a0(self, i):
        """effective address *value* of a jump/call operand (JMP/JSR with idx/dir/ext): returns code that leaves it sign-extended-16 in %a0"""
        e = self.e
        md = i.mode
        if md == 'ext': e(f"movea.w #{i.ea},%a0"); return
        if md == 'dir': raise Unsupported("JMP/JSR direct page")
        info = i.idx; kind = info['kind']
        if kind in ('pc8', 'pc16') and not (info['pb'] & 0x10): e(f"movea.w #{i.ea},%a0"); return
        if info['reg'] == 'S': raise Unsupported("jump via S")
        base, off, indirect = self.idx_base(i)
        if indirect:
            # target = word stored at base+off
            e(f"movea.w {off}(%a1,{base}.l),%a0")
        else:
            # target = base + off
            e(f"movea.w {base},%a0")
            if off: e(f"lea {off}(%a0),%a0")

    # -------- byte / word access through an operand
    def rd_b(self, m, dst):
        if m.kind == 'io': self.io_read(m.addr, dst)
        else: self.e(f"move.b {m.b(0)},{dst}")
    def wr_b(self, m, src):
        if m.kind == 'io': self.io_write(m.addr, src)
        elif m.kind == 'rom': self.e(f"tst.b {src}")          # writes to ROM have no effect; the flags follow the value
        else: self.e(f"move.b {src},{m.b(0)}")

    # ------------------------------------------------------------------ translate one instruction
    def tr(self, i, live):
        """append the code of instruction i; `live` = 6809 flags (N Z V C) that are needed afterwards.  Control-flow instructions are handled by the block driver."""
        self.cur = i
        fn = getattr(self, 't_' + i.mnem, None)
        if fn is None: raise Unsupported(f"no template for {i.mnem}")
        fn(i, live)

    # ---- loads / stores (N Z V=0; C unchanged)
    def _ld8(self, i, live, r):
        tok = self.pres_begin(live & C)
        if i.mode == 'imm8':
            v = int(i.operand[2:], 16); self.e(f"moveq #{v - 256 if v & 0x80 else v},{r}")
        else:
            m = self.mop(i, 1); self.rd_b(m, r)
        self.pres_end(tok)
    def t_LDA(self, i, live): self._ld8(i, live, '%d0')
    def t_LDB(self, i, live): self._ld8(i, live, '%d1')
    def _st8(self, i, live, r):
        tok = self.pres_begin(live & C)
        m = self.mop(i, 1); self.wr_b(m, r)
        if m.kind == 'io': self.e(f"tst.b {r}")
        self.pres_end(tok)
    def t_STA(self, i, live): self._st8(i, live, '%d0')
    def t_STB(self, i, live): self._st8(i, live, '%d1')

    def _ld16(self, i, live, r):
        tok = self.pres_begin(live & C)
        if i.mode == 'imm16':
            v = int(i.operand[2:], 16)
            self.e(f"moveq #{v},{r}") if v <= 0x7F else self.e(f"move.w #{v},{r}")
        else:
            m = self.mop(i, 2)
            if m.kind == 'io': raise Unsupported("16-bit read of I/O")
            if self.dst_is_index(i, r): raise Unsupported("load into the auto-incremented register")
            self.e(f"move.w {m.b(0)},{r}")
        self.pres_end(tok)
    def dst_is_index(self, i, r):
        return i.mode == 'idx' and i.idx['kind'] in ('inc1', 'inc2', 'dec1', 'dec2') and self.reg16(i.idx['reg']) == r
    def t_LDX(self, i, live): self._ld16(i, live, '%d2')
    def t_LDY(self, i, live): self._ld16(i, live, '%d3')
    def t_LDU(self, i, live): self._ld16(i, live, '%d4')
    def t_LDS(self, i, live):
        if i.mode != 'imm16': raise Unsupported("LDS non-immediate")
        v = int(i.operand[2:], 16)
        tok = self.pres_begin(live & C)
        self.e(f"lea {v - 0x8000}(%a5),%a4"); self.e(f"move.w #{v},%d5")        # N,Z from the value
        self.pres_end(tok)
    def _st16(self, i, live, r):
        if self.dst_is_index(i, r): raise Unsupported("store of the auto-incremented register")
        tok = self.pres_begin(live & C)
        m = self.mop(i, 2)
        if m.kind == 'io': raise Unsupported("16-bit write to I/O")
        if m.kind == 'rom': self.e(f"tst.w {r}")
        else: self.e(f"move.w {r},{m.b(0)}")
        self.pres_end(tok)
    def t_STX(self, i, live): self._st16(i, live, '%d2')
    def t_STY(self, i, live): self._st16(i, live, '%d3')
    def t_STU(self, i, live): self._st16(i, live, '%d4')

    def t_LDD(self, i, live):
        tok = self.pres_begin(live & C)
        if i.mode == 'imm16':
            v = int(i.operand[2:], 16)
            self.e(f"move.b #{v >> 8},%d0"); self.e(f"move.b #{v & 0xFF},%d1"); self.e(f"move.w #{v},%d5")
        else:
            m = self.mop(i, 2)
            if m.kind == 'io': raise Unsupported("16-bit read of I/O")
            if i.mode == 'idx' and i.idx['kind'] in ('inc1', 'inc2', 'dec1', 'dec2') and False: pass
            self.e(f"move.b {m.b(0)},%d0"); self.e(f"move.b {m.b(1)},%d1"); self.e(f"move.w {m.b(0)},%d5")
        self.pres_end(tok)
    def t_STD(self, i, live):
        tok = self.pres_begin(live & C)
        m = self.mop(i, 2)
        if m.kind == 'io': raise Unsupported("16-bit write to I/O")
        self.e("move.b %d0,%d5"); self.e("lsl.w #8,%d5"); self.e("move.b %d1,%d5")
        if m.kind == 'rom': self.e("tst.w %d5")
        else: self.e(f"move.w %d5,{m.b(0)}")
        self.pres_end(tok)

    # ---- 8-bit ALU with a source operand
    def src8(self, i, allow_imm=True):
        """returns an operand string usable as a source: '#imm', memory operand, or %d5 (read through I/O)"""
        if i.mode == 'imm8':
            return f"#{int(i.operand[2:], 16)}"
        m = self.mop(i, 1)
        if m.kind == 'io':
            self.io_read(m.addr, '%d5'); return '%d5'
        return m.b(0)

    def _alu(self, i, live, r, op, pres_c=False):
        tok = self.pres_begin(live & C) if pres_c else None
        s = self.src8(i)
        self.e(f"{op}.b {s},{r}")
        self.pres_end(tok)
    def t_SUBA(self, i, live): self._alu(i, live, '%d0', 'sub')
    def t_SUBB(self, i, live): self._alu(i, live, '%d1', 'sub')
    def t_CMPA(self, i, live): self._alu(i, live, '%d0', 'cmp')
    def t_CMPB(self, i, live): self._alu(i, live, '%d1', 'cmp')
    def t_ANDA(self, i, live): self._alu(i, live, '%d0', 'and', True)
    def t_ANDB(self, i, live): self._alu(i, live, '%d1', 'and', True)
    def t_ORA(self, i, live): self._alu(i, live, '%d0', 'or', True)
    def t_ORB(self, i, live): self._alu(i, live, '%d1', 'or', True)
    def _eor(self, i, live, r):
        tok = self.pres_begin(live & C)
        s = self.src8(i)
        if not s.startswith('#') and s != '%d5': self.e(f"move.b {s},%d5"); s = '%d5'
        self.e(f"eor.b {s},{r}")
        self.pres_end(tok)
    def t_EORA(self, i, live): self._eor(i, live, '%d0')
    def t_EORB(self, i, live): self._eor(i, live, '%d1')
    def _bit(self, i, live, r):
        tok = self.pres_begin(live & C)
        s = self.src8(i)
        self.e(f"move.b {r},%d5") if s != '%d5' else None
        if s == '%d5': self.e(f"and.b {r},%d5")
        else: self.e(f"and.b {s},%d5")
        self.pres_end(tok)
    def t_BITA(self, i, live): self._bit(i, live, '%d0')
    def t_BITB(self, i, live): self._bit(i, live, '%d1')

    def _add(self, i, live, r):
        """ADD with half-carry maintained in vm_cc (bit 5): H = bit 4 of (old ^ operand ^ result)"""
        e = self.e
        if i.mode == 'imm8':
            e(f"move.b #{int(i.operand[2:], 16)},%d5")
        else:
            m = self.mop(i, 1)
            if m.kind == 'io': self.io_read(m.addr, '%d5')
            else: e(f"move.b {m.b(0)},%d5")
        e(f"move.b {r},%d6"); e("eor.b %d5,%d6")                   # d6 = old ^ operand
        e(f"add.b %d5,{r}")                                        # N Z V C exact
        e("move %ccr,-(%sp)")
        e(f"eor.b {r},%d6"); e("and.b #0x10,%d6"); e("add.b %d6,%d6")          # d6 = H at bit 5
        e("and.b #0xDF,vm_cc(%a6)"); e("or.b %d6,vm_cc(%a6)")
        e("move (%sp)+,%ccr")
    def t_ADDA(self, i, live): self._add(i, live, '%d0')
    def t_ADDB(self, i, live): self._add(i, live, '%d1')

    # ADC / SBC / ROL / ROR / DAA / MUL: small C helpers (rare); they take A or B in %d0/%d1, the operand in %d5, old flags in %d6
    def _helper(self, i, live, name, operand=True):
        e = self.e
        e("move %ccr,%d6")
        if operand:
            if i.mode == 'imm8': e(f"move.b #{int(i.operand[2:], 16)},%d5")
            else:
                m = self.mop(i, 1)
                if m.kind == 'io': self.io_read(m.addr, '%d5')
                else: e(f"move.b {m.b(0)},%d5")
        e(f"jbsr rt_h_{name}")
    def t_ADCA(self, i, live): self._helper(i, live, 'adca')
    def t_ADCB(self, i, live): self._helper(i, live, 'adcb')
    def t_SBCA(self, i, live): self._helper(i, live, 'sbca')
    def t_SBCB(self, i, live): self._helper(i, live, 'sbcb')
    def t_ROLA(self, i, live): self._helper(i, live, 'rola', False)
    def t_ROLB(self, i, live): self._helper(i, live, 'rolb', False)
    def t_RORA(self, i, live): self._helper(i, live, 'rora', False)
    def t_RORB(self, i, live): self._helper(i, live, 'rorb', False)
    def t_DAA(self, i, live): self._helper(i, live, 'daa', False)
    def t_MUL(self, i, live): self._helper(i, live, 'mul', False)
    def t_SEX(self, i, live): self._helper(i, live, 'sex', False)
    def t_ABX(self, i, live): self._helper(i, live, 'abx', False)

    # ---- 16-bit ALU
    def _src16(self, i):
        if i.mode == 'imm16': return f"#{int(i.operand[2:], 16)}"
        m = self.mop(i, 2)
        if m.kind == 'io': raise Unsupported("16-bit read of I/O")
        return m.b(0)
    def _cmp16(self, i, live, r):
        s = self._src16(i)
        self.e(f"cmp.w {s},{r}")
    def t_CMPX(self, i, live): self._cmp16(i, live, '%d2')
    def t_CMPY(self, i, live): self._cmp16(i, live, '%d3')
    def t_CMPU(self, i, live): self._cmp16(i, live, '%d4')
    def t_CMPD(self, i, live):
        s = self._src16(i)
        self.e("move.b %d0,%d5"); self.e("lsl.w #8,%d5"); self.e("move.b %d1,%d5"); self.e(f"cmp.w {s},%d5")
    def _addsubd(self, i, live, op):
        e = self.e
        if i.mode == 'imm16':
            v = int(i.operand[2:], 16)
            e(f"move.b #{v >> 8},%d5"); e(f"{op}.b #{v & 0xFF},%d1"); e(f"{op}x.b %d5,%d0")
        else:
            m = self.mop(i, 2)
            if m.kind == 'io': raise Unsupported("16-bit read of I/O")
            e(f"move.b {m.b(0)},%d5"); e(f"move.b {m.b(1)},%d6"); e(f"{op}.b %d6,%d1"); e(f"{op}x.b %d5,%d0")
    def t_ADDD(self, i, live): self._addsubd(i, live, 'add')
    def t_SUBD(self, i, live): self._addsubd(i, live, 'sub')

    # ---- unary: INC DEC CLR TST NEG COM and shifts (A/B and memory)
    def _rmw(self, i, live, regs, mem_op, imm=None):
        pass
    def t_INCA(self, i, live): self._incdec(live, '%d0', 'addq')
    def t_INCB(self, i, live): self._incdec(live, '%d1', 'addq')
    def t_DECA(self, i, live): self._incdec(live, '%d0', 'subq')
    def t_DECB(self, i, live): self._incdec(live, '%d1', 'subq')
    def _incdec(self, live, dst, op):
        tok = self.pres_begin(live & C)
        self.e(f"{op}.b #1,{dst}")
        self.pres_end(tok)
    def _mem_unary(self, i, live, op, pres_c, extra=None):
        tok = self.pres_begin(live & C) if pres_c else None
        m = self.mop(i, 1)
        if m.kind in ('io', 'rom'): raise Unsupported(f"{i.mnem} on {m.kind}")
        if op in ('addq', 'subq'): self.e(f"{op}.b #1,{m.b(0)}")
        else: self.e(f"{op}.b {m.b(0)}")
        if extra: extra()
        self.pres_end(tok)
    def t_INC(self, i, live): self._mem_unary(i, live, 'addq', True)
    def t_DEC(self, i, live): self._mem_unary(i, live, 'subq', True)
    def t_CLR(self, i, live): self._mem_unary(i, live, 'clr', False)
    def t_NEG(self, i, live): self._mem_unary(i, live, 'neg', False)
    def t_COM(self, i, live): self._mem_unary(i, live, 'not', False, lambda: self.e("or #1,%ccr"))
    def t_TST(self, i, live):
        tok = self.pres_begin(live & C)
        m = self.mop(i, 1)
        if m.kind == 'io': self.io_read(m.addr, '%d5'); self.e("tst.b %d5")
        else: self.e(f"tst.b {m.b(0)}")
        self.pres_end(tok)
    def t_CLRA(self, i, live): self.e("clr.b %d0")
    def t_CLRB(self, i, live): self.e("clr.b %d1")
    def t_TSTA(self, i, live): self._reg_c(live, "tst.b %d0")
    def t_TSTB(self, i, live): self._reg_c(live, "tst.b %d1")
    def _reg_c(self, live, ins, mask=C):
        tok = self.pres_begin(live & mask); self.e(ins); self.pres_end(tok)
    def t_NEGA(self, i, live): self.e("neg.b %d0")
    def t_NEGB(self, i, live): self.e("neg.b %d1")
    def t_COMA(self, i, live): self.e("not.b %d0"); self.e("or #1,%ccr")
    def t_COMB(self, i, live): self.e("not.b %d1"); self.e("or #1,%ccr")
    def t_ASLA(self, i, live): self.e("asl.b #1,%d0")
    def t_ASLB(self, i, live): self.e("asl.b #1,%d1")
    def t_LSLA(self, i, live): self.e("asl.b #1,%d0")
    def t_LSLB(self, i, live): self.e("asl.b #1,%d1")
    def t_ASRA(self, i, live): self._reg_c(live, "asr.b #1,%d0", V)
    def t_ASRB(self, i, live): self._reg_c(live, "asr.b #1,%d1", V)
    def t_LSRA(self, i, live): self._reg_c(live, "lsr.b #1,%d0", V)
    def t_LSRB(self, i, live): self._reg_c(live, "lsr.b #1,%d1", V)
    def t_NOP(self, i, live): pass

    # ---- LEA
    def _lea(self, i, live, r, setz):
        info = i.idx; kind = info['kind']; reg = info['reg']
        if info['pb'] & 0x10 and kind != 'off5': raise Unsupported("indirect LEA")
        if kind in ('inc1', 'inc2', 'dec1', 'dec2'): raise Unsupported("LEA with auto increment")
        e = self.e
        mask = (NZVC & ~Z) if setz else NZVC
        tok = self.pres_begin(live & mask)
        if reg == 'S':
            if kind not in ('off5', 'off8', 'off16'): raise Unsupported("LEA S")
            e("move.l %a4,%d5"); e("sub.l %a5,%d5"); e(f"add.l #{0x8000 + info['off']},%d5"); e(f"move.w %d5,{r}")
            if setz: e(f"tst.w {r}")
        else:
            src = self.reg16(reg)
            if kind in ('off5', 'off8', 'off16'):
                off = info['off']
                if src != r: e(f"move.w {src},{r}")
                if off > 0: e(f"addq.w #{off},{r}") if off <= 8 else e(f"add.w #{off},{r}")
                elif off < 0: e(f"subq.w #{-off},{r}") if -off <= 8 else e(f"sub.w #{-off},{r}")
                elif setz: e(f"tst.w {r}")
            elif kind == 'zero':
                if src != r: e(f"move.w {src},{r}")
                if setz: e(f"tst.w {r}")
            elif kind in ('accA', 'accB'):
                e(f"move.b {'%d0' if kind == 'accA' else '%d1'},%d5"); e("extb.l %d5")
                if src != r: e(f"move.w {src},{r}")
                e(f"add.w %d5,{r}")
            elif kind == 'accD':
                e("move.b %d0,%d5"); e("lsl.w #8,%d5"); e("move.b %d1,%d5")
                if src != r: e(f"move.w {src},{r}")
                e(f"add.w %d5,{r}")
            else: raise Unsupported(f"LEA kind {kind}")
        self.pres_end(tok)
    def t_LEAX(self, i, live): self._lea(i, live, '%d2', True)
    def t_LEAY(self, i, live): self._lea(i, live, '%d3', True)
    def t_LEAU(self, i, live): self._lea(i, live, '%d4', False)

    # ---- TFR (no CC)
    def t_TFR(self, i, live):
        s, d = i.operand.split(',')
        e = self.e
        tok = self.pres_begin(live & NZVC)
        # source -> d5 (word)
        if s in ('X', 'Y', 'U'): src = self.reg16(s)
        elif s == 'D': e("move.b %d0,%d5"); e("lsl.w #8,%d5"); e("move.b %d1,%d5"); src = '%d5'
        elif s in ('A', 'B'): e(f"moveq #0,%d5"); e(f"move.b {self.reg8(s)},%d5"); src = '%d5'
        elif s == 'S': e("move.l %a4,%d5"); e("sub.l %a1,%d5"); src = '%d5'
        elif s == 'DP': e("moveq #0,%d5"); e("move.b vm_dp(%a6),%d5"); src = '%d5'
        else: raise Unsupported(f"TFR from {s}")
        if d in ('X', 'Y', 'U'): e(f"move.w {src},{self.reg16(d)}")
        elif d == 'D':
            if src != '%d5': e(f"move.w {src},%d5")
            e("move.b %d5,%d1"); e("lsr.w #8,%d5"); e("move.b %d5,%d0")
        elif d in ('A', 'B'): e(f"move.b {src},{self.reg8(d)}")
        elif d == 'S':
            if src != '%d5': e(f"move.w {src},%d5")
            e("and.l #0xFFFF,%d5"); e("lea (%a1,%d5.l),%a4")
        elif d == 'DP':
            e(f"move.b {src},vm_dp(%a6)")
            if src != '%d5': e(f"move.b {src},%d5")
            e("and.w #0xFF,%d5"); e("lsl.w #8,%d5"); e("and.l #0xFFFF,%d5"); e("lea (%a1,%d5.l),%a2")
        else: raise Unsupported(f"TFR to {d}")
        self.pres_end(tok)

    # ---- PSHS / PULS (no CC bit: those are interpreted)
    def t_PSHS(self, i, live):
        m = i.raw[-1]; e = self.e
        if m & 1: e("move %ccr,%d6"); tok = ('m', NZVC)          # the pushed CC needs the incoming flags; they are restored afterwards
        else: tok = self.pres_begin(live & NZVC)
        pc = i.addr + i.length
        if m & 0x80: e(f"movea.w #{pc},%a0"); e("move.w %a0,-(%a4)")
        if m & 0x40: e("move.w %d4,-(%a4)")
        if m & 0x20: e("move.w %d3,-(%a4)")
        if m & 0x10: e("move.w %d2,-(%a4)")
        if m & 0x08: e("move.b vm_dp(%a6),-(%a4)")
        if m & 0x04: e("move.b %d1,-(%a4)")
        if m & 0x02: e("move.b %d0,-(%a4)")
        if m & 0x01:
            e("move.b %d6,%d5"); e("and.b #0x0F,%d5"); e("or.b vm_cc(%a6),%d5"); e("move.b %d5,-(%a4)")
        self.pres_end(tok)
    def t_PULS(self, i, live):
        m = i.raw[-1]; e = self.e
        tok = self.pres_begin(live & NZVC)
        if m & 0x02: e("move.b (%a4)+,%d0")
        if m & 0x04: e("move.b (%a4)+,%d1")
        if m & 0x08:
            e("move.b (%a4)+,vm_dp(%a6)"); e("moveq #0,%d5"); e("move.b vm_dp(%a6),%d5"); e("lsl.w #8,%d5"); e("lea (%a1,%d5.l),%a2")
        if m & 0x10: e("move.w (%a4)+,%d2")
        if m & 0x20: e("move.w (%a4)+,%d3")
        if m & 0x40: e("move.w (%a4)+,%d4")
        if m & 0x80:
            e("movea.w (%a4)+,%a0")
            self.pres_end(tok)
            e("jmp ([ytab_mid,%za0,%a0.l*4])")
            return
        self.pres_end(tok)

