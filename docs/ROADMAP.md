# Roadmap: a faithful native Super Mario Kart

Goal: the game running on PC, SDL2, no emulator, **as faithful as possible**
to the SNES original. "Faithful" means: behaviour derived from the ROM's own
code and data, not from how it looks in videos or how another remake did it.

This file is the single place where we are honest about the gap between the
two. Every shortcut lives in the ledger below; a shortcut not written down
here is a bug in this file.

---

## Working principles

**1. Decode first, write later.**
The expensive, uncertain work is reading the 65816 — the C is easy once the
truth is known. So each phase front-loads the reverse engineering: pull the
tables, decode the routines, prove the format with throwaway scripts, and
only then write engine code. Writing the engine first and "filling in the
real values later" is how inherited fiction becomes permanent — every value
we invent today reads like a decoded fact in six months.

**2. Build the oracle before the port.**
For behaviour (physics especially), the strongest verification is to run the
game's *own* routines and compare. Plan: a minimal 65816 interpreter (we
already have the full opcode table and flag model in `tools/smktool/`) that
can execute an isolated ROM routine over a RAM snapshot. Feed both the
original routine and our C port the same state, diff the outputs across
thousands of states. This is more work up front than eyeballing — and it is
the *only* way to know the physics is right rather than plausible. This is
the "complex task first" rule applied: the oracle is the complex task.

**3. The game's arithmetic, not equivalents.**
The original is 16-bit fixed-point with wraparound, signed shifts, and
lookup tables. Reimplement in the same integer arithmetic. Float
"equivalents" drift, and drift in a racing game is feel.

**4. Every invented value is labelled at the point of use.**
A `PLACEHOLDER`/`NOT the game's` comment in the code, and a row in the
ledger here. When a phase replaces one, delete the row.

**5. One decode log.**
`docs/NOTES.md`, numbered entries, addresses included, ruled-out hypotheses
kept and marked superseded rather than deleted. Negatives stop us
re-investigating.

---

## Shortcut & assumption ledger (current)

