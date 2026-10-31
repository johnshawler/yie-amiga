#!/bin/sh
# usage: build.sh [out-name] [translator args]   env TRACEIO=1: generic I/O accessors (every access logged with its cycle stamp)
# builds the qemu-m68k harness: translated game + runtime + shared board model  ->  build/yrun
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-yrun}
B=$ROOT/build; mkdir -p $B
CC=m68k-linux-gnu-gcc
python3 $ROOT/amiga/translator/ytrans.py $B/ycode.s "$@" > $B/ytrans.log
FLAGS="-m68020 -O2 -Wall -fno-pic -I$ROOT/amiga/ref/src -I$ROOT/amiga/runtime -DBOARD_HOST"
$CC $FLAGS -x assembler -Wa,-I$ROOT/amiga/runtime -c $B/ycode.s -o $B/ycode.o
$CC -m68020 -x assembler -Wa,-I$ROOT/amiga/runtime -c $ROOT/amiga/runtime/ysys.s -o $B/ysys.o
$CC $FLAGS -DM6809_FLAT -Dm6809_step=m6809_step_flat -Dm6809_reset=m6809_reset_flat -c -o $B/m6809f.o $ROOT/amiga/ref/src/m6809.c
$CC $FLAGS -static -o $B/$OUT $ROOT/amiga/harness/yrun.c $ROOT/amiga/runtime/yrt.c $ROOT/amiga/runtime/board.c $ROOT/amiga/ref/src/m6809.c $B/ycode.o $B/ysys.o $B/m6809f.o
echo built $B/$OUT
