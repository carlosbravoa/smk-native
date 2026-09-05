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

`make run` opens the shell: **title → players → mode → class → driver →
course → race**.  **Grand Prix** runs a cup's five courses in the ROM's
order: after each race the times, then the points the ROM's table pays
the top four (9, 6, 3, 1), then the championship.  The next race lines
up in the order this one finished, its winner on pole, as the game does
(NOTES 275); finish fifth or worse and you score nothing, and the cup
goes on.  The finishing list - the game's own faces - comes up as karts
finish; the minimap shows every kart as its face; fifteen seconds after
the fourth kart is home the race is over at the positions held, or the
moment everybody is (NOTES 282/288).  **Single Race** is one cup course on its own.
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
right half of the screen is a CPU kart) and `VS 2P` (a second person on
the second grid slot).  **The CPU is a neural network**: a policy trained
by reinforcement learning in the port's own headless environment, built
into the binary (`src/netpolicy.inc`) and pressing the same buttons a
person would through the same player physics, with no privileged control
over its kart.  The dashboard under the speed dial says who is driving
each view: `P1`, `P2`, `NEURAL`, or `AUTO` for the scripted fallback,
which still takes the battle arenas and any build without weights.  The split is **side by side**, left and right -
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
exit), `--pads FILE` (drive player 1 from a trained policy's own choices)
and `--cpu-policy FILE` (a different network for the CPU than the built-in
one) — both in [`docs/RL.md`](docs/RL.md) — and `--cpu-rules original|fair`,
the class screen's CPU RULES row: FAIR makes the field pay for grass and
ramps and halves its handicap bonus ([`docs/AI.md`](docs/AI.md)).  `SMK_POLICY_TRACE=1` prints
when the network actually takes the wheel.

The renderer is single-threaded software and still does ~100 fps at 1920×1080,
so resolution is not a constraint.

## What works

- All **24 tracks** (20 GP courses + 4 battle arenas), each in **its own
  theme** — tileset, palette and surface data from the ROM's own tables,
  drawn as real Mode 7 tiles with the game's per-tile palette remapping on
  a resolution-independent perspective plane, at a fixed **60.0988 Hz**
- Kart **physics in the ROM's own arithmetic**: 16.16 position, 8.8
  velocity, 65536-unit angle, the exact integration from `$80879D`, the
  32-bit speed/acceleration model from `$80A4E1`, the ROM's acceleration
  curves and target speeds per class, the surface table, drift, hop, the
  turbo start and the 336-frame countdown — gated by replaying two human
  runs through the port frame-exact; the water's wade, sink and
  Lakitu's fishing measured from two more (NOTES 283)
- **Three modes.**  Grand Prix: four cups of five, the ROM's 9/6/3/1 to
  the top four, points and championship screens, the next grid from the
  last race's order, coins by grid slot, no retry (fifth or worse
  scores nothing), the finishing list as karts finish, a fifteen second
  cooldown after the fourth kart, the trophy.  Single Race.  Time Trial, with the top five laps per course
  kept on disk.  Two players **side by side** - each half's whole sound on
  its own speaker - or one player against the neural CPU
- **Seven opponents** on the ROM's own racing lines: its direction field,
  speed classes, rubber band (`$80ADA0`), kart-to-kart contact with the
  weight table, ramp launches, wall escapes, Lakitu's rescue; all 20 GP
  courses lapped
- **The CPU opponent is a trained neural network** (below), pressing
  buttons through the player physics like a person
- **Items**: the roulette, the nine items, their projectiles, hits and
  effects, the AI's own weapons — decoded from the ROM and measured on the
  running game ([`docs/ITEMS.md`](docs/ITEMS.md))
- **Sound effects** rendered from the game's own BRR samples, by its own
  ids: four engines, six surfaces, the voices, the held sounds.  Music is
  pre-recorded from your own ROM and mapped by you
  ([`docs/SOUND.md`](docs/SOUND.md)); off by default
