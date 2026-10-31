#!/usr/bin/env python3
"""Whole-program 6809 -> 68020 translator for the Yie Ar Kung-Fu ROM.
usage: ytrans.py out.s [--check] [--report]
Emits GNU as source: one labelled, budget-guarded block per leader (see emit.py), plus the leader list (ylead_tab) used to fill the dispatch table."""
import os, sys, collections
sys.path.insert(0, os.path.dirname(__file__))
import model, flags as F
from emit import Tr, Unsupported, N, Z, V, C, NZVC

CC68 = {'BHI': 'jbhi', 'BLS': 'jbls', 'BCC': 'jbcc', 'BCS': 'jbcs', 'BNE': 'jbne', 'BEQ': 'jbeq', 'BVC': 'jbvc', 'BVS': 'jbvs',
        'BPL': 'jbpl', 'BMI': 'jbmi', 'BGE': 'jbge', 'BLT': 'jblt', 'BGT': 'jbgt', 'BLE': 'jble'}

def lab(a): return f"Y{a:04X}"

def effect(i):
    rd, wr = F.effect(i)
    return rd & NZVC, wr & NZVC

class Gen:
    def __init__(self, rom, check=False):
        self.rom = rom; self.check = check
        self.code, self.vec = model.discover(rom)
        # instructions the templates cannot handle become interpreted
        self.interp = set()
        self.leaders = model.leaders_of(self.code, self.vec, rom)
        self.pre_pass()
        self.leaders = model.leaders_of(self.code, self.vec, rom)
        for a in self.interp: self.leaders.add(a)
        self.blocks = model.blocks_of(self.code, self.leaders)
        self.stats = collections.Counter()

    def pre_pass(self):
        """find instructions whose template raises Unsupported -> interpreted instead (decided before blocks are formed)"""
        t = Tr()
        for a, i in sorted(self.code.items()):
            if model.interp_only(i): self.interp.add(a); continue
            if i.is_branch or i.is_call or i.ends_flow: continue
            try:
                t.out = []; t.blk_total = 8; t.blk_off = 0
                t.tr(i, NZVC)
            except Unsupported as ex:
                self.interp.add(a); self.stats['unsupported:' + i.mnem] += 1

    def is_interp(self, i): return i.addr in self.interp

    def gen_block(self, blk, nxt_addr):
        o = []
        first = blk[0]
        A = lambda s: o.append(s)
        if self.is_interp(first):
            A(f"{lab(first.addr)}:")
            A(f"\tmovea.w #{first.addr},%a0"); A("\tjbra rt_interp1")
            return o, first.addr + first.length
        # cycles
        cyc = [model.cycles(i) for i in blk]
        total = sum(cyc)
        offs = []; s = 0
        for c in cyc: offs.append(s); s += c
        # liveness within the block (everything is live at the end)
        live = NZVC; lives = [0] * len(blk)
        for k in range(len(blk) - 1, -1, -1):
            lives[k] = live
            rd, wr = effect(blk[k])
            live = (live & ~wr) | rd
        A(f"{lab(first.addr)}:")
        A(f"\tdbra %d7,{lab(first.addr)}_b")
        A(f"\tmovea.w #{first.addr},%a0"); A("\tjbra rt_refill")
        A(f"{lab(first.addr)}_b:")
        A(f"\tlea -{total}(%a3),%a3")
        t = Tr(self.check)
        end_addr = None
        fall = None
        for k, i in enumerate(blk):
            t.out = []; t.blk_total = total; t.blk_off = offs[k]; t.cur = i
            m = i.mnem
            last = (k == len(blk) - 1)
            nxt = i.addr + i.length
            if i.is_branch and m in CC68 or (m.startswith('LB') and m[1:] in CC68 or (m.startswith('LB') and 'B' + m[2:] in CC68)):
                bm = m if m in CC68 else 'B' + m[2:]
                t.e(f"{CC68[bm]} {lab(i.target)}")
                if m.startswith('LB'): t.e("lea 1(%a3),%a3")          # long branches are charged as taken (6); credit the not-taken path (5)
                fall = nxt
            elif m in ('BRA', 'LBRA'):
                t.e(f"jbra {lab(i.target)}")
            elif m in ('BRN', 'LBRN'):
                fall = nxt
            elif m in ('BSR', 'LBSR', 'JSR'):
                ret = nxt
                t.e(f"movea.w #{ret},%a0"); t.e("movem.w %a0,-(%a4)")
                if i.target is not None and not (m == 'JSR' and i.mode == 'idx'):
                    t.e(f"jbra {lab(i.target)}")
                else:
                    t.ea_to_a0(i); t.e("jmp ([ytab_mid,%za0,%a0.l*4])")
            elif m == 'JMP':
                if i.target is not None and i.mode != 'idx':
                    t.e(f"jbra {lab(i.target)}")
                else:
                    t.ea_to_a0(i); t.e("jmp ([ytab_mid,%za0,%a0.l*4])")
            elif m == 'RTS':
                t.e("movea.w (%a4)+,%a0"); t.e("jmp ([ytab_mid,%za0,%a0.l*4])")
            else:
                t.tr(i, lives[k])
                if last: fall = nxt
            o.extend(t.out)
        if fall is not None and fall != nxt_addr:
            A(f"\tjbra {lab(fall)}")
        return o, fall

    def run(self):
        out = ['\t.include "yvm.inc"', '\t.text', '\t.even', '\t.globl y_code_start', 'y_code_start:']
        bl = self.blocks
        for n, blk in enumerate(bl):
            nxt_addr = bl[n + 1][0].addr if n + 1 < len(bl) else None
            lines, fall = self.gen_block(blk, nxt_addr)
            out.extend(lines)
            last = blk[-1]
            if fall is not None and fall not in self.code: out.append("\tjbra rt_trap_data")
        out.append('\t.globl y_code_end'); out.append('y_code_end:')
        # leader table: (6809 address, native address)
        out.append('\t.section .rodata'); out.append('\t.even'); out.append('\t.globl ylead_tab'); out.append('ylead_tab:')
        for a in sorted(b[0].addr for b in bl):
            out.append(f"\t.long {a}, {lab(a)}")
        out.append('\t.long 0, 0')
        out.append('\t.globl ylead_count'); out.append(f'ylead_count:\t.long {len(bl)}')
        return '\n'.join(out) + '\n'

if __name__ == '__main__':
    rom = model.load_rom()
    g = Gen(rom, check='--check' in sys.argv)
    txt = g.run()
    open(sys.argv[1], 'w').write(txt)
    print(f"{len(g.code)} instructions, {len(g.blocks)} blocks, {len(g.interp)} interpreted instructions, {len(txt.splitlines())} lines")
    if '--report' in sys.argv:
        print(dict(g.stats))
        print("interpreted:", collections.Counter(g.code[a].mnem for a in g.interp))
