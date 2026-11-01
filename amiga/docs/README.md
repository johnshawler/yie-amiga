# Yie Ar Kung-Fu (Konami, 1985) for the Amiga 1200

A port of the arcade game made by **mechanically translating the 6809 program** to 68020 code and emulating the board around it (see `DESIGN.md`).
Target: A1200 (68EC020, AGA), 2 MB chip RAM + **2 MB fast RAM**, Kickstart 3.1, PAL and NTSC.  Bootable floppy image, no OS files needed.

* **Controls**: joystick in port 2 (fire = kick, second button = punch; fire with no credit = coin + start), keyboard 5 = coin, 1/2 = start, P = pause, Esc = restart.
* **Disk**: `yiear_a1200.adf` (build: `make -C amiga/platform`; output `build/a1200/yiear_a1200.adf`), SHA-256 and MD5 in `CHECKSUMS.txt`.  The ROM set (`roms/`, MAME `yiear`) is not included; it is read at build time.
* Kickstart is only used for testing in FS-UAE (`KICK=...`), it is **not** part of the repository or the disk.
* Docs: `FACTS.md` (every fact with its source), `DESIGN.md`, `VERIFICATION.md` (what was tested and the results), `LIMITS.md` (what does not work yet, what to do next).
* **Status**: bit-exact game logic (RAM CRCs equal the reference through round 2), exact SN76489A and captured VLM5030 sound at Paula's top rate, PAL/NTSC.  Speed: 99.9 % in attract mode,
  98.1 % during play (about 95 % in the heaviest fights; see LIMITS.md).  **Not tested on real hardware.**

## Rebuilding and testing
```
sh amiga/harness/regress.sh 12000        # translated game vs reference, sound unit tests (needs qemu-m68k, m68k-linux-gnu-gcc)
sh amiga/translator/test/run.sh 100      # per-instruction tests
make -C amiga/platform                   # the ADF
KICK=/path/kick31.rom sh amiga/platform/test/battery.sh pal      # FS-UAE regression battery (also: ntsc)
KICK=/path/kick31.rom sh amiga/platform/test/audiotest.sh pal    # audio recording check
```