| # | where | what we do | what the game does | phase |
|---|---|---|---|---|
| S1 | `src/player.c` | **RESOLVED** — the player's control is the ROM's own, transcribed and verified frame-exact against the demo race (NOTES 106-108): per-character top speed, acceleration table, surface caps, steering rows and drift row, the slide machine, spin-out, hop, coins on the target, the mushroom boost. Residual, labelled: coins are not collected yet (P5), the sprite's steering lean is synthesised, the DSP-1 sine is +-1 on 3% of frames, snow/splash effects | same | closed |
| S2 | `src/course.c` `smk_course_start` | grid geometry synthesised from the finish strip (two columns, 24 px rows) | the per-track record at `[$0C],y` (`$819207`: cell + 4/10 px offsets) - located (NOTES 111), not ported; the CHARACTERS per slot are the ROM's (`$81EE97`, ported) | P2 |
| S4 | `src/mode7.c` camera | **RESOLVED** — the projection is derived from the ROM's own DSP-1 geometry: `depth(L)=4972/(L-20.36)`, `scale=depth/256` (the ratio is exactly `Les`=256, the cross-check), camera trails the kart 61 world px (NOTES 083/084) | the DSP-1 builds per-heading scanline tables at boot; HDMA feeds them to M7A-D | closed |
| S5 | `src/mode7.c` sky + `src/horizon.c` | **mostly**: backdrop colour and character-0 fill measured (NOTES 114); the far horizon plane is the ROM's own `gfx_d` tiles arranged by the `gfx_e` map, byte-matched against the game (NOTES 117).  Labelled: the scroll law and which map rows show.  Missing: the NEAR parallax plane (ghosts, arches) and the sky gradient | two scenery planes at different scroll speeds over a per-theme gradient | P5 - **next** |
| S6 | `src/kart.c` bounce | **RESOLVED** - measured frame by frame in the running game (NOTES 125): the impact mirrors the blocked component and leaves the speed alone; the frame after, each axis is scaled by the pair `$56` selects (`$80` reflected / `$F0` the other) and `$EA` is re-derived from the vector.  Four captures are a selftest.  Labelled: the `$12`-negative path and the `$500` fast-hit path are the ROM's text, unmeasured; the stuck-in-a-wall eject is not ported (we prevent entry) | same | closed |
| S6-old | `src/kart.c` bounce | **decoded**: the bounce is a ballistic launch (`$80F8C0` sets `$26`=$0080), not a timed knockback (NOTES 045); the wall hit's ~9-frame velocity freeze is MEASURED by displacement (NOTES 092) - `$42,x` turned out to be the HUD rank timer, not a window (NOTES 112) | per-class differences; the horizontal knockback magnitude | P3 residual |
| S7 | renderer | full-resolution smooth perspective | 256×224, per-scanline integer matrix | keep — named divergence, this is the point of a PC port. `--pixel` restores chunk. |
| S8 | no audio | silence | SPC700 + S-DSP running its own program | P7 |
| S10 | `src/main.c` draw | karts AND entities sized by the game's own measured law (+$06 = 0x4200/kart distance), the sheet tier choosing only which drawing; the 2x magnification cap comes from the reference screenshot, not the ROM | same | **open, parked** — playable, but playtest 2026-08-25: distant pipes draw too small, and a pipe beside the kart may still be slightly small (not yet measured). The ROM-side answer is wherever the game turns `+$06` into a tile choice; the demo never draws entities, so it needs a rig that is not the attract race (NOTES 105) |
| S11 | `src/main.c` start sequence | 3-2-1 countdown at 60 frames a step, karts held | the ROM's own start-frame count and Lakitu's light art | P5 |
| S12 | `src/main.c` entities | the theme's own object art (pipes, Thwomps, ...) drawn from `$81:EBD3`, one size tier scaled continuously; entities do not move | the sheet stores a size TIER per distance band; `$84:DD15` drives type and motion (a Thwomp rises and slams) | P5 |
| S14 | `src/course.c` direction field | the AI/rescue direction field is our atan2 of the waypoint delta, rounded.  MEASURED against the game's own `$7F:4000` (track 7): **2554 of 2684 cells exact, 130 off by one step of 1/256 turn, worst error 1** | `$81FCFC` builds it through the boot-time arctangent table at `$7F:9000` (`$81E4C5` generates it, `$81F638` reads it as octant base + `table[min*64+max]`) | labelled at that number; port the table if a divergence is ever traced to it |
| S13 | `src/player.c` per character | **decoded and read from the ROM** for the five tables the game has: base top speed (`$81:8000`), acceleration curve (`$81:8010`), off-road caps (`$81:8060`), steering rows (`$81:8088`) and the drift-row adjust (`$80A4C0`: Yoshi/Koopa slide one row lower). Only Mario (P1) and Toad (P2) are VERIFIED against the game so far - the other six characters run on the same code with their own tables but have not been replayed | every character handles differently; there may be further per-character factors (kart-to-kart weight, item odds) not yet found | P3 residual: replay each character (needs a log per character - a real race, not the attract demo) |
| S9 | `tools/smktool/dsp1.py` | full command set implemented; stream never desyncs; camera model verified against the game's own usage. Residual: gyrate is a passthrough, and raster/`$08`/`$18` scalings are unchecked | the real chip's exact fixed-point pipeline | largely closed (NOTES 039); residuals logged on first contact |

*Resolved:* **S9 for command `$04` (sin/cos)** — pinned by unit analysis in
NOTES 017; movement no longer rests on a guess. The kinematics (velocity
construction and position integration) are now the ROM's own, in
`src/kart.c`.

*Partly resolved:* **S2** (start positions) — the grid is real and confirmed
against the demo race on track 7; 5 of 24 courses still need their own.

*Resolved:* **S3** (per-course theme) — the ROM's own table `$81EC2F` is now
used; C output is byte-identical to the game's loader on all 24 courses.

---

## Status at a glance

