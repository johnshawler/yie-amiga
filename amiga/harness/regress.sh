#!/bin/sh
# Whole-program regression: reference interpreter (yref) vs translated game under qemu-m68k (yrun): per-frame RAM + CPU registers and the
# stream of I/O accesses (order, data, cycle stamp; watchdog writes excluded: the translated code does not perform them).
# usage: regress.sh [frames] [inputs.txt] [pinned|mame0264]
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
N=${1:-3000}; INP=${2:-$ROOT/build/inputs_2.txt}; TM=${3:-pinned}
B=$ROOT/build; mkdir -p $B
[ -f $INP ] || python3 $ROOT/amiga/ref/trace/mkinputs.py 12000 2 > $INP
[ -x $B/yref ] || gcc -O2 -DBOARD_HOST -I$ROOT/amiga/ref/src -I$ROOT/amiga/runtime -o $B/yref $ROOT/amiga/ref/src/yref.c $ROOT/amiga/ref/src/m6809.c $ROOT/amiga/runtime/board.c
$B/yref -n $N -i $INP -o $B/reg_ref -r $ROOT/roms -t $TM 2>/dev/null
# (1) generic I/O accessors: every I/O access of the translated game is logged and compared with its cycle stamp
OUT=yrun_trace TRACEIO=1 sh $ROOT/amiga/harness/build.sh >/dev/null 2>&1 || { echo "trace build failed"; exit 1; }
qemu-m68k -cpu m68020 $B/yrun_trace -n $N -i $INP -o $B/reg_run -r $ROOT/roms -t $TM 2>&1 | tail -1
python3 - $B/reg_ref.io $B/reg_run.io <<'P'
import sys
def ld(p): return [x for x in (l.split() for l in open(p)) if x[0] != 'V' and x[2] != '4f00']
a, b = ld(sys.argv[1]), ld(sys.argv[2])
bad = sum(1 for x, y in zip(a, b) if x != y)
print("I/O events: reference %d, translated %d, differing %d%s" % (len(a), len(b), bad, "  (exact, cycle stamps included)" if not bad and len(a) == len(b) else "  ** MISMATCH **"))
sys.exit(1 if bad or len(a) != len(b) else 0)
P
IO=$?
cmp $B/reg_ref.bin $B/reg_run.bin >/dev/null && echo "  frame dumps (RAM + registers + cycle counter): byte-identical" || { echo "  frame dumps DIFFER"; IO=1; }
# (2) the shipped configuration (specialised I/O ports): frame dumps only
sh $ROOT/amiga/harness/build.sh >/dev/null 2>&1 || { echo "build failed"; exit 1; }
qemu-m68k -cpu m68020 $B/yrun -n $N -i $INP -o $B/reg_fast -r $ROOT/roms -t $TM 2>&1 | tail -1
cmp $B/reg_ref.bin $B/reg_fast.bin >/dev/null && echo "shipped build: frame dumps (RAM + registers + cycle counter) byte-identical to the reference" || { echo "shipped build: frame dumps DIFFER"; python3 $ROOT/amiga/ref/trace/cmp_trace.py $B/reg_ref.bin $B/reg_fast.bin 0 | tail -2; IO=1; }
# (3) sound: the event-driven synthesiser against the per-sample reference model (verified against MAME audio, see sound/snref.c) on this run's writes,
#     and the speech blob (LZ4) against the captures
gcc -O2 -w -I$ROOT/amiga/runtime -o $B/sntest $ROOT/amiga/sound/sntest.c $ROOT/amiga/runtime/snsynth.c -lm && $B/sntest $B/reg_ref.io $N 472 | tail -2 || IO=1
m68k-linux-gnu-gcc -m68020 -O2 -static -w -I$ROOT/amiga/runtime -o $B/sntest68 $ROOT/amiga/sound/sntest.c $ROOT/amiga/runtime/snsynth.c $ROOT/amiga/runtime/snsynth_m68k.s -lm 2>/dev/null && qemu-m68k -cpu m68020 $B/sntest68 $B/reg_ref.io $N 472 | tail -2 | sed 's/^/m68k build (C loop, debug averages): /' || IO=1
for args in "$B/reg_ref.io $N 472" "rand 1500 470 1" "rand 1500 478 2"; do a=$(SNS_NOAVG=1 qemu-m68k -cpu m68020 $B/sntest68 $args | grep checksum); c=$(SNS_NOAVG=1 SNS_FORCE_C=1 qemu-m68k -cpu m68020 $B/sntest68 $args | grep checksum); if [ "$a" = "$c" ]; then echo "assembly core == C loop: $a ($args)"; else echo "** assembly core DIFFERS from the C loop ($args)"; IO=1; fi; done
python3 $ROOT/amiga/tools/mkvlm.py $ROOT/amiga/ref/audio/vlm_u16.bin $B/vlm_regress.bin >/dev/null && gcc -O2 -w -o $B/vlmtest $ROOT/amiga/sound/vlmtest.c $ROOT/amiga/runtime/lz4.c && $B/vlmtest $B/vlm_regress.bin $ROOT/amiga/ref/audio/vlm_u16.bin || IO=1
exit $IO
