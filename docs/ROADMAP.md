# Roadmap: a faithful native Super Mario Kart

Goal: the game running on PC, SDL2, no emulator, **as faithful as possible**
to the SNES original. "Faithful" means: behaviour derived from the ROM's own
code and data, not from how it looks in videos or how another remake did it.

This file is the single place where we are honest about the gap between the
two. Every shortcut lives in the ledger below; a shortcut not written down
here is a bug in this file.

**If you have ten minutes and a controller, read "What needs playing"
first.** Four mechanics are decoded, ported and gated by the headless
tools but have never been judged by a person, and the port cannot be
driven end to end from an agent shell - it blocks on SDL. Those four are
where the next real bug is.

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

**S44 — The VS CPU driver is a trained neural network, and it is OURS.**
`src/netpolicy.inc` (int8 weights, a float scale per row, 84 KB) is a
two-layer MLP - 81 inputs, two 256-unit layers, 14 actions - fitted by
PPO in the environment below, across the three classes and three
situations (alone, the field, the field with items) on 16 GP courses
with four held out.  It drives through `smk_player_step` by pressing
buttons, one decision every four frames, with no privileged control; the
dashboard names it `NEURAL`.  Its observation carries no absolute
position, course identity or clock, so it cannot store a route, and no
ROM bytes pass through it.  Off the 20 GP courses (the arenas have no
racing line: pointed at one it sat still, measured) and in a build with
no weights, `src/autopilot.c` drives and the dashboard says `AUTO`.
`--cpu-policy FILE` substitutes another network.  What it is NOT: the
ROM's own AI - that is the seven field karts, `$80ADA0` and all - and a
claim about how Super Mario Kart plays.  It is a better racer than the
scripted driver and the honest gap to a person is in docs/RL.md.

**S43 — The RL environment's observation, actions, reward and episode
rules are OURS.** `src/env.c` steps only the game's own code - the
physics, the surfaces, the sector map and racing line, the `$7F:4000`
direction field, the lap rule, the grid, the rescue and the 336-frame
countdown are all the ROM's, and `make envcheck` proves it by replaying
an environment race through the SDL binary and comparing the kart's
position and speed on every frame (currently 8,966 of 8,966). What the
game has no equivalent of - a 55-number observation, 13 actions, a
reward, and where an episode starts and stops - is invented, and makes
no claim about how Super Mario Kart works. Its own sub-ledger (the
skipped turbo start, 3 laps instead of 5, no opponents yet, items as one
binary action) is in [`docs/RL.md`](RL.md).

**S29 — The spilled coin's ARC is ours; its art and its spin are the game's.**
The three spin frames (VRAM tiles `$86`, `$A2`, `$60`, palette 6, ~4 frames
each) and their source (the shared blob at `$C1:0000`, a tile's bytes at
`(tile - 48) * 32`) are measured - dumped from VRAM at a real coin loss.
The LAUNCH is ours: `SMK_COIN_RISE`, the fan and the bounce. What IS taken
from the recording: the coin is thrown FORWARD (it appears high on screen
and comes down toward the camera) and it inherits the kart's motion -
thrown backwards it lands behind a forward-looking camera and is never
drawn at all.

**S41 — The overtake voices (NOTES 235).** MEASURED, not labelled: the
port reads the ROM's own two tables ($84:D99B and $84:D9CA) rather than
copying them, and the selftest checks all sixteen entries against it.
The rule is the ROM's too - $84:EF05's rank compare, and its $0A-frame
cooldown. OURS: nothing.

**S42 — Two players, side by side (NOTES 255).** OURS, and deliberately:
the original stacks its two views because it has 224 lines to divide, and
the user decided ours divides the other way - *"split screen but side by
side (left/right). Today we have widescreens!"*  With it comes a
projection reference width, so a narrow view shows a narrower slice of
the world instead of a squashed one; wider views are untouched and every
existing window renders as it did.  The engine sound is mono and follows
player 1 - also ours, and also labelled.  What is NOT ours: the second
kart is the ROM's own rival slot, and every rule that applies between two
karts applies between the two people unchanged.

**S40 — Four engines, one per driver pair (NOTES 234). CONFIRMED by
the user, 2026-09-01: "engine sounds are correct".** The sample and
the pitch law are both the game's, measured on the chip at eight rev
values per driver ($02/34, $03/38, $18/29, $17/19). What stays OURS is
what S39 already labelled - the placement of the AI voices - plus the
volumes.

**S39 — An engine per kart (NOTES 229).** The game gives a nearby kart
its own engine and the port now does too, but everything about the
PLACEMENT is ours: which three karts (the nearest), how far they carry
(220 world px), how they fade, how they pan, and the balance against the
player's own. The pitch law and the sample are the game's.

**S38 — The engine note (NOTES 212/213; superseded in part by S40).**
Now the game's own sample (Mario's SRCN $02 loop, decoded from BRR - and
since NOTES 234 one of four, chosen by driver) played at the game's own rate: the
DSP pitch register is exactly $4700 + 34*v, measured at ten values, and
the REV that drives v is the ROM's $80:9543. OURS: the volume, and the
PARAMETER - $42 is written from $C2 inside the sound update and the
physics reuses $C2 later in the frame, so the transcription of $80:9543
could not be validated and ran a third too high (the user: "in game is
slower"); the port now walks the parameter toward speed * 0.07 at most
3 a frame up and 1 down, fitted to 10,172 logged frames of the game's
own $42 (NOTES 214). Two earlier versions were wrong in a way only an
ear caught: a synthesised tone an octave and a half sharp (it fitted the
sample's ninth partial), then this parameter.

**S37 — The sound effects, rendered from the chip (NOTES 213/215).**
Two more things are ours here: where each effect ENDS (a voice is
followed until the baseline plays the same thing for eight frames, which
is a rule, not a measurement), and the fact that a sound made of several
ids cannot be rebuilt at all by poking - the driver serialises them onto
one voice - so the port fires the parts it knows separately. The
samples are the game's own BRR, decoded from its sound RAM, and the
notes are the DSP's own logged registers, so there is no music in them
at all - the first route (recording and subtracting, NOTES 211) left the
song smeared underneath every effect and is superseded. OURS: the
envelope is read once a FRAME and interpolated between, where the chip
runs it continuously; the effect is judged over when its envelope
reaches zero or the music takes voice 3 back; and every sample comes
from ONE snapshot, taken mid-race, so a state with a different sample
bank (the menus, most likely) may render the wrong sample. Which event
fires which id is the ROM's for the dozen with immediate call sites and
OURS-by-placement for the rest until a person names them.

**S36 — Round 2's approximations (NOTES 207/208).** The wading kart
rides 9 px low (the game's submerged drawing is unread); the Rainbow
Road Thwomp's palette-pair flash alternates every 8 frames (the pair is
measured, the period is not); Lakitu's post-drop exit - the two-coin
fee, the 64-frame rise, the kart held till he is gone - follows the
user's account of the original, with the pacing ours; a squashed-while-
small kart reuses the bug-13 flatten wholesale.

**S35 — The Thwomp squash (bug 13, NOTES 204).** The art is the user's
ripped flattened-racer sheet through each driver's OWN palette (fit
near-zero error; the Yoshi/Koopa block assignment is by eye where two
blocks share a palette family).  OURS: the rule (under ANY descending
block = squashed - the drop takes ~15 frames, faster than a kart leaves
the footprint), the 100-frame flatten with speed 0, and the straight
pose always; the game's own squash length and pose choice are unread.

**S34 — The known-bugs sweep (NOTES 203).** The feather's launch strength
(`$01E0`) is OURS (the flight's pose roll, +$0800 a frame, is $80B6D1's
rate); the Rainbow Road Thwomps' palette flash (every 4 frames, palette
bit 4) is OURS from the user's "flashy color"; the deep-water crawl on
themes 2/4/5 reuses the $22 fall-in measured in NOTES 120, by the user's
rule, not a trace of those courses; the finish-time extrapolation for
karts still out after 90 s is OURS (ordered by lap, +5 s a place); the
rescue Lakitu's descent pacing is OURS around the measured sprite rows.

