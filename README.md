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

`make run` opens the shell: **title → players → mode → driver and class →
course → race**.  **Grand Prix** runs a cup's five courses in the ROM's
order: after each race the times, then the points the ROM's table pays
the top four (9, 6, 3, 1), then the championship.  The next race lines
up in the order this one finished, its winner on pole, as the game does
(NOTES 275); finish fifth or worse and you run the course again.  **Single Race** is one cup course on its own.
**Time Trial** is five laps alone, a lap clock and splits, one mushroom,
and the five fastest laps per course kept between sessions (under
`$XDG_DATA_HOME/smk-port/laptimes.txt`).

```
menu            arrows move    enter selects    esc back
arrows / WASD   steer and accelerate      space  hop / drift
z or ctrl       use the mushroom          shift  boost
enter / START   pause                     alt+enter or F11  fullscreen
[  ]            previous / next track     o  p   cycle palette
f               toggle filtering          esc    back, quit at the title
```

It starts **fullscreen**; `alt+enter` (or F11) toggles at any time, and
`--windowed` starts in a window instead.  A run that names `--frames`
keeps the size it was asked for, so benchmarks and screenshots are
unaffected.

**Two players.**  The mode screen has a PLAYERS row: `1P`, `VS CPU` (the
right half of the screen follows a CPU kart) and `VS 2P` (a second person
on the second grid slot).  The split is **side by side**, left and right -
a deliberate deviation from the original, which stacks its two views
because it only has 224 lines to divide.  What the menu offers depends on
what is plugged in: with no controller `VS 2P` is unavailable, with one it
is the controller for player 1 and the keyboard for player 2, and with two
it is one each.

Useful flags: `--track N` (0–23) skips the shell and drives that course,
`--timetrial` makes it a solo five-lap trial, `--players 1|cpu|2` and
`--character2 N` set the split without the shell, `--width/--height`, `--pixel N`
(render at 1/N resolution — `--pixel 1` is native, `--pixel 4` is chunky and
retro), `--fullscreen`, `--frames N` (headless benchmark), `--fast` (one
simulation tick per frame, for headless runs), `--autodrive` (drive itself — a
crude test aid, not the shipped AI: it follows the course direction field
and recovers to the racing line when it leaves the road, which gets it round
most courses but not all), `--shot FILE` (render one frame to a BMP and
exit), `--pads FILE` (drive player 1 from a trained policy's own choices —
see [`docs/RL.md`](docs/RL.md)).

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
- A **time trial** that is the game's own: five laps because `$014C = $8500`
  and the grid sits behind the line (so five laps are six crossings), no
  coins and no item boxes, and the kart alone on the course — all measured,
  not assumed.  Menus draw with the ROM's own font and palettes, and the
  cup line-up and course names come from its own tables

Everything asset-side is verified byte-for-byte against the game's own
65816 code, executed in an interpreter (see below).

## What works now (updated)

- **Seven opponents** drive the ROM's own racing lines with its decoded
  speed classes, turn-rate tables, >90° turnaround, tangential wall bounce
  and Z-axis jumps.  In a lap harness the AI completes strict full laps on
  14 of 20 GP tracks at plausible times.
- **Laps and sectors** use the decoded rule (`$808994`): the crossing
  counts only on the finish strip, guarded by monotonic progress; sector
  capture follows `$808962` (off-course keeps the old sector; airborne
  rejects jump-zone sectors).
- **Sprites turn correctly**: the frame-selection rule was measured from
  the running game (22.5° + 11.25°·n boundaries, mirrored far half).

## What does not work yet

- **No items, no race timing/rank, no sound.**
- **The feel is only partly the game's.** Kinematics, the speed model and
  the acceleration curve are exact. What is still invented is *policy*:
  which target speed the player's input selects, the braking rate, and the
  steering rate. Drift, hop and per-surface response are undecoded.
- **No sound, no sprites, no HUD, no menus.**

Every shortcut is listed in the ledger in [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Training a driver

The port doubles as a headless deterministic RL environment - the same C
the window races, stepped from an action instead of a gamepad, at about
**36,000x realtime** on one core.  No emulator and no renderer are in the
loop.

```bash
make envtest          # the environment's gate, and its throughput
make envcheck         # prove it is frame-for-frame the same game as the window
make train TRACK=0    # PPO, in tools/rl/
make watch TRACK=0    # watch the trained policy drive, in the real window
```

Three million agent steps - about half a minute on one GPU - is enough
for a policy that finishes every lap of Mario Circuit 1 and beats
`src/autopilot.c` by 26 seconds over three.  The environment is verified
against the SDL game the hard way: a race is driven in the environment,
its inputs are replayed through the actual game binary, and the kart's
position and speed are compared on **every frame** - currently 8,966 out
of 8,966 identical.

What the ROM provides and what is ours - the observation, the actions,
the reward, the episode rules - is set out with its own ledger in
[`docs/RL.md`](docs/RL.md).

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
  env.c            the headless RL environment (docs/RL.md)
  main.c           SDL host: window, fixed timestep, input
tools/             the reverse-engineering toolkit (Python)
  smktool/         rom, disassembler, symbols, codec, graphics, assets
  rl/              the RL binding, PPO, and the env-vs-game replay gate
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