| phase | state |
|---|---|
| P0 oracle | **done** — 65816 interpreter, verified against the game's own decompressor |
| P0.5 running machine | **mostly** — boots, uploads sound, runs races; no PPU picture, no SPC700, no HDMA |
| P1 the track | **done** — themes, tilemaps, tilesets, palettes, surface table, all verified against VRAM |
| P2 start / laps | **mostly** — real grid, decoded lap rule (NOTES 052) with the monotonic guard, race clock and start countdown.  Residual: finish/results flow, GP points |
| P3 physics | **done for the player** — the control is transcribed from the ROM and replays the attract race's human inputs frame-exact: 99.8% / 100% of frames within 1 px (NOTES 106-108), with tyre smoke and dust from the game's own effect object (NOTES 109).  The demo replay is exact end to end for both karts (NOTES 112).  Residual: the other six characters unverified (S13), water/snow effects, pipe-crash spin, kart contact (none observed in the demo - NOTES 112) |
| P4 sprites | **done** — the projection is derived once from the ROM's own DSP-1 geometry (NOTES 083/084): depth(L)=4972/(L-20.36), scale=depth/256 (ratio = Les, the cross-check), camera trails the kart 61 px.  Pose ladder measured pixel-exact (NOTES 080/081).  Residual: kart-sheet rows 1-2 purpose, sprite size quantisation (ours is continuous, labelled) |
| P5 race furniture | **part** — ground objects stamped with the ROM's own tiles (NOTES 074), sprite-obstacle entity list decoded and colliding (NOTES 078), HUD set + clock + lap counter on the game's own art, start countdown (NOTES 085).  hazard classes decoded and ported - water ($22) wade/skim, the fall ($24/$26/$20/$28) and Lakitu's rescue as the ROM's own three states with a latched target (NOTES 113, 124).  Breakable blocks done and gated for both themes (NOTES 123/123a).  The sector map now matches the game's own $7F:5000 on all painted cells.  Residual: the horizon/backdrop (S5), entity motion handlers, item behaviour, Lakitu's art, the splash/sink effects |
| P6 opponents | **done to first order** — flow-field steering (95% byte-exact), ramp launches over jump gaps, wall escapes, and a Lakitu rescue: **20/20 strict laps** at 19-74 s (NOTES 057).  Residuals: ramp velocity placeholder, `$80ABxx` lane adjusters, rubber-banding, Lakitu animation |
| P7 audio | **decided** — pre-recorded; `smk spc` dumps the driver, rendering not wired up |
| P8 modes / menus | not started |

## Phases

Ordered so that each unlocks the next, and the scary unknowns are probed
early (see "Risks probed" lines — a risk we discover in phase 6 that
invalidates phase 3 work is the failure mode to avoid).

### P0 — Verification infrastructure (the oracle)  ✅ DONE
### P0.5 — Make the oracle *run the game*  ✅ DONE (it gated P2-P6)

Static decoding has a structural ceiling: 258 dispatches in the ROM jump
through a pointer already held in a register (NOTES 018). Behaviour has to
be observed, not read.

Already working: APU IPL handshake stub, RDNMI/HVBJOY, NMI dispatch, and
`run_frames()` at the game's own vblank pacing. The game boots and runs
~1200 frames in 0.1 s.

Done since: scanline counter, HTIME/VTIME compare, IRQ dispatch via
`$00:FFEE`, `$4211` TIMEUP, and an interrupt nesting guard. With IRQ the
game advances **mode 13 -> mode 0** (NOTES 019).

APU handshake solved without an SPC700 (NOTES 020) — the game uploads its
55 KB driver and progresses **mode 13 → 0 → 2 → 0 → 3**, reading the joypad.

**Race mode reached and physics observed (NOTES 022).** Needed `$4212` bit 6
(HBlank), mode changes through the pending-mode variable `$32`, and knowing
the kart block is based at `$B4` = `$1000`.

Remaining for fuller runtime fidelity:
  - menu navigation (not needed for observation; `$32` gets us into a race)
  - HDMA (`$420C`), which is how the Mode 7 matrix reaches the PPU

Acceptance: drive the attract mode with synthetic input and reach the race
mode; then read kart position/velocity/angle straight out of WRAM while
frames advance.
*Do this before any behaviour work.*

- Minimal 65816 interpreter over the ROM image + a flat RAM array: enough to
  run a leaf routine to its RTS/RTL. Reuse `smktool.opcodes`; skip
  interrupts, skip PPU. Add DSP-1 stubbing hooks (see risk R1).
