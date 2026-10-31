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

# Idle loops: a loop whose body only reads RAM words that are changed by the interrupt handlers, so that every iteration is identical.  The
# main program waits here for events: LDX <$DA ; LDD ,X ; ASLA ; BCS 807C  (15 cycles per iteration, 69 % of all CPU time of the arcade).
# Taking the back edge skips whole iterations while at least 128 cycles remain before the next scheduler event (state after one iteration is the
# fixed point of the loop, so the skipped iterations leave registers and flags unchanged; only the cycle counter advances).
IDLE_LOOPS_ON = {0x8081: (0x807C, 15, bytes.fromhex('9E DA EC 84 48 25 F9'))}
IDLE_LOOPS = {} if os.environ.get("NOIDLE") else IDLE_LOOPS_ON

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
        self.mids = []

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
        if os.environ.get("TRACEBLK"):
            A("\tmove %ccr,-(%sp)"); A("\tmovem.l %d0-%d1/%a0-%a1,-(%sp)"); A("\tmove.l %a2,-(%sp)"); A("\tmove.l %d2,-(%sp)"); A("\tmove.l %d1,-(%sp)"); A("\tmove.l %d0,-(%sp)"); A("\tmove.l %a3,-(%sp)"); A(f"\tmove.l #{first.addr},-(%sp)"); A("\tjsr rt_blocklog_c"); A("\tlea 24(%sp),%sp"); A("\tmovem.l (%sp)+,%d0-%d1/%a0-%a1"); A("\tmove (%sp)+,%ccr")
        A(f"\tlea -{total}(%a3),%a3")
        t = Tr(self.check)
        end_addr = None
        fall = None
        for k, i in enumerate(blk):
            t.out = []; t.blk_total = total; t.blk_off = offs[k]; t.cur = i
            if k > 0:                                          # entry point in the middle of the block (return from an interrupt lands here)
                o.append(f"Yi_{i.addr:04X}:")
                self.mids.append((i.addr, total - offs[k], offs[-1] - offs[k]))
            m = i.mnem
            last = (k == len(blk) - 1)
            nxt = i.addr + i.length
            if i.addr in IDLE_LOOPS:
                tgt, per, sig = IDLE_LOOPS[i.addr]
                assert i.target == tgt and bytes(self.rom[tgt - 0x8000: tgt - 0x8000 + len(sig)]) == sig, "idle loop signature mismatch"
                skip = t.lab()
                t.e(f"jbcc {skip}")                                  # not taken: leave the loop
                t.e("move %ccr,%d6"); t.e("move.l %a3,%d5"); t.e("sub.l #143,%d5"); t.e(f"jbmi {skip}x")
                # the registers must be the fixed point of the loop (an iteration that was interrupted and resumed by RTI may not be): X = <$DA, A = [X] << 1, B = [X+1]
                t.e("move.w 218(%a2),%d5"); t.e("cmp.w %d5,%d2"); t.e(f"jbne {skip}x")
                t.e("move.b (%a1,%d5.l),%d5"); t.e("add.b %d5,%d5"); t.e("cmp.b %d5,%d0"); t.e(f"jbne {skip}x")
                t.e("move.b 1(%a1,%d2.l),%d5"); t.e("cmp.b %d5,%d1"); t.e(f"jbne {skip}x")
                t.e("move.l %a3,%d5"); t.e("sub.l #143,%d5")
                t.e(f"divu.w #{per},%d5"); t.e("and.l #0xFFFF,%d5"); t.e(f"mulu.w #{per},%d5"); t.e("suba.l %d5,%a3"); t.e("moveq #0,%d7")
                t.e(f"{skip}x:"); t.e("move %d6,%ccr"); t.e(f"jbra {lab(tgt)}")
                t.e(f"{skip}:")
                fall = nxt
            elif i.is_branch and m in CC68 or (m.startswith('LB') and m[1:] in CC68 or (m.startswith('LB') and 'B' + m[2:] in CC68)):
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
                    t.e("move %ccr,%d6"); t.ea_to_a0(i); t.e("move %d6,%ccr"); t.e("jmp ([ytab_mid,%za0,%a0.l*4])")
            elif m == 'JMP':
                if i.target is not None and i.mode != 'idx':
                    t.e(f"jbra {lab(i.target)}")
                else:
                    t.e("move %ccr,%d6"); t.ea_to_a0(i); t.e("move %d6,%ccr"); t.e("jmp ([ytab_mid,%za0,%a0.l*4])")
            elif m == 'RTI':
                skip = t.lab(); done = t.lab()
                t.e("move.b (%a4)+,%d5"); t.e("move.b %d5,%d6"); t.e("and.b #0xF0,%d6"); t.e("move.b %d6,vm_cc(%a6)")
                t.e("btst #7,%d5"); t.e(f"jbeq {skip}")                                   # E flag: whole machine state was stacked
                t.e("move.b (%a4)+,%d0"); t.e("move.b (%a4)+,%d1")
                t.e("move.b (%a4)+,vm_dp(%a6)"); t.e("moveq #0,%d6"); t.e("move.b vm_dp(%a6),%d6"); t.e("lsl.w #8,%d6"); t.e("lea (%a1,%d6.l),%a2")
                t.e("move.w (%a4)+,%d2"); t.e("move.w (%a4)+,%d3"); t.e("move.w (%a4)+,%d4")
                t.e(f"{skip}:")
                t.e("movea.w (%a4)+,%a0")
                t.e("and.w #0x0F,%d5")
                t.e("tst.b rt_irqline"); t.e(f"jbeq {done}")                                # level-triggered IRQ still asserted: unmasked now?
                t.e("btst #4,vm_cc(%a6)"); t.e(f"jbne {done}")
                t.e("move %d5,%ccr"); t.e("jbra rt_interp1")                              # the interpreter takes the interrupt (a0 = return address)
                t.e(f"{done}:"); t.e("move %d5,%ccr"); t.e("jmp ([ytab_mid,%za0,%a0.l*4])")
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
        out.append('\t.globl ymid_tab'); out.append('ymid_tab:')
        for addr, rest, rel in self.mids:            # entry inside a block: (pc, rest of the block, last instruction start relative to this one, native address - y_code_start)
            out.append(f"\t.word {addr}, {(rest << 8) | rel}"); out.append(f"\t.long Yi_{addr:04X} - y_code_start")
        out.append('\t.word 0, 0'); out.append('\t.long 0')
        out.append('\t.globl y_code_end'); out.append('y_code_end:')
        # leader table: (6809 address, native address)
        out.append('\t.section .rodata'); out.append('\t.even'); out.append('\t.globl ylead_tab'); out.append('ylead_tab:')
        for a in sorted(b[0].addr for b in bl):
            out.append(f"\t.long {a}, {lab(a)}")
        out.append('\t.long 0, 0')
        out.append('\t.globl yolast_tab'); out.append('yolast_tab:')
        for b in bl:
            if self.is_interp(b[0]): out.append(f"\t.word {b[0].addr}, 0"); continue
            out.append(f"\t.word {b[0].addr}, {sum(model.cycles(i) for i in b) - model.cycles(b[-1])}")
        out.append('\t.word 0, 0')
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
