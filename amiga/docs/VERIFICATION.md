# Verification — what was tested, how, and what it showed

Everything ran in this repository's CI-less environment: a PC with MAME 0.264, qemu-m68k and **FS-UAE** (A1200, 68EC020, 2 MB chip + 2 MB fast, Kickstart 3.1 40.68 supplied by the user; the ROM is *test only* and is
not in the repository or in the ADF).  **Nothing was run on real hardware.**  Commands are relative to the repository root; `KICK=/path/to/kickstart.rom` for the FS-UAE tests.

## 1. Reference emulator vs MAME (the foundation)
`amiga/ref/src/m6809.c` + `amiga/runtime/board.c` (driver `yref`) against MAME 0.264 (`ref/trace/mame_trace.lua`, `cmp_trace.py`): 12,000 frames of scripted attract + play: **every I/O event with its cycle stamp
identical (634 k events)**, RAM, registers and cycle counter per frame identical.  (0.264 timing mode `-t mame0264` for the comparison; the shipped timing is the pinned driver's: 25344 cycles per frame.)

## 2. Translator (whole program) — `sh amiga/harness/regress.sh 12000`
* **Per-instruction tests** (`sh amiga/translator/test/run.sh 100`): 3698 generated cases x 100 random states against the reference core: **369,800 runs, 0 failures**.
* **Whole program** under qemu-m68k: translated game vs `yref`, per frame RAM + CPU registers + cycle counter, and every I/O access with cycle stamp: byte-identical for 12,000 frames
  (`inputs_2.txt`: 435,794 I/O events, 0 different).  The shipped configuration (specialised I/O ports, single-instruction tail routines) is also byte-identical.
* **Renderer** (`amiga/harness/rtest.c`): pixel-exact against MAME's screen for the frames MAME dumped.

## 3. Sound
* `amiga/sound/snref.c` (SN76489A, transcribed from MAME 0.264): **0 mismatches over 14.6 M samples** of MAME's audio at its native 768 kHz rate, on 1500 frames of scripted play (outside the VLM5030 phrases).
* `amiga/sound/sntest.c`: the event-driven synthesiser against the per-sample model: box averages within 7 units of the 32768 range (0.05 LSB of the 8-bit output) on the real chip-write streams,
  within 14 on random register stress (every register, mode and period 0/1 included), for 470-478 samples per frame; the quantiser output is checked against its definition (0 mismatches);
  the m68k build runs under qemu and the **assembly core is bit-identical to the C loop** (`SNS_FORCE_C=1`, checksums of 3000+ frames of several streams).
* `amiga/sound/vlmtest.c`: all 128 speech phrases decode from the LZ4 blob to exactly the captured 8-bit data (158,818 samples, 0 differences).
* **Audio recording** (`sh amiga/platform/test/audiotest.sh pal|ntsc`): a self-test build (no game; `tools/mkaudiotest.py`: tones, chords, all noise modes, five speech phrases, one cut by the next) is run in FS-UAE,
  Paula's output is recorded (OpenAL wave backend, 48 kHz) and compared with the stream `sound/sndpc.c` computes from the same script (`tools/audiocheck.py`: 0.25 s windows, delay and +-0.6 % clock error
  fitted, because FS-UAE adapts its audio clock to real time).  **PAL: 56 of 56 windows correlate > 0.9 (median 0.998), gain 76.7 per stream unit (predicted 76.8); NTSC: 53 of 56 (median 0.996).**

## 4. Platform, in FS-UAE (PAL and NTSC)
* **Regression battery** (`sh amiga/platform/test/battery.sh pal|ntsc`, HUD build): boot (frames advance), attract with no credit, coin (key 5), start (key 1), joystick right + fire (input bits), second button,
  picture changes while running, **pause freezes the picture and resume continues**, Esc restarts to attract with no credit, **free play** (fire with no credit starts a game): **all 11 checks PASS in PAL and NTSC** (the last one, "the picture is 224 lines and nothing is below it", was added after a look at the NTSC screenshots showed ~16 lines of garbage under the picture: the DIWSTOP vertical high bit was set for PAL's stop line 284 but NTSC's is 253; fixed).
* **Soak** (`build SOAK`, `tools/mkbot.py`, `tools/mksoak.py`): scripted play (coin, start, 6000 frames of random attacks; it fights BUCHU and reaches round 2 against STAR by frame 3000) injected as the
  joystick state; the CRC32 of the game RAM `$5000-$5FFF` at frames 3000 and 6000 is read from the screen: **`B3F7774B` and `C1B1380B`, identical to the reference emulator, in PAL and NTSC (final build).**
  (The first, 100, 1000-frame variants with no input also matched: `D9818832`, `3BD04FBB`.)  So the Amiga build runs the game bit-exactly through round 1 into round 2.
* Inputs checked through the HUD: arrows, fire (Z in the test configuration), second button (X), space, 1, 5 (the FS-UAE keyboard-joystick mapping is set in `test/*.fs-uae`).