- Harness: set up RAM/registers from a JSON description, run routine, dump
  the RAM/registers it touched.
- Acceptance: it reproduces the decompressor at `$84E09E` byte-for-byte
  against our C codec on all 69 assets. That validates the interpreter
  itself against something we already trust.

**Risks probed:** whether oracle-based verification is viable at all; DSP-1
call frequency (R1).

### P1 — The track, completely  (theme ✅, surface table ✅, objects next)
- **Surface-behaviour table**: which tile index is road / offroad / wall /
  boost / jump / pit. Approach: the physics reads it every frame — find who
  indexes RAM with `(y>>3)*128 + (x>>3)`-shaped math, or who reads the
  tilemap copy in WRAM. This table gates *everything*: collision, speed on
  grass, lap logic.
- **Per-track theme bindings** (S3): which tileset+palette per course. It is
  set during race-mode init; trace mode 6's setup path.
- **Track object lists**: item boxes, coins, pipes, oil, jumps — their
  positions must live in per-track data near the tilemap pointers.
- Acceptance: render all 24 tracks with correct themes; overlay the surface
  classes as colour; the overlay must visibly match roads/walls.

### P2 — Start line, checkpoints, lap logic  ✅ MOSTLY
- Real start positions and grid (kills S2).
- Lap counting is checkpoint-based (the game detects backwards driving), so
  there is per-track checkpoint data. Find it near the object lists.
- Acceptance: our lap counter agrees with the checkpoint data on a hand-driven
  path around each track.

### P3 — Kart physics (the core of "feel")   ✅ DONE for the player (NOTES 106-109)

**Per-character behaviour (note, 2026-08-25):** every kart differs - top
speed, acceleration, off-road caps, steering rows, drift row - and all five
are decoded and read per character (S13).  Two characters are verified by
replay; the rest need their own captured races.  Keep an eye out for
factors we have not seen yet (weight in kart collisions, item odds).
Done: the motion primitive, the RAM layout, the units, and the exact
integration (NOTES 016-017), ported to `src/kart.c`. Remaining: the
acceleration curve, steering/drift/hop, per-surface response and the
collision state machine at `$80F8C0`.

The largest decode. Sub-order:
1. Locate the per-frame kart update in race mode (mode 6 handlers; the kart
   state block in RAM — position, velocity, angle — is findable by watching
   which RAM the M7 matrix math consumes).
2. Decode: accel/brake curves, steering + drift/hop, surface speed modifiers
   (needs P1), wall response, jump/ramp physics.
3. Port to C in the same fixed-point. Verify each sub-routine against the
   P0 oracle over swept input states, not by feel.
   **Kinematics done and verified: `make verify-physics` shows 0 mismatches
   over hundreds of steps against the running game (NOTES 022).**
   **Speed integration done too (NOTES 023): speed and acceleration are
   32-bit pairs `$E8/$EA` and `$EC/$EE`; `smk_kart_accelerate()` mirrors it.**
   **Acceleration and steering decoded (NOTES 025): acceleration is a table
   lookup on current speed toward a target speed, deceleration a four-entry
   table; steering is a slew-limited follow of a target angle `$FA,x`.
   **ROM source found and ported (NOTES 026): `$81FED5` holds one pointer
   per engine class to a 64-byte table, widened `<<4` by `$81FEB6`.
   `src/physics.c` reads it at runtime; no numbers are baked in.**
4. Only after the oracle agrees: replace S1, derive the camera from the kart
   state the way the game computes its matrix (kills S4).
- Acceptance: oracle diff = 0 over the swept state space for each ported
  routine; then a human lap of Mario Circuit 1 that feels right.

### P4 — Sprites: karts and objects on the plane  ✅ DONE
Done: kart sprite frames located and read from the ROM at runtime, the
player's kart is drawn (NOTES 028), and the sheet's three size tiers are
identified (NOTES 030).

Since done: the frame-selection rule was measured pixel-exact by matching
the live P1 sprite's pixels against every sheet frame (NOTES 080/081) - the
straight pose is frame 0's LEFT HALF mirrored, steering is frame 1, drift
onset frame 47, deeper slides walk the rotation set.  The projection is the
ROM's own (NOTES 083/084).  Residual: what the sheet's rows 1-2 are for.

