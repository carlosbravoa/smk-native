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

- All **24 tracks** (20 GP courses + 4 battle arenas) decoded from the ROM
- Real Mode 7 tiles and palettes, with the game's own per-tile palette-base
  remapping
- A resolution-independent perspective ground plane — the same projection the
  SNES builds with per-scanline HDMA, minus the 256×224 quantisation
- Fixed **60.0988 Hz** simulation tick, the SNES NTSC vblank rate that every
  duration in the original is counted in
- Free driving around any course

## What does not work yet

Being explicit, because the gap is large:

- **No karts, no items, no laps, no collision.** The camera flies over the
  track. Kart physics live in the ROM's own routines and have not been decoded.
- **No sound.** The SPC700 audio engine is untouched.
- **One tileset for every track.** All 24 tilemaps decode, but the per-course
  theme selection has not been traced, so every course renders with tileset 1.
- No sprites, no HUD, no menus.

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
