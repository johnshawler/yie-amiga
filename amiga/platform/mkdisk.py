#!/usr/bin/env python3
"""mkdisk.py out.adf boot.bin yiear.elf
Builds the boot disk: the payload (text + rodata + data of the linked image, then the relocation table) is stored from ADF byte 1024; the header
fields of the bootblock (marker 'YIEL' at offset 16, then payload_len, load_len, total_len, link_base, entry, reloc_off, reloc_count, chip_base,
chip_len) are patched in; mkadf.py adds the bootblock checksum.  The relocation table lists every 32-bit absolute pointer of the image (R_68K_32
relocations kept by `ld -q`) so that the bootblock can run the image at any address (FAST RAM when there is any)."""
import os, re, struct, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
mkadf = os.path.join(here, '..', 'tools', 'mkadf.py')
out, boot, elf = sys.argv[1:4]
PREFIX = os.environ.get('PREFIX', 'm68k-linux-gnu-')
def sym(name):
    for l in subprocess.check_output([PREFIX + 'nm', elf]).decode().split('\n'):
        p = l.split()
        if len(p) == 3 and p[2] == name: return int(p[0], 16)
    raise SystemExit('symbol %s missing' % name)
base, load_end, end, entry = sym('_payload_base'), sym('_load_end'), sym('_end'), sym('_start')
chip0, chip1 = sym('_chipbuf_start'), sym('_chipbuf_end')
with tempfile.NamedTemporaryFile(delete=False) as t: binp = t.name
subprocess.check_call([PREFIX + 'objcopy', '-O', 'binary', elf, binp])
img = bytearray(open(binp, 'rb').read()); os.unlink(binp)
assert len(img) == load_end - base, (len(img), load_end - base)
while len(img) % 4: img.append(0)
# relocations
rel = []
for l in subprocess.check_output([PREFIX + 'readelf', '-r', '-W', elf]).decode().split('\n'):
    m = re.match(r'\s*([0-9a-f]{8})\s+[0-9a-f]+\s+R_68K_(\w+)', l)
    if not m: continue
    off, typ = int(m.group(1), 16), m.group(2)
    if typ == 'PC32' or typ == 'PC16' or typ == 'PC8': continue
    if typ != '32': raise SystemExit('unsupported relocation R_68K_' + typ + ' at %08x' % off)
    if not (base <= off < load_end): continue
    v = struct.unpack_from('>I', img, off - base)[0]
    rel.append(off - base)
rel.sort()
tab = struct.pack('>%dI' % len(rel), *rel)
payload = bytes(img) + tab
n = (len(payload) + 511) & ~511
assert 1024 + n <= 880 * 1024, 'payload too big for one disk: %d' % n
bb = bytearray(open(boot, 'rb').read())
assert bb[16:20] == b'YIEL', 'bootblock marker missing'
struct.pack_into('>9I', bb, 20, n, len(img), end - base, base, entry, len(img), len(rel), chip0, chip1 - chip0)
with tempfile.NamedTemporaryFile(delete=False) as f: f.write(bb); tmp = f.name
with tempfile.NamedTemporaryFile(delete=False) as f: f.write(payload); pl = f.name
try: subprocess.check_call([sys.executable, mkadf, out, tmp, pl])
finally: os.unlink(tmp); os.unlink(pl)
print('%s: bootblock %d bytes, image %d bytes + %d relocations, run area %d bytes, chip region %06x-%06x' % (out, len(bb), len(img), len(rel), end - base, chip0, chip1))