- Kart sprite sheets (many rotation frames), character palettes, the
  world→screen projection for sprites (scale by distance — the game has a
  table for it), sprite sorting against the ground plane.
- Acceptance: contact sheet of every character's rotation frames; a kart
  rendered on-track at the right scale for its distance.

### P5 — Race furniture  (part)
- Item boxes, coins, pipes/obstacles behaving; the real horizon/backdrop
  per track (kills S5); start-light sequence.

### P6 — Opponents
- AI drives per-track waypoint/racing-line data (it must exist — find it
  with the object lists in P1). Rubber-banding parameters. Items later;
  plain driving opponents first.

### P7 — Audio  — **DECIDED: pre-recorded, no SPC700 in the shipped game**

Design decision (the user's): the native game plays **pre-recorded digital
audio**, not emulated FM/BRR. That removes the SPC700 from the shipped port
entirely and makes the blocker in NOTES 019 cheap to solve — the 65816 only
needs its *handshake* answered, not a real sound CPU. Done in NOTES 020.

Where the audio comes from, which is the part that is not automatic: we
cannot ship Super Mario Kart's music. The pipeline is

    the user's ROM -> `smk spc` -> .spc -> any SPC player -> wav/ogg -> SDL_mixer

`smk spc` already writes a structurally valid dump (NOTES 021). Two things
remain: the driver's "play track N" command (we log the command stream but
have not mapped it), and rendering, which needs an SPC player — either
vendored, or left as a documented step the user runs once.

Fallback if that proves fiddly: original replacement music, which ships
cleanly and needs no ROM at all.
- Faithful = run the game's own SPC700 program on an emulated SPC700+S-DSP
  core, uploaded from the ROM exactly as the game does, and speak to it
  through the 4 APU ports with the same command protocol the 65816 side
  uses. This is the register-stream philosophy: don't re-synthesize, run the
  original driver.
- Decision to make when we get there: vendor an existing permissively-
  licensed SPC core vs. write one. Do not hand-convert music.
- Acceptance: A/B a recording of the title theme against an emulator.

### P9 — Quality of life  (AFTER the game is replicated correctly)

Deliberate, opt-in departures from the original - things the hardware
could not do and we can.  The rule for every entry here: **fidelity is
the default and ships first**; a QoL option is only allowed once the
faithful behaviour it replaces is decoded, implemented and verified, and
it must be switchable so the original can always be seen.

- **Smooth sprite scaling.**  The SNES cannot scale a sprite: it swaps
  between a few pre-drawn sizes, so karts and objects POP between steps
  as they approach (entities 16 -> 11 art px, karts 31 -> 28 -> 25 plus
  a half-size drawing).  That popping is faithful and is what we render
  now.  Smooth interpolation looks better at our resolution, and the
  requirement is that it **matches the quantised sizes at the tier
  distances** and only interpolates between them - so it is the same
  scaling curve, without the steps.  Not a re-scaling: a smoothing of
  the one we measured.
- Higher internal resolution than 256x224 (already true of the ground;
  the sprites are the remaining pixel-art constraint).
- Wide-screen framing, which needs a decision about what the extra
  horizontal field does to the AI's blind spots.

Everything in this phase is off by default until the faithful path is
green.

### P8 — Modes, menus, HUD, polish
- Time trial first (no AI dependency), then GP structure, points, ranks.
- HUD (the game renders it on BG1 over Mode 7), menus, 2P split-screen
  (two Mode 7 views — renderer already resolution-independent, cheap for us).

---

## Risks — what could bite us

**Status: R1 scoped (NOTES 008) but not closed (NOTES 015). R2 confirmed real
— the Mode 7 matrix is HDMA-driven, so there are no PPU stores to read
(NOTES 014).**

**R1 — The DSP-1 coprocessor sits inside the physics.  [CLOSED as a
blocker — NOTES 039]** The full command set is implemented and the stream
stays in sync through boot and racing with zero unknown commands. The
camera model (`$02`/`$06`) was corrected against the game's own parameter
traffic: F is the ground focal point, not the camera. Residual
approximations are labelled in the code and logged on first use.

