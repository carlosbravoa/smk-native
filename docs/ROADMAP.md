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
| S5 | `src/mode7.c` sky + `src/horizon.c` | **mostly**: backdrop colour and character-0 fill measured (NOTES 114); the far horizon plane is the ROM's own `gfx_d` tiles arranged by the `gfx_e` map, byte-matched against the game (NOTES 117).  Labelled: the scroll law and which map rows show.  Missing: the NEAR parallax plane (ghosts, arches - and note these are BACKGROUND, not track objects, NOTES 127) and the sky gradient | two scenery planes at different scroll speeds over a per-theme gradient | P5 - parked at the user's request |
| S6 | `src/kart.c` bounce | **RESOLVED for the impact and its cost.** Measured frame by frame in the running game and then against a human crash run (NOTES 125/130/132/133): the impact mirrors the blocked component and leaves the speed alone; the next frame damps each axis by the pair `$56` selects and re-derives `$EA` from the vector; with BOTH components under `$C0` there is no damping at all - `$80F9C1` forces each to +-`$100`, the constant push-back; the window holds the SPEED as well as the velocity; and then drive state `$16` decelerates from the table at `$80A590` indexed by the velocity lag.  A wedged kart is ejected after eight frames (`$80F964`, NOTES 136).  **Open**: `$80A0C7`'s realignment is decoded but NOT ported - porting it naively broke the dynamics outright (NOTES 131) and it needs the slide machine's `$A6`/`$AC` states first; the graze exemption at `$80A0EB` is in the ROM and in the recording but makes the port WORSE (82.0% -> 73.4%), so something upstream still differs; the `$500` fast-hit path is unmeasured | same | impact and cost closed; realignment and graze open |
| S7 | renderer | full-resolution smooth perspective | 256×224, per-scanline integer matrix | keep — named divergence, this is the point of a PC port. `--pixel` restores chunk. |
| S8 | no audio | silence | SPC700 + S-DSP running its own program | P7 |
| S10 | `src/main.c` draw | **entities RESOLVED for law and size.** The scale is the DSP-1 projection's own third output, `$4200 / depth ALONG THE VIEW AXIS ahead of the kart` (2.1% over 975 samples, against 19% for the euclidean distance the port used), and the drawing is chosen by walking `$84DA3C` = C0 60 30 00 (NOTES 129).  The drawn SIZE is twice the sheet's drawing, measured against a real frame with the kart as the ruler - 23x31 SNES px where the sheet holds 12x16 (NOTES 139).  Labelled: (a) where the larger art comes from is unknown - the whole object sheet tops out at 16x16, so the port magnifies; (b) that nothing draws past the last threshold is a reading of `$84DA38`, not a measurement.  **KARTS still open**: same `+$06`, but their drawing ladder is a different table and has not been measured | same | entities closed; karts P5 |
| S11 | `src/main.c` start sequence | 3-2-1 countdown at 60 frames a step, karts held | the ROM's own start-frame count and Lakitu's light art | P5 |
| S12 | `src/main.c` entities | **spawn done, MOTION mapped and not yet ported.**  The spawn is the game's own (NOTES 127): two slots in a one-player race, respawned from the lap segment the player's waypoint falls in.  The movers are now understood too (NOTES 146) - Thwomps and moles move **only in Z**, on a per-object BYTECODE SCRIPT, and the port has neither the interpreter nor the scripts | `$85E0B9`: `ldy $04,x / tyx / jsr ($0000,x)` - a record's first word is its handler, which reads args from `$0002,y` and advances `+$04` past itself.  `$85DDA0` is the height command (`$0002,y -> $1F,x`, +6).  Command table around `$85DD26`.  Same shape as the tyre-smoke interpreter in `src/effects.c` | P5 - **next**, one focused session |
| S14 | `src/course.c` direction field | the AI/rescue direction field is our atan2 of the waypoint delta, rounded.  MEASURED against the game's own `$7F:4000` (track 7): **2554 of 2684 cells exact, 130 off by one step of 1/256 turn, worst error 1** | `$81FCFC` builds it through the boot-time arctangent table at `$7F:9000` (`$81E4C5` generates it, `$81F638` reads it as octant base + `table[min*64+max]`) | labelled at that number; port the table if a divergence is ever traced to it |
| S13 | `src/player.c` per character | **decoded and read from the ROM** for the five tables the game has: base top speed (`$81:8000`), acceleration curve (`$81:8010`), off-road caps (`$81:8060`), steering rows (`$81:8088`) and the drift-row adjust (`$80A4C0`: Yoshi/Koopa slide one row lower). Only Mario (P1) and Toad (P2) are VERIFIED against the game so far - the other six characters run on the same code with their own tables but have not been replayed | every character handles differently; there may be further per-character factors (kart-to-kart weight, item odds) not yet found | P3 residual: replay each character (needs a log per character - a real race, not the attract demo) |
| S15 | `src/main.c` `draw_entity` | the near object's ART: the port magnifies the sheet's 16x16 drawing 2x to reach the measured 23x31 | the game gets that size from somewhere - the object sheet is 57 tiles and nothing in it exceeds 16x16.  Either a second art source, or a composer (the mirror of the kart minifier, NOTES 076).  The crop of the original shows the shape it must keep: a wide lid overhanging a narrower body | P5 |
| S16 | `src/player.c` fall | while falling, our kart descends in z so something is seen to move | `$1F` stays at **1** for all 60 countdown frames - the physics stops and waits, and the visible drop is the SPRITE (NOTES 135a).  Matters more now that sprites below the plane are clipped: ours sinks behind the track, the game's does not | P5 |
| S17 | `src/player.c` start | no start boost at all: the countdown holds the kart, the lights go out, you drive | during the countdown the throttle DOES something - the kart is held but revs, you launch at "higher rev, normal speed", and pressing accelerate at one exact point gives a **turbo launch** (user, who plays it).  So there is a rev accumulator separate from `$EA` and a window that tests it - and it OVERSHOOTS: hold from the start and the kart spins on itself until the revs decay to zero.  Nothing of this is decoded | P5 - needs the `starts` recording |
| S18 | `src/main.c` start | no Lakitu and no traffic light; the port shows 3-2-1 digits | Lakitu descends with a semaphore, and it (with the sound) is how a player times the launch.  TIMING is already right - the countdown is the measured 336 frames - so this is art plus animation.  Ruled out (NOTES 145a): not in `gfx_b`/`gfx_f`, no discrete light state in low WRAM, and MAME exposes no VRAM/OAM share.  Route: render the OBJ half of the Python oracle's VRAM after reaching a race and match back to a ROM asset; `$0142` is the likely animation driver | P5 - parked for a session of its own |
| S9 | `tools/smktool/dsp1.py` | full command set implemented; stream never desyncs; camera model verified against the game's own usage. Residual: gyrate is a passthrough, and raster/`$08`/`$18` scalings are unchecked | the real chip's exact fixed-point pipeline | largely closed (NOTES 039); residuals logged on first contact |
| S19 | `src/main.c` time trial | the one mushroom is granted at the start and, once used, is gone for the run | the ROM's own time-trial grant is not decoded.  Located, not read: the item state lives in `$0D70,x`/`$0D78,x` and the roulette entry that arms it is `$81B34E` (`$0D70 = $A000`, `$0D78 = $E1`); no `$2C == 4` path to it was found in bank `$81`.  So "one per run" is the user's rule, not a measurement - it may refill per lap | P5, with items |
| S20 | `src/menu.c` | the shell's LAYOUT is ours: title, mode, driver, course and results screens composed from the ROM's font and palettes | the real screens are BG tilemaps with Lakitu, a course map preview and an animated cursor.  The tiles are in the `$C7:1996` stream we already decompress (NOTES 147); what is missing is the tilemap that arranges them and the BG/scroll setup.  The TEXT is not invented - font, palettes, cup order and course names are all ROM-derived (NOTES 147/148) | P8 |
| S21 | `src/menu.c` `smk_tt_crossing` | lap 1 is timed from the LIGHTS, so it carries the run up to the line | the ROM's own clock start is not decoded.  The structure around it is: the grid is behind the line, so five laps are six crossings (`$014C = $8500`, NOTES 148), and `tools/laptest.c` confirms that on 20/20 courses.  Timing lap 1 from the first crossing instead would make it two seconds of rolling start rather than a lap, so this reading is the sane one - but it is a reading | P8 |