- **The furniture**: the HUD on the game's own art, Lakitu and his light,
  the flag, the lap sign, the Thwomps, moles, cheep-cheeps, pipes,
  breakable blocks, water and the fall, the horizon per theme, the
  winner's pose, the squash, a track map
- Karts, objects and effects from the ROM's sprite sheets, with the
  rotation frames and the size ladder measured off the running game

## What is still open

The **menus are our own layout** in the ROM's font and palettes, not its
tilemaps; the AI has no per-character personality; the cup's finish
(no retry, a fifteen second cooldown) is the user's rule, not the
game's; some sound ids are captured but not wired.  Every shortcut and every approximation is a labelled
entry in the ledger in [`docs/ROADMAP.md`](docs/ROADMAP.md), with the
measurement that would close it.

## The neural CPU, and training your own

The port doubles as a headless deterministic RL environment - the same C
the window races, stepped from an action instead of a gamepad, at about
**36,000x realtime** on one core.  No emulator and no renderer are in the
loop.  **The shipped VS CPU driver came out of it**: a two-layer MLP, 81
inputs, two 256-unit layers, 14 actions, trained with PPO (300 lines of
PyTorch in `tools/rl/`, no framework) across all three engine classes and
three situations - alone, against the field, against the field with
items - on 16 of the 20 GP courses, with four held out to show it learned
to drive rather than twenty routes.

What it sees is **not pixels** and **not where it is**: velocity in its
own frame, the slip angle, the next four waypoints as bearings, its
offset from the racing line, twelve rangefinders, the ROM's own direction
field, its rank, the nearest three karts, the item held and the nearest
projectile.  No absolute position, no course identity, no clock - so it
cannot memorise a route and cannot contain one.  The weights
(`src/netpolicy.inc`, int8 with a float scale per row, 84 KB) are ours:
parameters fitted by gradient descent, with no ROM bytes in them.

```bash
make envtest          # the environment's gate, and its throughput
make envcheck         # prove it is frame-for-frame the same game as the window
make train TRACK=0    # PPO on one course; --gp --tracks gp --classes 0,1,2
                      # --holdout 3,11,16,19 is how the shipped one was made
make watch RUN=runs/track0   # watch the CURRENT policy drive, in the real
                             # window - safe while the training is running

# race against a policy of your own instead of the built-in one
python3 tools/rl/export_net.py runs/track0/policy.pt -o cpu.net
./build-native/smk --players cpu --cpu-policy cpu.net
make embed-policy NET=cpu.net && make game     # or make it the default
```

The CPU driver **presses buttons** - it is a full `smk_player` in its own
grid slot, going through `smk_player_step` like a person's keyboard, with
no privileged control over its kart, and it decides once every four
frames, the rate it learned at.  Inference is 30 lines of C (`src/net.c`);
there is no runtime to link.  Off the 20 GP courses, or in a build with
no weights, the scripted driver in `src/autopilot.c` takes over and the
dashboard says `AUTO` instead of `NEURAL`.

The environment is verified against the SDL game the hard way: a race is
driven in the environment, its inputs are replayed through the actual
game binary, and the kart's position and speed are compared on **every
frame**; then the same race is run on both sides and all 81 observation
numbers are compared at matched frames.  What the ROM provides and what
is ours - the observation, the actions, the reward, the episode rules -
is set out with its own ledger in [`docs/RL.md`](docs/RL.md).

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
  net.c            the trained CPU driver's forward pass; netpolicy.inc its weights
  main.c           SDL host: window, fixed timestep, input
tools/             the reverse-engineering toolkit (Python)
  smktool/         rom, disassembler, symbols, codec, graphics, assets
  rl/              the RL binding, PPO, and the env-vs-game replay gate
romhack/           the ROM-patching path: asar patches + symbol database
docs/FINDINGS.md   what the ROM turned out to contain
docs/AI.md         the opponents: every way the game's AI plays by other rules
```

## Verification

```bash
make selftest   # 107 checks through the C code the game actually runs
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