History: first scoped to four commands from a static scan (undercounted),
then reopened when a race showed ~1500 unmodelled operations. Original
notes follow.
Confirmed used, and narrowed to four commands (NOTES 008): multiply, sin/cos,
2D rotate, vector length. The remaining risk is *scaling*: our
implementations are from documented behaviour, not measured (NOTES 015, S9).
Until that is settled, anything ported on top of a DSP-1 result is a guess
wearing a decoded routine's clothes. Settle it before P3, not during.

Original note follows.
The cart has a DSP-1 (cart type `$05`), used for Mode 7 maths — likely
raster→world projection and possibly kart position/rotation maths. If the
physics calls into it, "decode the physics" includes "decode which DSP-1
commands are used and reimplement those". Mitigation: probe **early** (P0
oracle work): find all reads/writes to `$6000/$7000` (DSP data/status), log
which commands race mode issues. DSP-1 commands are publicly documented
maths (multiply, inverse, rotate, project) — reimplementable — but we must
know *which* and *where* before P3 planning, not during.

**R2 — No reference emulator in the loop yet.  [BEING BUILT; SPC700 is the
critical path]** The oracle boots the game, runs scanline-accurate-enough
frames and dispatches NMI and IRQ. It stops because the game waits on the
sound CPU. An SPC700 interpreter is now the single highest-value piece of
work in the project: it unblocks observing everything else.
The whole-frame question the original note said to wait for has arrived
(NOTES 018). The oracle boots the game and runs frames; it needs IRQ and
HDMA to progress. Original note follows.
The oracle (P0) verifies routine-level fidelity, but whole-frame behaviour
(interrupt timing, HDMA effects) has no ground truth on this machine yet.
Mitigation: keep P0's scope honest (leaf routines), and when a whole-frame
question appears, build/install a debug-friendly emulator then — not
speculatively now.

**R3 — Fixed-point subtleties.**
65816 signed shifts, BCD/decimal-mode arithmetic (the game may use it for
score/time), 16-bit wraparound, and the M/X width dance. The C port must
match bit-for-bit; the oracle exists to catch exactly this. Never "clean up"
an odd-looking computation — oddness is usually load-bearing.

**R4 — RAM map archaeology.**
Physics decode is really RAM-map decode: the kart state block, the surface
table copy, the object array. Approach: name RAM addresses in
`romhack/symbols/` as they are identified, and grow one authoritative RAM
map file. Renaming late is cheap; two names for one address is chaos.

**R5 — Scope creep toward engine-building.**
The temptation is to write a nice entity system, then bend the decoded game
into it. Resist: mirror the game's own structure (its RAM block layout, its
update order) even where it is ugly. Order of update **matters** — ties in
state machines resolve by code order (last write wins), and that decides
observable behaviour.

**R6 — 2P/battle mode assumptions.**
Battle courses (tracks 20–23) and split-screen touch everything (two
cameras, different HUD, different physics tuning?). Defer consistently:
decode single-player first, but when choosing data structures, never assume
"there is exactly one kart/camera".

**R7 — Versions.**
Everything is pinned to the USA revision (sha1 `47e103d8…`). Addresses in
this project are wrong for PAL/JP/rev-1 ROMs. The loader already warns on an
unrecognised dump; keep every new address in `romhack/symbols/`, never
inline-undocumented, so a future second-version port is a table swap, not an
archaeology dig.

---

## How to work a phase (the loop)

1. Read this file's phase entry; open a numbered entry in `docs/NOTES.md`.
2. Decode with the toolkit (`smk lin`, `smk trace`, xref scans). Throwaway
   Python until the format/behaviour is *proven* (adjacency, oracle,
   round-trip — whatever fits).
3. Only then write the C. Same arithmetic. Placeholder comments for anything
   still invented, and a ledger row here.
4. Extend `smk_selftest` (C) and `tools/test.py` (Python) with the new facts.
   Both suites green before commit.
5. Update `docs/FINDINGS.md` (what is now known), the ledger here (what is
   no longer faked), commit, push.
