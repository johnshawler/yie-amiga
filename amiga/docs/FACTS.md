# Facts page — Yie Ar Kung-Fu (Konami GX407), Amiga 1200 port

Every fact is marked **[D]** from the MAME driver (`sources/mame/src/mame/konami/yiear.cpp`, mamedev/mame commit `2692d09218b67df92b9eb26b7c0c0cc04ca4c72e`, see `provenance.json`;
line numbers refer to that file), **[M]** from MAME 0.264 itself (the installed binary and, for the sound chips, its source `src/devices/sound/sn76496.cpp`, `vlm5030.cpp`, tag `mame0264`),
**[R]** from analysis of the ROMs / traces we produced, or **[A]** assumed.  Nothing below is taken from memory.

## Board (what is emulated)
| Fact | Value | Mark |
|---|---|---|
| Game / set | Yie Ar Kung-Fu, set `yiear`; ROMs `407_i08.10d`, `407_i07.8d` (program, 2 x 16 KB at $8000-$FFFF), `407_c01.6h`, `407_c02.7h` (tiles), `407_d03..d06` (sprites), `407c10.1g` (palette PROM, 32 bytes), `407_c09.8b` (VLM5030 speech ROM, 8 KB) | D (rom list), M (`mame -listxml yiear`) |
| Main CPU | one MC6809E at 18.432 MHz / 12 = 1.536 MHz, no second CPU, no MCU | D l.490 |
| Memory map | $0000 R VLM5030 status (BSY); $4000 W control (flip, NMI enable bit 1, IRQ enable bit 2); $4800 W SN latch; $4900 W SN strobe; $4a00 W VLM5030 control; $4b00 W VLM5030 data; $4c00 DSW2; $4d00 DSW3; $4e00 SYSTEM; $4e01 P1; $4e02 P2; $4e03 DSW1; $4f00 W watchdog; $5000-$502F sprite RAM 1; $5030-$53FF RAM; $5400-$542F sprite RAM 2; $5430-$57FF RAM; $5800-$5FFF video RAM; $8000-$FFFF ROM | D l.340-360 |
| Interrupts | IRQ at vblank (when enabled by the control port); NMI from a scanline timer at line 16 and every 32 lines afterwards ("music tempo") (8 per frame, 40-line gap) | D l.326-338, 493 |
| Screen | pixel clock 18.432 MHz / 3, 384 x 264 total, visible 256 x 224 (lines 16-239), one frame = 384 x 264 / 6.144 MHz = 16.5 ms = 25344 CPU cycles = 60.606 Hz | D l.499 |
| Banking / self-modifying code | none: all code is in ROM at $8000-$FFFF, RAM is never executed (the translator would stop otherwise) | R (coverage of 12000 frames + static discovery; the generated code has a trap for jumps into data) |
| Sound | SN76489A at 1.536 MHz (mono, route gain 1.0), VLM5030 at 3.579545 MHz | D l.512-514 |
| Video | 32 x 32 tile map of 8x8 tiles (4 bpp), 24 sprite entries of 16x16 (4 bpp, pen 0 transparent, sprites over tiles), palette PROM weights (pinned driver) blue 0x52/0xAD, red/green 0x21/0x47/0x97 | D (screen_update, palette), R (renderer is pixel-exact against MAME dumps) |
| Code | 8519 instructions in 3353 basic blocks, 2 interpreted instructions (no native template), 16 computed-jump sites ( 809F, 844C, 8B2E, 8BE3, 8D0F/8DE8/8E24/8EFC, 9250, 9863, A680, B895, C95C, C7EB/C805, AB32) | R (`translator/model.py`) |
| CPU time | 69 % of all arcade CPU time is spent in the idle loop $807C-$8081 (LDX <$DA; LDD ,X; ASLA; BCS) waiting for an interrupt | R (profile of the reference run) |

## SN76489A (model checked against MAME audio)
| Fact | Value | Mark |
|---|---|---|
| Model | MAME 0.264's `sn76496_base_device` configured as SN76489A: LFSR feedback mask 0x10000 (17-bit state), taps 0x04/0x08, clock divider 8, "sega" flag set (tone register 0 -> period 0x400), 2 dB volume steps from 8191, power-on: periods 0, volumes silent, register 3 selected, LFSR = 0x10000 | M (source) |
| Time base | MAME runs the chip at clock/2 = 768 kHz, one chip tick every 8 samples (first tick at sample 7); a write at CPU cycle c takes effect before stream sample ceil(c/2); MAME's output is the sum minus 1 LSB (the WAV writer truncates) delayed by one sample | R: `sound/snref.c` has **0 mismatches over 14.6 M samples** of MAME audio (1500 frames of scripted play), outside the VLM5030 phrases |
| Output scaling | the Amiga stream is the box average of that output over each Paula sample period, 1/128 of the 15-bit range per LSB, centred so that silence = -128 | A (design) |

## VLM5030
| Fact | Value | Mark |
|---|---|---|
| Stream rate | the integer `clock/440 = 8135 Hz`; MAME's mixer holds every chip sample for exactly 8 output samples when the output rate is 65080 | M (source), R (captured: edges always at output index = 1 mod 8) |
| Speech data | all 128 phrases (42 of them contain sound), 158,818 chip samples = 19.5 s, captured from MAME 0.264 by running the game's own start routine ($C89A) for each phrase in a separate MAME run (`ref/trace/vlm_busy.lua`, `tools/vlmcap.py`) | R |
| Why captured | the chip's unvoiced noise comes from `machine().rand()`; reproducing MAME's sequence is not possible outside MAME | M (source) |
| BSY durations | measured with the same runs; used by the board model (`runtime/vlm_dur.h`); entries differ from a first back-to-back run for 35 phrases the harness had mis-measured as 0 | R |

## Amiga target (assumptions, all marked A)
| Fact | Value |
|---|---|
| Machine | A1200: 68EC020 (also runs on 68020/030), AGA, 2 MB chip RAM, 2 MB fast RAM, Kickstart 3.1; PAL and NTSC.  Chip-RAM-only machines are **not** supported (the run area is 1.8 MB, see LIMITS) |
| Video | 256 x 224 dual playfield, 8 interleaved bitplanes, hardware does the sprite-over-tile composition (sprite pen 0 transparent); exact PROM palette in 24-bit AGA colours |
| Sound | Paula channels 0/1: SN76489A stream at period 124 (28604 Hz PAL, 28867 Hz NTSC, the fastest rate the DMA sustains); channels 2/3: speech at period 436 / 440 (8135 Hz) |
| Controls | joystick in port 2; fire = kick, second button = punch; with no credit, fire inserts a coin and starts a game (start = fire); keyboard: 5 coin, 1/2 start, P pause, Esc restart |
| Kickstart | used only for testing in FS-UAE; it is not part of the repository or of the ADF.  The ADF boots without any OS file: its bootblock takes over the machine |