*Resolved:* **S9 for command `$04` (sin/cos)** — pinned by unit analysis in
NOTES 017; movement no longer rests on a guess. The kinematics (velocity
construction and position integration) are now the ROM's own, in
`src/kart.c`.

*Partly resolved:* **S2** (start positions) — the grid is real and confirmed
against the demo race on track 7; 5 of 24 courses still need their own.

*Resolved:* **S3** (per-course theme) — the ROM's own table `$81EC2F` is now
used; C output is byte-identical to the game's loader on all 24 courses.

---

## Wrong turns worth not repeating

* **Ghost Valley has no moving objects.**  Its `$0D28` selects the path
  repositioner at `$84DC80`, which makes it look like the mover track in
  every static reading - but the four slots there only shift when the
  WAYPOINT advances, and they carry no graphic (`+$08` = 0).  The tracks
  that really have movers - Bowser Castle, Rainbow Road, Donut Plains -
  select the STATIC spawner.  Derived wrongly three times; see NOTES
  145b/146.
* **Memory taps cannot see direct-page writes to WRAM.**  A Lua tap on
  `$00:1018` catches six writes in 2171 frames while the game writes it
  every frame.  Use the debugger's watchpoints, or the Python oracle
  (NOTES 142b).
* **A decoded routine is not a portable routine.**  `$80A0C7` is correct
  65816 that wrecked the dynamics when dropped into a state machine we
  had only half ported (NOTES 131).
