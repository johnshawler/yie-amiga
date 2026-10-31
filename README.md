# yie

A Yie Ar Kung-Fu reference workspace collecting four complementary projects. This is research material, not a playable game, merged engine, or Amiga port. No proprietary game ROMs, graphics, audio, save states, or executables are included.

| Reference | What it provides | Included here |
| --- | --- | --- |
| NES disassembly | 6502 gameplay disassembly of the NES version; differs from the arcade game | Pinned upstream and fork links only |
| MAME | Arcade hardware emulation, memory map, controls, video and sound integration | Unmodified `yiear.cpp` snapshot and license notices |
| Jotego jtcores | FPGA hardware implementation and board documentation | Yie-specific HDL/configuration and Yie/Super Basketball READMEs, with GPL text |
| K80DS | Nintendo DS arcade emulator with Yie Ar Kung-Fu support | Pinned source and dependency references only |

## Getting the workspace

```sh
git clone https://github.com/johnshawler/yie.git
cd yie
python3 scripts/verify_snapshots.py
```

The checked-in source snapshots need no submodule download. They are scoped references, not standalone build targets: MAME needs the rest of MAME; the FPGA core needs shared Kicker/JTFRAME/CPU/sound components and the upstream build environment. See each pinned upstream for its dependencies and build instructions. Nothing here downloads or runs a game.

## Pinned references

All revisions were checked on 2026-10-31; full commit IDs and every copied file's upstream path, Git blob ID, and SHA-256 are in [provenance.json](provenance.json). Snapshots retain upstream bytes and directory structure.

### NES gameplay disassembly (reference only)

[Original upstream Yie directory](https://github.com/cyneprepou4uk/NES-Games-Disassembly/tree/df2c8e55e7f13e5792482527db9e7c9e4a84c6e1/Yie%20Ar%20Kung-Fu) · [originally discussed fork](https://github.com/alexcode-cc/axNES-Games-Disassembly/tree/fb519076bd38caf383df115a8b3e140d6a7a628e/Yie%20Ar%20Kung-Fu)

GitHub identifies `alexcode-cc/axNES-Games-Disassembly` as a fork of `cyneprepou4uk/NES-Games-Disassembly`. Both contain the exact directory `Yie Ar Kung-Fu`, including `bank_FF.asm`, `bank_ram.inc`, and `bank_val.inc`. The upstream calls this a rebuildable NES disassembly. It also includes `CHR_ROM.chr`, ROM-derived gameplay code, and bundled tools. No top-level license was declared in the inspected trees. These materials remain links rather than copied files or automatically fetched dependencies.

### MAME arcade driver

[Upstream driver](https://github.com/mamedev/mame/tree/2692d09218b67df92b9eb26b7c0c0cc04ca4c72e/src/mame/konami/yiear.cpp) · [local snapshot](sources/mame/src/mame/konami/yiear.cpp)

This describes and emulates the arcade machine; the original gameplay runs from separately supplied ROMs. ROM names/checksums in the driver are metadata, not ROM payloads. The driver identifies Phil Stroffolino as copyright holder and declares BSD-3-Clause. [COPYING](sources/mame/COPYING) explains MAME's broader licensing; [BSD text](sources/mame/docs/legal/BSD-3-Clause) accompanies this individual driver.

### Jotego FPGA hardware

[Dedicated Yie core](https://github.com/jotego/jtcores/tree/c09e20acb7fd73f1ace6d771bdde7fd5c5d139bf/cores/yiear) · [shared Kicker core](https://github.com/jotego/jtcores/tree/c09e20acb7fd73f1ace6d771bdde7fd5c5d139bf/cores/kicker) · [earlier sbaskt reference](https://github.com/jotego/jtcores/tree/c09e20acb7fd73f1ace6d771bdde7fd5c5d139bf/cores/sbaskt/README.md)

Start with [cores/yiear](sources/jtcores/cores/yiear), rather than treating the earlier Super Basketball link as the whole implementation. `cfg/files.yaml` identifies shared Kicker and sound dependencies; `cfg/macros.def` also imports Kicker configuration. Only Yie HDL/configuration and the two referenced READMEs are copied. Simulation save states, regression files, message data, board assets, and unrelated cores are omitted. The HDL declares GPL-3.0-or-later; the original [GPL license](sources/jtcores/LICENSE) is retained. The upstream README also notes consolidation into JTGNG; this workspace pins the specifically discussed jtcores revision.

### K80DS Nintendo DS emulator (reference only)

[Upstream repository](https://github.com/FluBBaOfWard/K80DS/tree/1ade47578850af6d8772c48876338455948e5763) · [Yie integration](https://github.com/FluBBaOfWard/K80DS/tree/1ade47578850af6d8772c48876338455948e5763/source/Games) · [pinned dependency definitions](https://github.com/FluBBaOfWard/K80DS/tree/1ade47578850af6d8772c48876338455948e5763/.gitmodules)

Relevant files are `source/Games/YieAr.c`, `YieAr.h`, and `YieArAsm.s`. Its pinned tree also references `source/YieArVideo`, ARM6809, SN76496, VLM5030 and other submodules. See upstream for exact submodule gitlinks and retrieval instructions. No top-level license was declared and the tree contains a bitmap, so no K80DS files or assets are redistributed here.

## Licensing and scope

This is an aggregation of separate references, with no blanket license applied to third-party work. MAME's individual driver is BSD-3-Clause; Jotego's copied HDL is GPL-3.0-or-later. Preserve their notices and follow the applicable terms when reusing files. Reference-only repositories do not acquire a license by being listed here. Original game content remains subject to its owners' rights.

To update a snapshot, deliberately select a new upstream commit, inspect the selected files for game data and licensing changes, copy only the documented scope, update `provenance.json`, then run the verifier. Do not replace pinned links with branch-head links or bulk-import upstream ROM/assets directories.
