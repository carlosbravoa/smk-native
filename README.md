# Super Mario Kart — native reimplementation

A PC executable that runs Super Mario Kart's real track data natively on
SDL2. No emulator, no ROM patching: the game reads assets out of a Super
Mario Kart (USA) ROM **you supply**, decompresses them in process, and renders
them with its own perspective renderer at any resolution you like.

**No game data is distributed here.** This repository is tools, formats and
addresses. See [`rom/README.md`](rom/README.md).

## Build and run

```bash
sudo apt install libsdl2-dev cmake build-essential     # or your equivalent
cp /path/to/your/smk.sfc rom/smk_usa.sfc
make game
make run
```

```
arrows / WASD   steer and accelerate      shift  boost
[  ]            previous / next track     o  p   cycle palette
t               cycle tileset             f      toggle filtering
esc             quit
```

Useful flags: `--track N` (0–23), `--width/--height`, `--pixel N` (render at
1/N resolution — `--pixel 1` is native, `--pixel 4` is chunky and retro),
`--fullscreen`, `--frames N` (headless benchmark), `--shot FILE` (render one
frame to a BMP and exit).

The renderer is single-threaded software and still does ~100 fps at 1920×1080,
so resolution is not a constraint.

## What works

- All **24 tracks** (20 GP courses + 4 battle arenas), each in **its own
  theme** — tileset, palette and surface data from the ROM's own tables
- Real Mode 7 tiles with the game's per-tile palette-base remapping
- **Solid walls**: the surface-behaviour table is the ROM's, and the test for
  "is this tile solid" is literally the one the game's collision path uses
- Kart **physics in the ROM's own arithmetic**: 16.16 position, 8.8
  velocity, 65536-unit angle, the exact integration from `$80879D`, the
  32-bit speed/acceleration model from `$80A4E1`, and the ROM's own
  **acceleration curve and target speeds** read at runtime (`--class`
  selects 50/100/150cc)
- The **player's kart drawn from the ROM's own sprite frames** — 32x32 4bpp,
  read at runtime; `--character 0..7` selects any of the eight drivers, each
  with its own sheet and palette
- The **real starting grid**, read off the game's own demo race, with the
  other seven karts drawn in world space and scaled by distance
- A resolution-independent perspective ground plane
- Fixed **60.0988 Hz** tick, the SNES NTSC vblank rate

Everything asset-side is verified byte-for-byte against the game's own
65816 code, executed in an interpreter (see below).

## What does not work yet

Being explicit, because the gap is still large:

- **No opponents, items, laps or lap timing.** Start lines and checkpoints
  are not decoded; the start position is a heuristic.
- **The kart sprite does not turn with the camera.** The frames are the
  ROM's and their three size tiers are identified, but which frame to show
  for a given heading is not decoded — reaching a race where the game
  actually draws karts is still open.
- **The feel is only partly the game's.** Kinematics, the speed model and
  the acceleration curve are exact. What is still invented is *policy*:
  which target speed the player's input selects, the braking rate, and the
  steering rate. Drift, hop and per-surface response are undecoded.
- **No sound, no sprites, no HUD, no menus.**

Every shortcut is listed in the ledger in [`docs/ROADMAP.md`](docs/ROADMAP.md).

## The oracle

`tools/smktool/` contains a 65816 interpreter that **runs the game's own
code**, which is how the port is verified: the C loader's output is diffed
against the ROM's actual loader rather than against expectations.

It **boots the ROM, uploads its sound driver, and runs a race** — with NMI,
IRQ, scanline timing and an APU handshake stub, at the game's own vblank
pacing. That is how the physics above was verified: `make verify-physics`
drives the real game and checks our integration against it, currently 0
mismatches over hundreds of steps.

It also models DMA, VRAM, CGRAM, OAM and — as of NOTES 039 — the **full
DSP-1 coprocessor**, with its command stream verified to never desync
through boot and racing. That verified the Mode 7 pipeline end to end
(tiles 100% identical to VRAM, tilemap 99.5%), located the kart sprites,
and now lets the game **draw its own race**: Lakitu, karts, shadows and HUD
all appear in OAM, which a small compositor can render for inspection.

It is not a general emulator — there is no background rendering, no SPC700
and no HDMA. It exists to answer questions about behaviour. See
`docs/NOTES.md` 018-039.

The plan to close the gap — phases, risks, and an explicit ledger of every
shortcut currently in the code — is [`docs/ROADMAP.md`](docs/ROADMAP.md).
The running decode log is [`docs/NOTES.md`](docs/NOTES.md).

## Layout

```
include/, src/     the native game (C11 + SDL2)
  rom.c            ROM loading, HiROM mapping
  lzc.c            Super Mario Kart's compression codec
  assets.c         pointer tables, tilemap/tileset/palette, tile expander
  mode7.c          perspective ground-plane renderer
  main.c           SDL host: window, fixed timestep, input
tools/             the reverse-engineering toolkit (Python)
  smktool/         rom, disassembler, symbols, codec, graphics, assets
romhack/           the ROM-patching path: asar patches + symbol database
docs/FINDINGS.md   what the ROM turned out to contain
```

## Verification

```bash
make selftest   # 9 checks through the C code the game actually runs
make test       # 25 checks: ROM identity, disassembler, codec, build loop
make roundtrip  # every disassembled instruction reassembles byte-identically
make shots      # a still from all 24 tracks
```

The C and Python implementations of the codec and asset layout are independent
and both are tested, so they cannot silently drift apart.

## The reverse-engineering toolkit

The native game exists because of the toolkit in `tools/`, which is still the
way to learn anything new about the ROM:

```
./tools/smk info                 cartridge, header, vectors
./tools/smk trace --coverage-map how much is understood as code
./tools/smk dis -s '$808000'     annotated 65816 listing
./tools/smk lin '$81E745' -n 20  linear disassembly with explicit M/X
./tools/smk jumptables           discover indirect dispatch tables
./tools/smk assets list          compressed asset inventory
```

The method is written up as a skill in
[`.claude/skills/snes-rom-reverse-engineering/`](.claude/skills/snes-rom-reverse-engineering/SKILL.md).

## Legal

This project contains no Nintendo code or data. It reads a ROM you already
own. Do not commit a ROM, an extracted asset, or a screenshot of one — the
`.gitignore` is set up to prevent it.