* **The route points are not a drivable line.**  They are sector
  CENTROIDS: on Mario Circuit 2 the straight line from sector 29's point
  to sector 30's crosses a solid barrier.  Steer on course by the
  direction field - which is built from those same waypoints, in the form
  that knows where the road is - and keep atan2-to-a-waypoint for
  off-course recovery, which is what the ROM does.  `src/ai.c` already
  said so; NOTES 149 walked into it again anyway.
* **A guard invented to be safe can make part of the game unreachable.**
  `p->drive != 0x10` on the object-class dispatch made Mario Circuit 2's
  jump impossible - the boost pad twelve pixels before the ramp sets that
  very state - and every gate stayed green for weeks, because no recorded
  run crosses a ramp while boosting (NOTES 149).

## The gate, and how work gets proved now

The strongest instrument this project has is not a rig - it is the user
playing the real game while MAME records, and the port then replaying
their inputs frame by frame.  Five replay gates run in `make check`,
alongside the asset selftest, the AI lap test and `smk_laptest` (the race
length and the shell's own bookkeeping):

| run | what it covers | today |
|---|---|---|
| `demo_race.csv` 1000 / 1100 | the attract race, both karts | 100.0% within 1 px, 0 resyncs |
| `demo_tt_track19.csv` | a time trial | 100.0% within 1 px, 0 resyncs |
| `crash_run.csv` | **a human** driving Mario Circuit into barriers, 15 wall contacts | 82.0%, 240 resyncs |
| `gv1_run.csv` | **a human** on Ghost Valley: 8 block hits, a fall, Lakitu's rescue, a lap of sliding into rails | 93.0%, 56 resyncs |

The staged demos are exact and stay that way.  A human run never will be -
it has AI karts we do not simulate - so each carries its own floor
(`--min`, `--resync`), set just under what it achieves.  That is enough:
the version that broke bouncing scored 63%.

**A second instrument, added later: a driver that obeys the rules.**
`--autodrive` (`src/autopilot.c`) plays the game through the pad - it
presses buttons and nothing else, so every rule the player is subject to
applies to it.  That is the difference from the AI, which writes its own
heading and speed and ignores surfaces, and it is why the autopilot found
in one lap a jump the player could not take on a Mushroom Cup track while
five replay gates stayed green (NOTES 149).  It gets round most GP
courses, not all; where it fails, it fails honestly.

**Why this matters more than it looks.**  Twice this session a change
passed every gate and every selftest and was still unplayable, because
the attract demo never touches a wall.  Both times the user found it in
minutes.  The lesson is in NOTES 131 and worth repeating here: *a decoded
routine is not a portable routine.*  `$80A0C7` is correct 65816 that
reads cleanly and writes into a state machine we have only half ported.
When the only available proof is someone driving it, port the smallest
measured piece and leave the rest decoded in the log.

**To record another** (the loop is `tools/labs/mame/`):

    tools/labs/mame/play.sh <name>          # play; Esc ends it
    DEMOLOG=x.csv tools/labs/mame/replay.sh <name> tools/labs/mame/demolog.lua 200
    ./build-native/smk_demoreplay rom/smk_usa.sfc x.csv    # diff, frame by frame

Time Trial and a verified character (Mario, Toad) keep the run clean.
Ask for one whenever a decode depends on "the game doing X" - it has been
faster than every rig it replaced, every single time.

## Status at a glance

| phase | state |
|---|---|
| P0 oracle | **done** — 65816 interpreter, verified against the game's own decompressor |
| P0.5 running machine | **mostly** — boots, uploads sound, runs races; no PPU picture, no SPC700, no HDMA |
| P1 the track | **done** — themes, tilemaps, tilesets, palettes, surface table, all verified against VRAM |
| P2 start / laps | **mostly** — real grid, decoded lap rule (NOTES 052) with the monotonic guard, race clock and start countdown.  The race LENGTH is now measured (`$014C = $8500`: five laps are six crossings, NOTES 148) and gated on 20/20 courses by `tools/laptest.c`; the finish and results flow exist for time trial.  Residual: GP points and standings |
| P3 physics | **done for the player, and now gated by human runs** — the control is transcribed from the ROM and replays the attract race's human inputs frame-exact: 99.8% / 100% of frames within 1 px (NOTES 106-108), with tyre smoke and dust from the game's own effect object (NOTES 109).  The demo replay is exact end to end for both karts (NOTES 112).  Residual: the other six characters unverified (S13), water/snow effects, pipe-crash spin, kart contact (none observed in the demo - NOTES 112) |
| P4 sprites | **done** — the projection is derived once from the ROM's own DSP-1 geometry (NOTES 083/084): depth(L)=4972/(L-20.36), scale=depth/256 (ratio = Les, the cross-check), camera trails the kart 61 px.  Pose ladder measured pixel-exact (NOTES 080/081).  Residual: kart-sheet rows 1-2 purpose, sprite size quantisation (ours is continuous, labelled) |
| P5 race furniture | **part** - the live phase — ground objects stamped with the ROM's own tiles (NOTES 074), sprite-obstacle entity list decoded and colliding (NOTES 078), HUD set + clock + lap counter on the game's own art, start countdown (NOTES 085).  hazard classes decoded and ported - water ($22) wade/skim, the fall ($24/$26/$20/$28) and Lakitu's rescue as the ROM's own three states with a latched target (NOTES 113, 124).  Breakable blocks done and gated for both themes (NOTES 123/123a).  The sector map now matches the game's own $7F:5000 on all painted cells.  Residual: the horizon/backdrop (S5), entity motion handlers, item behaviour, Lakitu's art, the splash/sink effects |
| P6 opponents | **done to first order** — flow-field steering (95% byte-exact), ramp launches over jump gaps, wall escapes, and a Lakitu rescue: **20/20 strict laps** at 19-74 s (NOTES 057).  Residuals: ramp velocity placeholder, `$80ABxx` lane adjusters, rubber-banding, Lakitu animation |
| P7 audio | **decided** — pre-recorded; `smk spc` dumps the driver, rendering not wired up |
| P8 modes / menus | **part** — a working shell: title → mode → driver+class → course-by-cup → 5-lap time trial → results, with the top five lap times per course kept on disk.  Font, palettes, cup order, course names, lap count and the time-trial rules are all ROM-derived (NOTES 147/148).  Residual: Grand Prix (and with it points and standings), the real menu art (S20), the mushroom grant rule (S19) |

## Where to pick up next

Physics is in good shape and gated by human runs; the divergences that
remain in them are drift, not wrong rules.  In rough order of value:

1. **Grand Prix (P8).**  The shell exists and time trial runs end to end,
   so this is now the visible gap: four cups of five races, the AI field
   we already have, finishing order, points and standings.  The lap and
   finish rule is measured and shared (`$014C`, NOTES 148); what is new is
   scoring and the between-race flow.  Not a decode problem so much as a
   game-state one.
2. **Moving obstacles (S12's other half) - MAPPED, and now BLOCKING.**
   No longer cosmetic: Thwomps spawn at the right positions and never
   move, so a row of four permanently-down Thwomps is a wall across the
   road.  It makes **Bowser Castle 2 and 3 unfinishable** - the autopilot
   is pinned at (448,401) on track 9 against entities at (428..452, 396),
   and at (283-332, 688-702) on track 3 against entities at
   (308..332, 708).  A human cannot get past them either (NOTES 149).
   Everything below was already true:
   Thwomps and moles move only in Z, on a per-object bytecode script
   (NOTES 146).  The work is: the interpreter (`$85E0B9`), the handful of
   commands a Thwomp and a mole use (`$85DDA0` is the height one, table
   at `$85DD26`), and where a script is attached at spawn.  `src/effects.c`
   already has the same shape to copy from.  **Gate it on a Bowser Castle
   recording** - the demo never sees a Thwomp, so nothing existing would
   catch a regression.  A cheaper gate now exists too: `--autodrive` gets
   round 18/20 GP courses, and the two it cannot are exactly these.
3. **Items (P5).**  The largest gameplay gap and the one you notice in ten
   seconds of a real race.  We have the mushroom as a special case; the
   roulette, the item set and the award-by-rank rule are all undecoded.
   Big, but it is what turns a faithful driving model into the game.
4. **Per-character verification (S13).**  Nearly free now - both human
   runs used character 1, so gating one more character is mostly
   bookkeeping.  Six of eight remain unverified.
5. **The near-object art (S15).**  We reproduce the SIZE by magnifying;
   finding the real source would close it properly.  Fingerprint: a wide
   lid overhanging a narrower body, ~24x32.
6. **Kart size ladder (S10's other half).**  Same `+$06` law, unmeasured
   drawing ladder.  Visible.
7. **The start (S2, S17) - one decode, three ledger rows.**  The
   grid origin is out by up to 152 px.  S11 (the countdown) and S17 (the
   rocket start) are CLOSED; S18 (Lakitu and his light) is parked.  All three live in the same few
   frames of the same routine, so they are worth doing together rather
   than one at a time.  S16 (the falling kart's z) is closed.
6. **The graze exemption (S6).**  Known-wrong detail: the ROM exempts a
   slip under 45 degrees from the crash deceleration, the recording shows
   the game doing it, and applying it makes the port worse.  Something
   upstream differs; finding it would likely lift both human gates.

Deliberately parked: the background's near plane and sky gradient (S5) -
the user has said it is less relevant than feel.

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