## 5. Speed (measured, FS-UAE; method: frame counter vs the 60.606 Hz CIA tick counter over a >= 100 s window, HUD shown only at the two ends)
| Scenario | PAL | NTSC |
|---|---|---|
| Attract mode (sound on), 6320 frames | **99.9 %** (idle 27 %) | **99.9 %** (idle 23 %) |
| Scripted play, coin + start, rounds 1-2, frames 1045 to 7253 (sound on) | **98.1 %** (idle 6.8 %) | **98.1 %** (idle 6.3 %) |
| Heaviest stretch of that play (frames ~1000-3300, fights with sound) | ~95 % | ~95 % |
"Speed" = emulated arcade frames / real-time ticks; 100 % means the arcade's 60.606 Hz.  Below 100 % the game runs slightly slow and skips pictures (up to 3 in a row); sound underruns are then possible
(measurements below and LIMITS.md).  Sound-stream health in the same windows (104 s, DIAG HUD build, PAL): attract: **2 underruns**, 0 blocks with sound lost; scripted play: **13 underruns** (one per 8 s, each a gap of ~80 ms), **0 blocks with sound lost** (the game waits instead), 30 silent blocks dropped to bound the latency.
History of the play figure (sound on): 88 % with the first C synthesiser, 95 % with the assembly event loop, 98.1 % with the interrupt fast path.

## Not tested
Real hardware of any kind; Kickstart versions other than 3.1 (40.68); other CPUs (68030/040/060 beyond the bootblock's refusal of 040/060); chip-RAM-only machines (unsupported); accelerated
machines' timing; joysticks other than via FS-UAE's keyboard mapping; the A500/OCS path (not part of this port).

## Long play-through with infinite lives (crash hunt)

Tool: `ref/src/ybot.c` is a lookahead bot: it forks the reference emulator, tries candidate joystick actions for a few frames in child processes and keeps the best (score, lives lost, progress), so it keeps playing without a human. Infinite lives are a RAM poke (`harness/cheat.h`, env `INFLIVES=1`): while a game runs (`$51C3 == 2`) the real lives counter `$5430` is put back to 3 when it falls to 1 or 2. The same rule runs in `yref`, `yrun` and the Amiga SOAK build. (`$5430` was found from the game-over test at `$8F1B`; an earlier attempt that poked `$51A8` was the wrong variable, and the game over at stage 22 seen with it was simply running out of lives.)

Results (all measured here, nothing on real hardware):
- With the correct cheat the game never ends: two bot runs of 300 000 frames each (about 83 minutes of game time per run, 24 and 42 deaths) stay in game state 2 throughout, pass the last opponent (BLUES) many times and keep looping through the opponents; the score display wraps back to 0 after about 1 000 000. No game over, no crash, no kill screen appeared. The original ROM therefore does not seem to have a kill screen that this bot can reach (not proven: only that none appeared in about 166 minutes of play).
- The translated build (`yrun` under qemu-m68k) produced per-frame RAM + register dumps byte-identical (`cmp`) to the reference for both 300 000-frame runs (1.2 GB each), 0 illegal opcodes, no `rt_fatal`. Earlier runs (about 507 000 frames, lives variable then wrongly poked) were also identical.
- Amiga SOAK build (FS-UAE, PAL), RAM `$5000-$5FFF` CRC32 equal to the reference at frames 30000, 40000, 45000, 52000, 54000 and 58000 of run L21 (inputs: `ref/trace/bot_L21_inputs.txt`, cut at frame 64500 because the soak script uses 16-bit frame numbers), after earlier matches at 3000..56000 on another run. Only checkpoints were compared on the Amiga, not every frame. Two attempts to read a checkpoint at frame 60000 gave a black screenshot instead of the HUD (the reading is taken after about 1150 s of FS-UAE time); the cause was not found, and nothing in the 58000-frame results points to a game crash, but it is unresolved.

## Sprite flashing fix (picture buffer hand-over)

Symptom reported: the players flash constantly. Cause (found by reading the code and confirmed by a counter in the DIAG build): a picture was drawn whenever a vertical blank had passed since the last one was *started*. If a blank came while a picture was being drawn, the next frame saw a new blank count while the previous picture's buffer was still waiting to be switched in, and it drew into the buffer that was on screen: the sprite layer is cleared and redrawn in place, so the players were visible only part of the time. In a 5000-frame scripted run (DIAG build, game running at about 94 % of arcade speed) this situation arose 476 times against about 1 065 pictures drawn. Fix: a picture is only drawn when no earlier picture is still waiting to be shown (`plat_show_pending()`); the frame is skipped instead. Checks after the fix: renderer output unchanged (sprite RAM vs drawn sprite plane: 0 mismatches), regression battery PASS in PAL and NTSC, RAM CRC of the soak unaffected (rendering does not touch game RAM). Not seen on real hardware; the earlier screenshots could not show the flash directly because a screenshot rarely lands in the middle of a redraw, so the proof is the counter, not a before/after picture.
