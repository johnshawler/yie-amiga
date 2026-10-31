#!/bin/sh
# usage: run.sh [iters] [only]
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
B=$ROOT/build/xltest; mkdir -p $B
python3 -I $ROOT/amiga/translator/test/gentests.py
CC=m68k-linux-gnu-gcc
AS="$CC -m68020 -x assembler -Wa,-I$ROOT/amiga/runtime"
$AS -c -o $B/tests.o $B/tests.s
$AS -c -o $B/ysys.o $ROOT/amiga/runtime/ysys.s
$AS -c -o $B/shim.o $ROOT/amiga/translator/test/shim.s
$CC -m68020 -O1 -static -I$B -I$ROOT/amiga/ref/src -o $B/xltest $ROOT/amiga/translator/test/driver.c $ROOT/amiga/runtime/yrt_stub.c $ROOT/amiga/ref/src/m6809.c $B/tests.o $B/ysys.o $B/shim.o
qemu-m68k -cpu m68020 $B/xltest "$@"
