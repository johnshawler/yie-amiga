# Limits and remaining improvements

## What is true today (honestly)
* **Speed**: attract mode runs at 99.9 % of the arcade (PAL and NTSC, FS-UAE A1200); **play runs at 94-97 %** (scripted play: 97.2 % PAL, 96.3 % NTSC over 6000 frames, 94.6-95.1 % in the heaviest fights).  The target
  (100 % during play) is **not yet met**.  The game stays in step with the arcade's logic (the CRC checks pass), it just runs a few percent slow in busy scenes, skips some pictures and can starve the sound.
* **Sound**: correct stream (verified), but in busy scenes the stream can underrun (audible as a short gap and a slight delay of the following sound; the lead is cut back by dropping silent blocks
  only, never sound).  Measured: ~1 underrun per 15 s in attract mode, more in heavy play.  Latency of the music against the picture: 5 blocks = 83 ms.
* **8-bit output**: Paula is 8-bit; the SN76489A stream uses the full -128..127 range with a 16-phase ordered dither (quiet notes of 12 dB below full scale are only ~2 LSB).
* **Memory**: needs **fast RAM** (the run area is 1.8 MB: the translated code with its single-instruction tail routines is 500 KB).  A chip-RAM-only A1200 shows a magenta screen (allocation failed; tested in FS-UAE with no fast RAM).
* **Disk**: payload 487 KB (LZ4-compressed image) + 53 KB (relocations) of the 880 KB floppy image; loading takes ~20 s from a real drive (estimated from the floppy data rate, not measured).
* The bootblock refuses 68040/68060 (cache handling is the 020/030 one).
* No scores are saved, no service/test mode switches (not mapped to keys); the DIP switches are the driver defaults.
* **Not tested on real hardware.**  FS-UAE only (see VERIFICATION.md).

## Remaining improvements (gain / risk / cost), in the order I would do them
| # | Improvement | Gain | Risk | Cost |
|---|---|---|---|---|
| 1 | Native NMI/IRQ entry: today every interrupt (9 per frame) goes through the C reference core (state spill, `board_fire`, one interpreted step) = ~7 % of the CPU in play | +5..6 points of speed (play ~100 %) | medium: the event timing must stay cycle-exact (the whole-program comparison catches errors) | 1-2 days |
| 2 | Cheaper SN76489A synthesis: ~25-30 % of a frame in sound-heavy scenes (60 us per event x ~50 events); fewer instructions per event (batched box fills, per-channel fast paths, 68k assembly for the writes), maybe fixed-point positions | +8..12 points of speed, fewer underruns | low (bit-exact tests against the C loop exist) | 1-2 days |
| 3 | Renderer on the Blitter (sprite clearing/restoring, tile updates) | +5..8 points (11 % of the CPU today) | medium (AGA blitter/DMA contention) | 2 days |
| 4 | 14-bit SN output (two Paula channels per ear) | +6 dB resolution for quiet notes | medium: needs a channel budget decision (speech on the same pair) | 1 day |
| 5 | Chip-RAM-only operation (drop the tail routines, ~1.35 MB) | runs on stock A1200 | low | 0.5 day |
| 6 | Better payload compression (LZ4-HC/delta-coded relocations) | ~5 s shorter load, room on the disk | low | 0.5 day |
| 7 | Real-hardware test | the only thing that proves it | none | needs hardware |
| 8 | Smarter audio resync after an underrun (resume at the next sound, not after the 5-block pre-roll) | shorter dropouts | low | 0.5 day |
