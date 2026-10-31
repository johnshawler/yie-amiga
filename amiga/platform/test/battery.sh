#!/bin/bash
# usage: battery.sh <pal|ntsc>   (KICK=... the real Kickstart, test only)  Regression battery in FS-UAE: boot, coin, start, joystick, second button, pause, restart, free play.
set -e
std=${1:-pal}; ROOT=$(cd "$(dirname "$0")/../../.." && pwd); HERE=$(cd "$(dirname "$0")" && pwd)
(cd $ROOT/amiga/platform && make BUILD=diag EXTRA="-DHUD -DDIAG" >/dev/null)
export ADF=$ROOT/build/diag/yiear_a1200.adf
"$HERE"/run.sh $std $ROOT/build/battery_$std ${2:-70} "$HERE"/battery_inject.sh >/dev/null 2>&1
python3 "$HERE"/battery_check.py $ROOT/build/battery_$std
