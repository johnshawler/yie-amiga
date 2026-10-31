#!/bin/bash
# usage: audiotest.sh <pal|ntsc> [boot_secs]   (KICK=... must be set: the real Kickstart, test only)
# Builds the AUDIOTEST variant (no game: tools/mkaudiotest.py script -> chip writes + speech starts through the real sound path), runs it in FS-UAE, records the
# Paula output (OpenAL wave backend, 48 kHz) and compares it with the stream sound/sndpc.c computes from the same script (tools/audiocheck.py).
set -e
std=${1:-pal}; secs=${2:-85}
ROOT=$(cd "$(dirname "$0")/../../.." && pwd); B=$ROOT/build/at; mkdir -p $B
python3 $ROOT/amiga/tools/mkaudiotest.py $B
[ -x $B/sndpc ] || gcc -O2 -w -I$ROOT/amiga/runtime -o $B/sndpc $ROOT/amiga/sound/sndpc.c $ROOT/amiga/runtime/snsynth.c
$B/sndpc $B/audiotest.io 1560 $B/exp_$std $std >/dev/null
(cd $ROOT/amiga/platform && make BUILD=at EXTRA="-DAUDIOTEST -I$B" >/dev/null)
echo 'sleep 1' > $B/wait.sh
export ADF=$B/yiear_a1200.adf AUDIO_OUT=$B/rec_$std.wav; rm -f $AUDIO_OUT
"$(dirname "$0")"/run.sh $std $B/at_$std $secs $B/wait.sh >/dev/null 2>&1
WSEC=0.25 python3 $ROOT/amiga/tools/audiocheck.py $AUDIO_OUT $B/exp_$std.sn $B/exp_$std.ev $ROOT/amiga/ref/audio/vlm_u16.bin $std