**S33 — The track map (NOTES 200).** OURS by design: the corner, the size,
the translucency and the dots; the picture is the course's own tilemap.
M toggles it.

**S32 — Surfaces, the picture (NOTES 197).** The shake per class is MEASURED
for the nineteen classes swept on Mario Circuit; classes outside the sweep
take the road's bob, and the AI karts shake by the same table (OURS: only
P1's sprite was logged). The spray / splash / dust kinds and their art are
the ROM's; the theme-specific puff tiles ($100-$11F) are Mario Circuit's on
every theme until the other themes' streams are found; the water classes'
drawings ($20-$24) are not ported; every surface's SOUND is P7.

**S31 — What the item system still guesses (docs/ITEMS.md).**
The state machine, the outcome tables, the nine effects, the hit reactions
and the icons are the ROM's; the projectile motion is MEASURED. OURS: the
five random bits of the roll (`$1F26` is not reproduced); the thrown
banana's flight (UP never registered through the oracle's pad); the speed
of a shell thrown backward; a green shell's 8-bounce life; the 8 px contact
box; the shrunk kart's half size; the shell's spin rate (its two 16x16
frames are the game's, NOTES 189; the star's palette sequence is now
MEASURED); a dropped green shell being static and what it does when hit;
a starred kart knocking obstacles out (the real pipe's flight is not
measured); and the road items' spin rate. The road items' ART is the ripped
sheet's ladders through the ROM's own palettes (NOTES 192: the game
computes them, the ROM holds no tiles; the red shell's small tiers are a
remap of the green's, labelled). The AI's own weapons are IN
(NOTES 190, from the user's `attack` recording): the same two projectile
slots as the human's, carried behind the kart 58 frames then let go —
MEASURED; OURS: the 160 px trigger distance and the 640-frame cooldown
(bracketed by five events), the fireball's flight, the road art of the
mushroom / egg / fireball (roulette icons in borrowed palettes), the AI
star's length. The coinless
bump's spin (NOTES 187) is decoded and measured; OURS: an AI kart's coin
count only falls (its pickups are not modelled), so the AI spins on its
third bump where the real one may have refilled.

**S30 — Lakitu's chequered flag is drawn, but not pixel-right.**
The DATA is measured and correct: his path (230 frames, captured), the
group's offsets from his head, the three wave poses and their ~17-frame
cadence, and the art's true home - the shared blob at `(tile - 48) * 32`,
NOT the HUD set, whose indices are not VRAM tile numbers (asking it for
`$6C` returns the FINAL LAP plate). The DRAW still does not match the
game's composite: ours reads as a flag detached from his hand where the
game's sits against it. Two causes were found and fixed - wrong art
source, then wrong paint order (his head belongs OVER the flag's upper
half) - and a third remains. The user spotted it from a picture, unable
to test: *"the flag on the opposite side so the Sprite looks broken."*

**S28 — CLOSED (NOTES 199): the winner's pose is frame 46's mirrored half
with five sheet tiles swapped for the raised arms, measured from a real
finish in the oracle, and the arms-up alternation (from +101, 16 frames
up / 16 down) measured per frame. Nothing labelled.**

**S27 — The finish sequence is OURS by design, not by default.**
The celebration camera, its timing (50 frames to swing, 210 to hold), the
38-unit framing distance and the whole results layout are designed rather
than measured. That is deliberate and the user set the rule: *"faithful is
for driving experience, not for hud, menus, and things that can be better
without constraints."* The ART and the data stay the ROM's - its font, its
palettes, its character sprites and names, its own time formatting - and
the times shown are times karts actually drove, because the field keeps
being simulated after the player crosses (`settle_field`). What is NOT
ours: nothing in this touches how a kart moves.


**S26 — The height at which a raised mover stops touching you.**
`SMK_MOVER_CLEAR = 1280` is OURS. The game's own rule is not decoded: the
routine is reached only through an index register, so it has no caller and
no absolute address to search for, and SEVEN measurement rigs each ended up
measuring something else (NOTES 176). The user made the call - *"it is one
kart sprite in altitude, you can pass. I would even say 80% of it. This is
one of the things we don't need to do super accurate and we can implement
our own rule, put it in the ledger and move on"* - and 80% of a 16 px kart
is ~13 px, which is 1280 in mover units (also exactly 20 frames of the
measured +64 climb). Their recorded Bowser Castle run agrees with it on
every sample (crashes at 0-960, close passes at 2880-4096) and is the
selftest gate, but it has no samples between 960 and 2880, so it constrains
the number rather than fixing it. To close this properly: log the game's
own collision against a mover height with a rig that has a positive control.


| # | where | what we do | what the game does | phase |
|---|---|---|---|---|
| S1 | `src/player.c` | **RESOLVED** — the player's control is the ROM's own, transcribed and verified frame-exact against the demo race (NOTES 106-108): per-character top speed, acceleration table, surface caps, steering rows and drift row, the slide machine, spin-out, hop, coins on the target, the mushroom boost. Residual, labelled: coins are not collected yet (P5), the sprite's steering lean is synthesised, the DSP-1 sine is +-1 on 3% of frames, snow/splash effects | same | closed |
| S2 | `src/course.c` `smk_course_start` | **RESOLVED** — the grid is the game's own per-track record, ported from `$81:903C` (NOTES 161): `$81:8A79 + track*2` -> setup entry -> placement record (`x0`, `y0`, `x step`), eight slots 24 px apart alternating between two columns, heading 0.  A kart alone on the track gets `$818F7F`'s nudge, `(step/4, -16)`, which is the time-trial start.  Verified against the game on every course by `tools/labs/gridtable.py` + `gridcheck.py`, and against both time-trial recordings.  The CHARACTERS per slot are the ROM's (`$81EE97`, ported) | same | closed |
| S4 | `src/mode7.c` camera | **RESOLVED** — the projection is derived from the ROM's own DSP-1 geometry: `depth(L)=4972/(L-20.36)`, `scale=depth/256` (the ratio is exactly `Les`=256, the cross-check), camera trails the kart 61 world px (NOTES 083/084) | the DSP-1 builds per-heading scanline tables at boot; HDMA feeds them to M7A-D | closed |
| S5 | `src/mode7.c` sky + `src/horizon.c` | **mostly**: backdrop colour and character-0 fill measured (NOTES 114); the far horizon plane is the ROM's own `gfx_d` tiles arranged by the `gfx_e` map, byte-matched against the game (NOTES 117).  Labelled: the scroll law and which map rows show.  Missing: the NEAR parallax plane (ghosts, arches - and note these are BACKGROUND, not track objects, NOTES 127) and the sky gradient | two scenery planes at different scroll speeds over a per-theme gradient | P5 - parked at the user's request |
| S6 | `src/kart.c` bounce | **RESOLVED for the impact and its cost.** Measured frame by frame in the running game and then against a human crash run (NOTES 125/130/132/133): the impact mirrors the blocked component and leaves the speed alone; the next frame damps each axis by the pair `$56` selects and re-derives `$EA` from the vector; with BOTH components under `$C0` there is no damping at all - `$80F9C1` forces each to +-`$100`, the constant push-back; the window holds the SPEED as well as the velocity; and then drive state `$16` decelerates from the table at `$80A590` indexed by the velocity lag.  A wedged kart is ejected after eight frames (`$80F964`, NOTES 136).  **Open**: `$80A0C7`'s realignment is decoded but NOT ported - porting it naively broke the dynamics outright (NOTES 131) and it needs the slide machine's `$A6`/`$AC` states first; the graze exemption at `$80A0EB` is in the ROM and in the recording but makes the port WORSE (82.0% -> 73.4%), so something upstream still differs; the `$500` fast-hit path is unmeasured | same | impact and cost closed; the realignment's SLIP is now ported and gate-proven (NOTES 150: `vel_angle` takes the impact slip while a crash runs - the human crash run 81.5% -> 86.2%, floor ratcheted to 85); the graze exemption and the `$500` fast-hit path are still open |
| S7 | renderer | full-resolution smooth perspective | 256×224, per-scanline integer matrix | keep — named divergence, this is the point of a PC port. `--pixel` restores chunk. |
| S22 | `src/ai.c` `smk_collide_objects` | the object hit is the ROM's measured response (reflect, 308/581, NOTES 072) plus the impact SLIP so it survives the window, plus a `$80` low-speed floor so a slow hit is not glued.  The GEOMETRY is ours: a 6 px circle with a positional push-out | the ROM's own object collision shape is not decoded, and NOTES 072's measured response has no floor at all - with none the kart is glued to what it hit, so something in that measurement is missing rather than in the fit.  The `$80` value is fitted to behaviour the user confirmed, not read (NOTES 150/150a) | P5 |
| S23 | `src/course.c` / `src/main.c` | **named divergence, on purpose.**  Every object on the course is drawn AND collided, at any distance.  The ROM keeps two live blocks in a one-player race (`$819136`) and respawns them as the lap segment changes, and culls past `$84DA3C`'s last threshold (zf 352) - so pipes wink in and out as you drive.  Both are an OAM and a 256x224 budget, not a statement about the track: at our resolution the third band is still several pixels.  `--rom-spawn` restores the live pair and the cull.  Same standing as S7 | two live objects, culled at 352 | keep |
| S24 | `src/kart.c` `smk_kart_bump` | kart-to-kart contact is the ROM's: the `[-4,+3]` box, the `$5E` pair cooldown, the `$81:9277` weight order and all four branches of `$819B06`, each measured in the oracle (NOTES 166).  **Labelled**: `$819CC9`'s separation ships as the READING, not the measurement - its own indexing does not pair the components the way `$819B7F` loads them, and the one geometry that fired came back with `$80` off both x components, which separates nothing.  Without some separation the field heaps up | `$819CC9` | P5 - wants a clean sweep of the separation with a partner that is not a live AI kart |
| S25 | `src/ai.c` `smk_ai_rubber` | the rubber band is the ROM's: the target-speed ROW (`$80AD96` -> `$C8`, read by `$80B074`), chosen from rank and the distance to the kart one place ahead against `$80AF0F`, plus `$80B0A1`'s flat per-rank correction.  Both tables are diffed against the ROM in the self-test (NOTES 167).  **Labelled**: the class index is the engine class where the ROM uses `$C1,x & 7` over four rows; `$DA`'s meaning is unknown so `$B099`'s correction is out; and the `$18` row is never selected because the state that hands it out (`$84,x`) is not modelled | `$80AD5E` per frame, per kart | P6 - wants `$DA` decoded, and a playtest |
| S8 | no audio | silence | SPC700 + S-DSP running its own program | P7 |
| S10 | `src/main.c` draw | **entities RESOLVED for law and size.** The scale is the DSP-1 projection's own third output, `$4200 / depth ALONG THE VIEW AXIS ahead of the kart` (2.1% over 975 samples, against 19% for the euclidean distance the port used), and the drawing is chosen by walking `$84DA3C` = C0 60 30 00 (NOTES 129).  The drawn SIZE is twice the sheet's drawing, measured against a real frame with the kart as the ruler - 23x31 SNES px where the sheet holds 12x16 (NOTES 139).  Labelled: (a) where the larger art comes from is unknown - the whole object sheet tops out at 16x16, so the port magnifies; (b) that nothing draws past the last threshold is a reading of `$84DA38`, not a measurement.  **KARTS still open**: same `+$06`, but their drawing ladder is a different table and has not been measured | same | entities closed; karts P5 |
| S11 | `src/main.c` start sequence | **RESOLVED** — the countdown is the ROM's own 336 frames (`$809FE1` loads `$0146` with `-$150`, `$80A1F8` counts it up, the field goes on zero; NOTES 145), and what runs over it is now Lakitu and his light rather than invented digits (NOTES 162) | same | closed |
| S12 | `src/main.c` entities | **spawn done, MOTION mapped and not yet ported.**  The spawn is the game's own (NOTES 127): two slots in a one-player race, respawned from the lap segment the player's waypoint falls in.  The movers are now understood too (NOTES 146) - Thwomps and moles move **only in Z**, on a per-object BYTECODE SCRIPT, and the port has neither the interpreter nor the scripts | `$85E0B9`: `ldy $04,x / tyx / jsr ($0000,x)` - a record's first word is its handler, which reads args from `$0002,y` and advances `+$04` past itself.  `$85DDA0` is the height command (`$0002,y -> $1F,x`, +6).  Command table around `$85DD26`.  Same shape as the tyre-smoke interpreter in `src/effects.c` | P5 - **next**, one focused session |
| S14 | `src/course.c` direction field | the AI/rescue direction field is our atan2 of the waypoint delta, rounded.  MEASURED against the game's own `$7F:4000` (track 7): **2554 of 2684 cells exact, 130 off by one step of 1/256 turn, worst error 1** | `$81FCFC` builds it through the boot-time arctangent table at `$7F:9000` (`$81E4C5` generates it, `$81F638` reads it as octant base + `table[min*64+max]`) | labelled at that number; port the table if a divergence is ever traced to it |
| S13 | `src/player.c` per character | **decoded and read from the ROM** for the five tables the game has: base top speed (`$81:8000`), acceleration curve (`$81:8010`), off-road caps (`$81:8060`), steering rows (`$81:8088`) and the drift-row adjust (`$80A4C0`: Yoshi/Koopa slide one row lower). Only Mario (P1) and Toad (P2) are VERIFIED against the game so far - the other six characters run on the same code with their own tables but have not been replayed | every character handles differently; there may be further per-character factors (kart-to-kart weight, item odds) not yet found | P3 residual: replay each character (needs a log per character - a real race, not the attract demo) |
| S15 | `src/main.c` `draw_entity` | **RESOLVED** - an object is `SMK_OBJ_PIPE_W x SMK_OBJ_PIPE_H` WORLD pixels and is drawn with `smk_project`'s own scale, the same law as the ground and every kart.  There was never a missing 2x art source: the port was drawing a fixed size, and later a `$4200/zf` size measured from the KART while every other scale is measured from the EYE 61 px behind it (NOTES 154a/154b).  `$4200/zf` still selects the BAND - it is a depth cue, not the size | same | closed |
| S16 | `src/player.c` fall | while falling, our kart descends in z so something is seen to move | `$1F` stays at **1** for all 60 countdown frames - the physics stops and waits, and the visible drop is the SPRITE (NOTES 135a).  Matters more now that sprites below the plane are clipped: ours sinks behind the track, the game's does not | P5 |
| S17 | `src/player.c` start | **RESOLVED, and corrected twice** — the countdown rev is `$80959F`-`$8095E0` (its own routine, immediates `+$C0`/`-$280`/`-$180`, wobbling between `$3F00` and `$4F00`), the band test is `$80956A`, an over-rev is snapped to `$3000` at the line and then bleeds `$70` a frame with `$E2` bit 5 up - the smoke - while bit 0 gives the kart the `$C0` creep instead of stopping it dead (NOTES 163).  The turbo window is two ticks, f214..f217 of 336, and the self-test reproduces the user's own 11008 / 11776 / 11968 from the press frame | same | closed |
| S18 | `src/main.c` start + `src/lakitu.c` | **RESOLVED for the picture.** Lakitu drops in on frame 1 from y = -48, settles at y = 5 having overshot to 7, lights the first red on 179, the second on 244 and the green on 309 - changing to the cheering pose with it - stays down through the release on 336 and climbs back out from 377.  Art: his four H-flipped 16x16 quadrants and the three lamps are the HUD stream's own ($C1:0000), the lamps in the block `smk_hud_load` used to skip.  Read from the game's OAM and VRAM through the Python oracle (`tools/labs/lakitu.py`), which is exactly the route NOTES 145a laid out.  LABELLED: the trajectory is MEASURED, not derived - its generator is not decoded (the only WRAM word tracking the sprite is the OAM shadow at `$0220`), the same standing as the movers in NOTES 152.  **Open**: the sound half of the cue, because the port has no audio at all | Lakitu descends with a semaphore, and it with the sound is how a player times the launch | picture closed; sound is P5 |
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

* **A routine that returns small constants is not necessarily a
  priority or an index into art.**  `$80ADE0` returns `$00`/`$08`/`$10`/
  `$18` and was read as sprite priority while hunting kart collision, in
  the same session that later needed it: they are byte offsets into the
  target-speed table, and they are the whole rubber band (NOTES 167).
  Two hours were spent on the wrong side of the same routine.
* **A negative result is only as broad as the run it came from.**  NOTES
  112 closed kart-to-kart collision as "not in the data" from one demo in
  which no two karts ever touched.  There is a full response, weight
  classes and all, and finding it took ten minutes once the question was
  asked of a race where they DID touch (NOTES 166).
* **Wall-clock timings of the port from an agent shell measure SDL, not
  the game.**  61 seconds of wall clock for 0.49s of CPU: the process
  sleeps waiting to present.  Two "the race no longer finishes"
  regressions were chased that way, one of them additionally starved by
  six of our own races running at once.  Compare builds by CPU time, or
  through the headless tools, and never run two.

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

**A third: the object repro.**  `tools/objhit.c` places the kart a known
distance from a known object, holds the throttle and optionally a
direction, optionally hops, and prints the state across the impact.  Every
number in NOTES 150/150a came out of it, including the table that picked
the low-speed floor.  When a "feel" report arrives ("it feels too
aggressive at low speed"), this is what turns it into a measurement.

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

## What needs playing, and what to look for

**The AI's speed rows are new and need a person (NOTES 174).** The rubber
band was rebuilt from `$80ADA0` and now turns on the flag the original
turns on: whether the kart ahead or behind is the HUMAN. It reproduces the
game's own choice on 94.2% of 39,074 recorded kart-frames, but our own
headless race cannot judge it - an autopilot does not pull away the way a
person does, so the field stays bunched and chases less. What to look for
is exactly the report that started it: run away down a straight and see
whether someone comes back at you. Also worth watching: the catch-up
distances are re-tuned EVERY LAP, so lap 5 should not feel like lap 1.

Everything below is decoded, ported and gated by the headless tools, and
none of it has been judged **in play** except where it says so. That is
not laziness: `--frames` runs from an agent shell block on SDL - 61
seconds of wall clock for 0.49 seconds of CPU (see the wrong turns list)
- so the port cannot be driven end to end from there at all. A single lap
by a person settles more than any rig here can.

In rough order of how likely it is to be wrong:

**1. The rubber band (NOTES 167) — completely untried.**
The whole point of it: the field should now stick to you instead of
falling away. Drive well and they should still be there; drive badly and
one that has dropped back should visibly wind up and reel you in.

* Does a kart that loses touch come back, and does it *stop* winding up
  once it is near again? (chase row `$08` -> hold row `$10`)
* Does the leader ease off when it gets clear air? That is the half that
  is easiest to get wrong, and the half that stops the leader vanishing.
* 50cc against 150cc: the target-speed rows scale with class, so 150cc
  should feel much stickier. The catch-up DISTANCES do not scale with
  class - they scale with the LAP (NOTES 174) - so the thing to watch is
  lap 5 against lap 1 within one race.
* Is the field now too fast, or too slow? Both brakes are in now: the
  `$18` row (a kart in trouble) and `$DA`. But `trouble` is approximated
  from our own crash states and currently never fires, where the real
  game is on `$18` for 8% of kart-frames - so if the field feels
  relentless, that missing 8% is the first suspect.

**2. Kart-to-kart contact after the re-contact fix (NOTES 166/166a).**
The user already reported the first version as too aggressive between AI
karts, and `$819C93` was missing - a second contact inside the pair's
eight-frame cooldown should now cost nothing at all unless both karts
have nearly stopped.

* Does the pack still knock itself apart through a corner, or does it
  lean and stay?
* Bowser or DK Jr against Toad or Koopa: two weight classes should feel
  clearly different from same-weight contact. Ramming a heavier kart from
  behind should cost you three quarters of your speed.
* `SMK_NO_BUMP=1` turns it all off for an A/B in the same session, and
  `SMK_BUMP_TRACE=1` prints one line per contact.
* Watch for karts heaping up and stalling. Without the separation step
  they did; it now ships as the READING of `$819CC9` rather than the
  measurement, which is the one place the two disagree (S24).

**3. The start rev, the wheelspin and the turbo launch (NOTES 163).**
**CONFIRMED WORKING by the user** - "it also works in our
implementation.  It is slightly harder to pull off given that there is no
sound yet."  That last clause is the whole argument for sound being first
on the build list below: the mechanic is right, and a player still cannot
time it reliably.  Their own emulator recording (`sessions/flag`) holds
the ORIGINAL's version of the same launch for comparison - +50 a frame
off the grid, 186 over the class top (NOTES 171).  What is left to judge:

* Hold accelerate from the moment Lakitu appears: the kart should sit
  there spinning its wheels with smoke, creeping forward at walking pace,
  for about 37 frames, then go.
* Press at the right moment and you should get the boost. The window is
  two ticks - frames 214-217 of 336 - which is **between the first and
  second red lamp**, and 95 frames BEFORE the green. If that feels wrong
  to a player who knows the game, say so: it is measured, but the cue a
  player actually times against is the sound, and there is none.
* `SMK_START_HOLD=frame` holds the throttle from a chosen countdown frame
  if you want to hit the window reliably.

**4. Single race (NOTES 164/165).**
* Eight karts on the grid on **every** course - eleven of them drew no
  opponents at all before NOTES 165, so this is worth a quick tour rather
  than one track.
* You start eighth, which is the ROM's own order; if that feels wrong for
  a single race rather than a GP, that is a design call, not a decode.
* Item boxes still register a pickup that does nothing. Harmless, easy to
  strip if it annoys.

**5. Lakitu and the light (NOTES 162) — reported good, one thing left.**
The green lights 27 frames before the field is released. That is
measured three ways and it is what the game does, but it reads oddly, so
it is worth a second opinion from someone who knows the original.

**6. Menu text (NOTES 165's sibling), the grid and the time-trial start
(NOTES 161)** — all three reported good by the user. Listed only so a
regression in them is noticed.

## Status at a glance

| phase | state |
|---|---|
| P0 oracle | **done** — 65816 interpreter, verified against the game's own decompressor |
| P0.5 running machine | **mostly** — boots, uploads sound, runs races; no PPU picture, no SPC700, no HDMA |
| P1 the track | **done** — themes, tilemaps, tilesets, palettes, surface table, all verified against VRAM |
| P2 start / laps | **done.**  The grid is the game's own per-track record with the player eighth (NOTES 161/164), then the previous race's finishing order through a cup (NOTES 275); the countdown is the measured 336 frames with Lakitu and his light over it (NOTES 162), the rev/wheelspin/turbo launch is the ROM's (NOTES 163), the lap rule is decoded (NOTES 052) and gated on 20/20 courses; the cup's 9/6/3/1 and the coins by slot are the ROM's (NOTES 198/275) |
| P3 physics | **done for the player, and now gated by human runs** — the control is transcribed from the ROM and replays the attract race's human inputs frame-exact: 99.8% / 100% of frames within 1 px (NOTES 106-108), with tyre smoke and dust from the game's own effect object (NOTES 109).  The demo replay is exact end to end for both karts (NOTES 112).  Residual: the other six characters unverified (S13), water/snow effects, pipe-crash spin, kart contact (none observed in the demo - NOTES 112) |
| P4 sprites | **done** — the projection is derived once from the ROM's own DSP-1 geometry (NOTES 083/084): depth(L)=4972/(L-20.36), scale=depth/256 (ratio = Les, the cross-check), camera trails the kart 61 px.  Pose ladder measured pixel-exact (NOTES 080/081).  Residual: kart-sheet rows 1-2 purpose, sprite size quantisation (ours is continuous, labelled) |
| P5 race furniture | **part** - the live phase — ground objects stamped with the ROM's own tiles (NOTES 074), sprite-obstacle entity list decoded and colliding (NOTES 078), HUD set + clock + lap counter on the game's own art, start countdown (NOTES 085).  hazard classes decoded and ported - water ($22) wade/skim, the fall ($24/$26/$20/$28) and Lakitu's rescue as the ROM's own three states with a latched target (NOTES 113, 124).  Breakable blocks done and gated for both themes (NOTES 123/123a).  The sector map now matches the game's own $7F:5000 on all painted cells.  Lakitu's own art is now decoded and drawn for the start (NOTES 162).  Residual: the horizon/backdrop (S5), entity MOTION (S12), item behaviour, the splash/sink effects |
| P6 opponents | **driving and competitiveness in, personality not.**  Flow-field steering (95% byte-exact), ramp launches, wall escapes and a Lakitu rescue get the field round **20/20** GP courses.  Kart-to-kart contact is the ROM's, weight classes and all (NOTES 166).  The rubber band is the ROM's own row chooser, `$80ADA0`, rebuilt in NOTES 174 and reproducing the game's choice on 94.2% of 39,074 recorded kart-frames - it turns on whether the neighbouring kart is the HUMAN, and its catch-up distances re-tune every lap.  **The VS CPU opponent is a trained neural network** (S44): it races through the player physics by pressing buttons, decides every four frames, and beats the scripted driver on courses it never trained on; the scripted `src/autopilot.c` remains as its fallback.  The field's pace is the ROM's now: the AI sheds speed through `$80B064`'s rates instead of snapping to its target, and the four handicap karts take `$80B099`'s bonus in place of the rank penalty (NOTES 277/278, gated on the user's 50/100/150cc recordings).  Residual: the "in trouble" test (`$84`, `$10` bit 5) is approximated - `$84` is now known to be the shrink/hit timer; the distance CACHE is not modelled; per-kart driving personality |
| P7 audio | **effects DONE and decoded** (NOTES 211-246) — the game's own ids, rendered from its own BRR samples off the chip; four engines by driver pair, six rough surfaces, the overtake voices, the held voices (engine/roulette/skid).  **Music PARKED** by the user and off by default.  Residual: nine PENDING items listed under "Where to pick up next" (1a-1i) — the bridge's rate, `$4F`/`$53`, coverage 35/71 |
| P8 modes / menus | **three modes, two players** — the shell: title → players → mode → class → driver → course-by-cup → race → results.  GRAND PRIX runs the cup: the ROM's 9/6/3/1 to the top four, a points screen and an animated championship, the next grid from the last race's order and the coins by slot (NOTES 198/274/275), a retry when ranked out, the trophy.  SINGLE RACE is one cup course: eight karts, the ROM's per-character grid order (`$81EE97`, NOTES 111) with the player at the back (NOTES 164).  TIME TRIAL is alone with one mushroom and keeps the top five lap times per course on disk.  1P / VS CPU (the neural driver, S44) / VS 2P side by side (S42).  Font, palettes, cup order, course names, lap count and the time-trial rules are all ROM-derived (NOTES 147/148).  Residual: the real menu art (S20), the mushroom grant rule (S19), the retry's exact rule |

## Known bugs (the user's list, 2026-08-30)

1. ~~A long jump off the edge of Ghost Valley / Rainbow Road teleports you
   to the other side - the WRAPPED WORLD again (NOTES 063/138).~~ FIXED:
   the position clamps at the world's edge instead of wrapping.
2. ~~Bumping a player while invincible does nothing; it should trigger the
   "banana roll" spin on the victim.~~ FIXED, both directions (a starred
   AI bumping you rolls you too).
3. ~~Lakitu's drop-off is still wrong - not the kart, LAKITU's own position
   through the whole process.~~ DONE - Lakitu is drawn from the KART's
   screen row (kart_top - 27) for the whole descent - awaiting the user.
4. ~~AI karts finish as DNF; the simulation must bring them home with
   times.~~ DONE - any kart still out 90 s after the winner gets a time
   extrapolated by lap order; a full autodrive race shows 8/8 times.
5. ~~The tyre dust / water / mud comes out shifted to the left.~~ FIXED:
   the puffs' base moved to the game's own average offset.
6. ~~The feather does nothing; it should jump with a 360 roll.~~ FIXED
   (NOTES 203): the launch was being overwritten the same frame by the
   collide pass's stale copy-back; now a ~0.6 s flight with the roll.
7. ~~Donut Plains water: sink at once but keep FULL control, very very
   slow; after some seconds Lakitu rescues.~~ DONE (the $22 fall-in on
   theme 2).
8. ~~Koopa Beach DEEP water (and likely Vanilla Lake): the same rule -
   full control, super slow; shallow is fine.~~ DONE (themes 4/5 too).
9. ~~The kart is jumpy on sand; the original is not.~~ FIXED: shake only
   on the classes the user confirmed ($50/$5A/$5C/$5E), absolute pixels.
10. ~~Choco Island shows FOUR piranha plants where the game has one; the
    same for Koopa Beach's cheep-cheeps.~~ FIXED: the ROM's own spawn set
    only (show-all was left on).
11. ~~Rainbow Road Thwomps: touching one is the "banana roll", not a wall,
    and they flash.~~ DONE (S34 for the flash).
12. ~~Moles are not implemented: they rise from holes, and passing over
    one sticks it to your face.~~ DONE (NOTES 210, from the user's
    moles recording): the moles are DONUT PLAINS' entities (not
    Choco's), popping 0..6 on a measured ~130-frame cycle; underground
    they are nothing, popped they latch on and drag the kart to a
    crawl, riding the driver until shaken off (three hops - OURS; the
    recording's mole was never shaken, "they can stick forever").
    Art zero-error on DP's own palette 7.
13. ~~A Thwomp landing exactly on you SQUASHES you - the flattened racer
    sprites are in tmp/new/flattened-racers-after-thwomp.png.~~ DONE
    (NOTES 204, S35): the sheet imported per driver, hazard kind 2,
    flattened 100 frames.  Found on the way: the Thwomps NEVER left
    their parked height in an autodrive race (the release counts the
    player's crossings), so movers now clamp their climb and
    SMK_MV_ON=1 releases them from frame 0 for testing.
14. ~~Bowser Castle 1 has one or two Thwomps in the wrong place.~~ FIXED
    (NOTES 205), and it was exactly "one or two": the spawn offsets are
    a TABLE ($84DAC5) whose fifth entry is ZERO - BC1's last segment
    respawns the first pair, while our linear seg*8 spawned entities
    16-17 at (396,44)/(956,148), two Thwomps the game never places.
    Oracle-verified (tools/labs/bc1seg.py), selftest-pinned.
15. The track map shows the karts on the wrong track. (Could not
    reproduce on the keyboard map - which track/mode showed it?)
16. ~~From the user's screenshot (tmp/"graphic issues.png"): rainbow
    rows at the road/grass boundary, a garbled top-right shape.~~
    STALE - the screenshot was old; the user confirms this was solved
    long ago.

## Known bugs, round 2 (the user's list, 2026-08-30, after testing)

Stubborn ones, and ones marked fixed that are not:

1. Lakitu's drop-off is STILL badly implemented - not the kart, LAKITU's
   own position through the whole process.
2. Tyre dust / water / mud STILL comes out shifted to the left.
3. ~~Feathers fire now but the animation is wrong: the jump should carry
   a 360 movement while airborne.~~ FIXED (NOTES 207): $80B6D1
   disassembled - one exact 360, and the spin states now draw through
   the full rotation rule instead of capping at the side-on frame.
4. ~~Sink on water is STILL not working - anywhere.~~ The MECHANICS were
   working (measured against the live game: skim at speed, $CA=$FF,
   crawl to 123, rescue); what was missing was the PICTURE - the kart
   sat at full height on the water.  It now rides low while wading
   (OURS, 9 px).  Awaiting the user's eyes.
5. ~~Donut Plains water: sink at once, keep full control, very very
   slow, Lakitu after some seconds.  Not happening.~~ See 4: DP deep
   water is class $22 (the round-1 fix keyed on $24, which does not
   exist there - dead code, removed); the $22 cycle is in and measured.
6. ~~Koopa Beach deep water (and Vanilla Lake): same rule.  Not
   happening.~~ See 4/5 - all three courses' deep water is $22.
7. NOT solved: four piranha plants on Choco Island where the game has
   ONE.  Same with Koopa Beach's cheep-cheeps.
8. ~~Rainbow Road Thwomps: the flashy colour is totally wrong -
   probably even the sprite used.~~ DONE: the ripped BLUE Thwomp,
   zero-error on RR's own palette 1, flashing to palette 2 (its exact
   flash coloring - three entries differ); the period is OURS (S36).
9. ~~Rainbow Road Thwomps: you can pass THROUGH them; they should be a
   rock (the spin on touch, but solid).~~ FIXED: theme 7 flags the spin
   AND falls through to the measured bounce.
10. ~~Cheep-cheep (Koopa Beach and probably everywhere): shown as FOUR
    fishes in a block you cannot pass through.  The game has ONE, white
    colours, jumping around its place; touch = banana spin, then you
    pass through.~~ DONE (NOTES 209, from the user's cheep-cheep
    recording): ONE white fish per entity (the ripped ladder on KB's
    own palette, zero error), hopping with the measured leap (vz 316,
    g 18, ~35 frames); touch spins and passes through.
11. ~~The squash triggers TOO SOON - the Thwomp is still at top
    altitude.  It should be on contact.~~ FIXED: the squash fires only
    in the contact band of the drop; higher up the descending block is
    neither wall nor hit.  Selftest-pinned both ways.
12. ~~Bowser Castle 1 and Rainbow Road have FEWER Thwomps than the
    original - a regression from assuming there were too many.~~ FIXED
    (NOTES 209): the game runs FOUR live entities per spawn window in a
    1P race - both recordings show four pairs alive the whole way - and
    the old "two in 1P" reading of $819136 was backwards.  Every course
    now carries its full window (Thwomps, plants, fish alike).
13. ~~The map shows EXACTLY THE SAME TRACK whatever you race on.~~
    FIXED: load_race never rebuilt the minimap, so every shell race
    kept the boot track's picture.
14. ~~The banana spin (every status that causes it) does not move the
    camera in 360.~~ FIXED: the $AA/2 camera term now rides every spin
    state, not just the shell tumble ($80B6D1 shows $AA is the spin's
    home in the feather too).
15. Lakitu's rescue: Mario disappears in the middle of the drop; Lakitu
    shows with Mario coming back but is GONE after dropping you.  The
    original shows him taking TWO COINS with the rod and going up - you
    are not released until he is gone.  PART DONE: after the drop he
    now takes the two-coin fee and rises off (OURS: the pacing), and
    the kart is held until he is gone.  The mid-drop disappearance
    still needs the user's detail on WHICH phase (the fall, the carry,
    or the descent).
16. AI players jitter in X when they get close.  ATTEMPTED: the near
    draw now rounds instead of truncating its centre; awaiting the
    user's eyes.
17. ~~The star's colour cycle is not applied to the TURNING sprites -
    the colours go away when you turn.~~ FIXED: one of the three player
    draw branches used the plain driver palette.
18. ~~Star duration has rules - investigate from the ROM.~~ MEASURED
    LIVE (tools/labs/starlab.py/starlab2.py, NOTES 208): $86 = $200 and
    it runs straight down at 1 a frame on classes $40, $4E, $54 and $5A
    alike - the "pauses at 1 below $52" reading in docs/ITEMS.md never
    manifests.  The port's flat 512 frames is the game's.
19. ~~The poison-mushroom small kart is a SPECIFIC SPRITE, not a scaled
    kart.~~ MEASURED (shrinklab2, NOTES 208): the game draws the SAME
    kart tiles ($80/$A0, the straight pose's left column mirrored) with
    the OAM size bits dropped - half size, same art.  That is the very
    operation the port's half-scale draw performs.  Awaiting the user's
    eyes on what still looks off.
20. ~~When small, a hit from another kart SQUASHES you - no banana
    spin.~~ DONE: every hit but the lightning flattens a shrunk kart
    (player and AI), through the bug-13 squash.
21. ~~When small, another poison mushroom returns you to full size.~~
    DONE, both directions, player and AI.
22. ~~Check the duration of being small (from the ROM).~~ $84 = $440
    (1088 frames), and shrinklab watched it tick 1 a frame live.

## Known issues (measured, not decided)

Things the port does that the game does not, kept for now because taking
them out would leave a hole - but they are NOT deliberate improvements
and must not be filed with the ledger's ledgered deviations (the
speedometer, the side-by-side split, the track map).  The user's own
distinction: *"keep it but as a known issue.  It is not something we
decided to have because it is better."*

1. **The skid loop is invented, and the same on every surface**
   (NOTES 219 / 267).  MEASURED three ways that the game has no
   sustained slide sound at all: nothing is queued while sliding on
   either of the user's two recordings; over 46 drift onsets at speed on
   Ghost Valley the number of DSP voices that START is 2 (noise); and
   the one skid-looking call, `$80:A9A8`'s `$2A`, is the SPIN-OUT settle
   path, not the drift.  Ours plays on `$A8 > 6000` with a cooldown, the
   same sample everywhere - which is why our Ghost Valley sounds like
   our Mario Circuit (the user's report).  Kept because silence there
   feels worse; wrong all the same.  Removing it is one line.

## Where to pick up next

The driving is gated by two human runs at their best numbers (crash
86.2%, Ghost Valley 92.8%), the shell runs two modes, and the field now
races rather than parades. **Before any of the below, the playtest list
above** - four of those items have never been judged by a person, and one
of them changes how every race feels.

In rough order of value:

1. **Sound (S8, P7) — the SFX are IN and DECODED, not guessed
   (NOTES 211-246); music PARKED by the user and off by default.**
   Everything below is what is still PENDING; the working methods are in
   docs/SOUND.md and the memory note, and none of them needs anyone to
   play anything.

   PENDING, in rough order of value:

   a. **The bridge (class $50) still sounds wrong to the user** after
      three passes. Sample `$16`, two pitches `$0400`/`$0300` alternating,
      re-keyed every 5 frames - all measured and stable across the whole
      log. The untested suspicion is that the clatter RATE tracks the
      kart's SPEED, and one speed is baked into the loop. Measure it at
      two speeds before touching anything else.
   b. **`$4F` and `$53` are played by the game and never by the port.**
      Both captured. `$4F` comes from `$80:F0CC`, which sets a `$012C`
      timer on an object; `$53` from `$80:F809`, an object's `$66` timer
      expiring. Neither EVENT is identified yet, so neither is wired.
   c. **`$48` (boost) renders at 0.67 of the chip's span** and every
      other id now lands within 1%. `SFX_DEBUG=1` says the music takes
      the voice back at f2265, so it may be right - it needs one look.
   d. **`$5B` is captured but not wired**: which screen uses it rather
      than `$2C` is not measured, and the course-screen split the port
      used to have was an invention.
   e. **`$29`, `$5D`, `$5F` are the last ear-only wirings.** The port
      plays them; the game has never been seen to. `$5D`/`$5F` are the
      poison mushroom, and forcing the shrink state plays nothing
      (NOTES 233).
   f. **Coverage is 35 of 71 call sites.** What is left is the menus'
      per-screen sites and the item-hit variants, both reachable by
      forcing.
   g. **Distance banding is applied to the shell bounce only.** The
      other families off `$80:FBBC` (`$33`, `$36`, and the kart's own
      `$3C`) share `$84:D9DA`'s threshold table. For the player's own
      hits the nearest band is always right, so this only matters once
      another kart's events are audible - which they are not today.
   h. **Whether a surface's pulse rate tracks speed** - the same question
      as (a), for grass and sand as well as the bridge.
   i. **Music stays PARKED** (the user, 2026-08-30): the loops "don't
      start from a sensible part of the song" and real intros are wanted;
      "this will take long to get properly running and it is minor."

   DONE and confirmed by the user: the engine note, four engines by
   driver pair, the item roulette, the skid, the five... now six rough
   surfaces, the overtake voices, the item lock-in, throwing versus
   dropping, and the wall pair.

2. **Items (P5) — IN and played (docs/ITEMS.md, NOTES 185/190/192).**
   The roulette replays the user's race 285/285 frames exact; the nine
   items work from the game's own box through its icons to the road; the
   AI's weapons ride behind their karts and drop (measured); the road art
   is the ripped ladders through the ROM's palettes. Still OURS (S31): the
   thrown banana's arc and the fireball's weave (both wait on a recording
   with one in it), the shell's spin rate, the hit box.

3. **~~Moving obstacles (S12's other half)~~ — IN, and confirmed by the
   user 2026-09-02: "thwomps movement is working since a long while".**
   `smk_course_movers_step` runs the measured cycle every frame: parked at
   `z = 4096` until the first lap completes and then, per object after its
   own stagger, FALL (from `-64`, gaining `-32` a frame, clamped at 0) ->
   HOLD 135 frames -> RISE `+64` a frame, capped back at the parked
   height, and round again. x and y never move (NOTES 152).

   LABELLED, and the only piece still open: `SMK_MOVER_RISE = 120`, how
   long the rise lasts. The trace gave 119/116/96 on one object and
   144/199/93 on the other and it is NOT proximity-driven, so it is
   script data - the interpreter at `$85E0B9` with the Thwomp/mole
   commands at `$85DDA0` (table `$85DD26`) is where the real number
   lives. Everything else about a Thwomp - spawn positions, the blue
   Rainbow Road art, the squash's contact band, standing like a rock -
   is measured and in.

4. **~~Grand Prix (P8)~~ — IN (NOTES 198, 274).** Four cups of five in
   the ROM's order, 9/6/3/1 from `$85:BEB4` to the top four, a points
   screen and an animated championship between races, the grid of the
   next race from this one's finishing order (measured, NOTES 275) and
   the starting coins by grid slot with it, a retry when ranked out,
   final standings with the trophy after the fifth.  LABELLED: the
   retry's exact rule and text, and whether the AI scores on a retried
   race.

5. **~~Finish the rubber band's two loose ends (S25)~~ — CLOSED by
   NOTES 174.** Both were measured from the user's recorded race. `$DA`
   is not a 0-60 counter: it is STATIC per kart block for a whole race
   (`0,0,0,0,2,4,6,8`), and it is also the routine's own parameter
   (`$80AD9F lda $DA,x`). And `$C1 & 7` is not a class index at all - it
   is the **lap**, so `$80AF0F` is re-tuned every lap of every race. What
   remains open is smaller: `$84` and `$10` bit 5 (the "in trouble" test,
   8% of kart-frames in the real game and 0% in ours), the `$E2` bit 1
   policy, and `$0E50` - none of which fired in a one-player race.

6. **The graze exemption (S6).** The ROM exempts a slip under 45 degrees
   from the crash deceleration (`$80A0EB`), the recording shows the game
   doing it, and applying it used to make the port WORSE (82.0% ->
   73.4%). The reason may have been removed since: the slip now reaches
   `vel_angle` (NOTES 150), which is the upstream difference that decode
   was fighting. Cheap to retest, would likely lift both human gates.

7. **The kart-to-kart separation (S24).** `$819CC9` ships as the READING
   of the routine, not the measurement - the one place in this port where
   the two disagree. It wants a clean sweep with a partner that is not a
   live AI kart driving its own frame.

8. **Per-character verification (S13).** Nearly free: both human runs use
   character 1, so gating another character is mostly bookkeeping. Six of
   eight unverified.

9. **Coin animations — the ONE-coin drop is DONE; the four-coin drop is
   deferred with the spin-out.** The user: *"leave the 4 coin drop for
   later because they are part of 'being hit' animation (which is a
   series of spins).  One coin drop is more relevant since you get to
   bump into others a lot."*  Still open: the PICKUP animation.** Both ends: the pickup, and the
   coins that spill when you are hit. The trigger for the second is now
   decoded - `$85:E4B2` decrements `$0E00` by one per call, and the
   pickup wraps at 100 (NOTES 172) - so what is missing is the sprite
   work and which hits call it. Small, visible, and the counter it
   animates is already correct.

10. **~~The finish sequence~~ — camera, results and now the celebration
    pose (NOTES 199) are in.** *"the race doesn't stop
    abruptly. If you arrive top 4, camera shows you from the front while
    the character celebrates for a few seconds. Then after that, you get
    times: your times, and the AI's total times and positions."* Three
    pieces: a camera move, the celebration poses, and a results table.
    The third is the chequered flag's neighbour - Lakitu's third job is
    still unstarted, and the art is the checker across `$68`-`$9F`.

11. **~~The dashboard (user's list)~~ — IN (NOTES 248/249).** Coins and
    position are the ROM's own art, read off the game's OAM in a race
    rather than invented (`tools/labs/hudlayout.py`); coins had never been
    drawn at all - `draw_hud` took them and threw them away. The kart icon
    beside them is LIVES, not laps (the user), and lives are deliberately
    NOT implemented. The game shows no lap number anywhere: Lakitu
    announces it.

    OURS and ledgered: the corner it hangs in (the window is not 256x224 -
    the standing rule is ROM art, our layout); the position drawn as the
    ROM's sprite digit at DOUBLE scale, because the game's own big number
    is background-layer art and is nowhere in OAM; the SPEEDOMETER, which
    the user asked for and the original never had - a NEEDLE rather than a
    figure ("that number doesn't mean much"), in the bottom-left corner,
    sweeping 200 degrees against an ABSOLUTE `SMK_SPEED_MAX` of `$07E0` =
    2016 - the ROM's own boost clamp at `$80:A5E8`, confirmed by forcing
    a mushroom and watching it sit flat there for eight frames - with this class's top
    marked on the scale so a mushroom shows as the needle going PAST it,
    and the surface cap's amber arc between (NOTES 250/251); and the telemetry
    overlay - surface, slip angle, the class-top bar, and the lap count,
    which lives there because the game has no lap display to copy. The
    overlay TOGGLES on **H**, with `SMK_NO_TELEMETRY=1` to start without
    it. Still open: Lakitu's lap announcement, after which the telemetry's
    lap number can go.

12. **~~The track map (user's list)~~ — IN (NOTES 200, S33).** The
    course's own tilemap in the bottom-right corner, every kart a dot, M
    toggles it.

13. **~~Two-player (user's list)~~ - IN (NOTES 255, S42).** A PLAYERS
    setting beside the mode - 1P / VS CPU / VS 2P - with the field still
    eight karts. **SIDE BY SIDE, not stacked**, as the user decided:
    *"split screen but side by side (left/right). Today we have
    widescreens!"* A deliberate deviation, ledgered. The cost was NOT
    only the second camera: halving the width squashes the world, so the
    projection now takes its horizontal scale from a reference width
    (`smk_render_proj_width`) and a narrow view shows a narrower SLICE
    rather than the same slice squeezed. Player 2 takes the ROM's own
    rival slot `racers[1]`, so bumps, coins, shells and stars work
    between the two people with no special case. Controllers decide what
    the menu offers: none disables VS 2P, one is pad + keyboard, two is
    one each.

14. **Art detail.** The near-object source (S15) and the kart size ladder
    (S10's other half). Both visible, neither affecting how it plays.

15. **~~The VS CPU driver (user's ask)~~ - IN, and it is a neural network
    (S44, docs/RL.md).** Trained by PPO in the port's own environment,
    built into the binary, pressing buttons through the player physics.
    What is left for it, in order: self-play (two policies on one grid);
    the turbo start as a learned skill rather than `cfg.start_hold`; the
    ninth GP parity check (Mario Circuit 1 from frame 357, one box giving
    two items - `KNOWN_GAPS` in `tools/rl/check_obs.py`, printed and
    measured, not hidden); and the ROM's own per-character AI data, which
    would give the seven field karts a personality the network does not
    need.

Deliberately parked: the background's near plane and sky gradient (S5) —
the user has said it matters less than feel.

### Off this list, done

Three items the user raised were fixed rather than queued, and are on the
playtest list above instead:

* **A standstill turns nothing, but the driver leans.** The kart never
  turned at rest (`$80A9B8[0] = 0`); the countdown was throwing the
  steering away, so the lean never happened (NOTES 175).
* **The AI's speed logic.** Rebuilt from `$80ADA0`; it turns on whether
  the neighbouring kart is the human, and its distances re-tune every lap
  (NOTES 174).
* **Crashing into a raised Thwomp.** `SMK_MOVER_CLEAR`, ours and ledgered
  as S26, set from the user's own rule and gated on their recording
  (NOTES 176).

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

### P2 — Start line, checkpoints, lap logic  ✅ DONE for one race
- ✅ Real start positions and grid, per track, from the game's own record
  (S2 closed, NOTES 161) - and the player starts eighth, which is the
  block order the ROM lines up (NOTES 164).
- ✅ Lap counting from the decoded rule (NOTES 052) with the monotonic
  guard, gated on 20/20 courses by `tools/laptest.c`.
- ✅ The countdown: 336 measured frames, Lakitu and his light over it
  (NOTES 162), and the rev / wheelspin / turbo launch (NOTES 163).
- ✅ The cup around it: points, championship, the grid from the standings (NOTES 274).

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
- ✅ Ground objects stamped with the ROM's own tiles; the sprite-obstacle
  entity list decoded, drawn and collided; the start-light sequence, with
  Lakitu (NOTES 162).
- Next: ITEMS - the biggest remaining gameplay gap - then entity MOTION
  (S12), then the real horizon/backdrop per track (kills S5).

### P6 — Opponents  (driving ✅, competitiveness ✅, items ✅, the neural CPU ✅, personality next)
- ✅ Steering from the game's own direction field, wall escape, ramp
  launches, Lakitu rescue: 20/20 GP courses lapped.
- ✅ Kart-to-kart contact, weight classes and all (NOTES 166).
- ✅ Rubber-banding: the target-speed row from rank and gap (NOTES 167).
- ✅ The AI's own weapons and the items thrown at it (docs/ITEMS.md).
- ✅ **The VS CPU driver is a neural network** (S44, docs/RL.md): PPO in
  the port's own environment, across the three classes and three
  situations, 16 courses trained and four held out; built into the
  binary, pressing buttons through the player physics, `NEURAL` on the
  dashboard, with `src/autopilot.c` as the fallback off the GP courses.
- Next: `$DA` and the fourth class row (S25); per-kart personality — the
  ROM has per-character AI data we have not looked for; for the neural
  driver, self-play (two policies on one grid, which the two-player path
  already has the machinery for) and the turbo start as a learned skill.

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

### P8 — Modes, menus, HUD, polish  (three modes ✅)
- ✅ Time trial: five laps, splits, the top five per course kept on disk.
- ✅ Single race: eight karts, the ROM's grid order, a finishing place.
- ✅ Grand Prix: the cup, its points and championship screens, the grid
  from the last race's order and the coins by slot, the trophy (NOTES
  198/274/275).
- ✅ The shell: title → players → mode → class → driver → course-by-cup →
  results, in the ROM's own font and palettes (NOTES 147/148); the class
  and the driver each on their own screen, the drivers as side-view cards
  with the ROM's weight class (NOTES 276).
- ✅ Two-player, side by side: 1P / VS CPU / VS 2P (NOTES 255, S42).
- Next: the real menu art (S20).
- **2P is SIDE BY SIDE, not stacked.** DECIDED by the user: *"split screen
  but side by side (left/right). Today we have widescreens!"* The original
  stacks two Mode 7 views because it has 256x224 to divide; on a modern
  panel the same division left/right gives each player a taller, wider
  view than the original ever had. This is a deliberate deviation - the
  first one in the presentation layer - and it is in the ledger. The
  renderer is resolution-independent, so the cost is the second camera,
  not the geometry.
- Both 2P shapes are supported: two humans, or one human + one CPU.

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
