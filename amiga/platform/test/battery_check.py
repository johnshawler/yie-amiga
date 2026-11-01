#!/usr/bin/env python3
"""Reads the HUD of the battery screenshots and checks the regression battery.  usage: battery_check.py prefix   (prefix_<name>_full.png)"""
import sys, subprocess, os
here = os.path.dirname(os.path.abspath(__file__))
pre = sys.argv[1]
def hud(name):
    out = subprocess.check_output([sys.executable, os.path.join(here, 'hudread.py'), '%s_%s_full.png' % (pre, name)], env=dict(os.environ, ROWS='14')).decode().split()
    return [int(x, 16) for x in out[6:]]
R = {n: hud(n) for n in 'a0 a1 coin start joy btn2 resume restart freeplay'.split()}
from PIL import Image, ImageChops
img = lambda n: Image.open('%s_%s_full.png' % (pre, n)).convert('RGB')
same = lambda a, b: ImageChops.difference(img(a), img(b)).getbbox() is None
ok = True
def check(name, cond, detail=''):
    global ok; ok &= bool(cond); print('%-34s %s %s' % (name, 'PASS' if cond else 'FAIL', detail))
fr = lambda n: R[n][0]; inp = lambda n: R[n][12]; st = lambda n: R[n][13] >> 24
check('boot: game running (frames advance)', fr('a1') > fr('a0') + 100, '%d -> %d' % (fr('a0'), fr('a1')))
check('attract: no credit', st('a1') == 0, 'state %02x' % st('a1'))
check('coin (key 5)', st('coin') != 0 and inp('a1') == 0, 'state %02x' % st('coin'))
check('start (key 1): game running', st('start') == 2, 'state %02x' % st('start'))
check('joystick right + fire', inp('joy') == 0x18, 'input %03x' % inp('joy'))
check('second button', inp('btn2') == 0x20, 'input %03x' % inp('btn2'))
check('running game: pictures change', not same('pre', 'mov'))
check('pause: picture frozen', same('pz1', 'pz2'))
check('resume: frames advance', fr('resume') > fr('joy') + 60, '%d -> %d' % (fr('joy'), fr('resume')))
check('Esc restarts: attract, no credit', st('restart') == 0 and fr('restart') < fr('resume') - 200 or fr('restart') < 2000, 'state %02x frame %d' % (st('restart'), fr('restart')))
check('free play (fire, no credit): game running', st('freeplay') == 2, 'state %02x' % st('freeplay'))
def span(n):
    i = img(n); rows = [y for y in range(i.size[1]) if any(i.getpixel((x, y)) != (0, 0, 0) for x in range(130, 660, 3))]
    return rows[0], rows[-1]
f, l = span('resume')
check('picture is 224 lines, nothing below it', l - f <= 2 * 224 + 4, 'rows %d..%d of the native screenshot' % (f, l))
print('BATTERY', 'PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)
