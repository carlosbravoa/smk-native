# Decode log

Numbered entries, newest last. Addresses always included. Ruled-out
hypotheses are kept and marked SUPERSEDED, never deleted — the trail of what
was eliminated is worth as much as the conclusions.

---

**001** — Cartridge is HiROM + FastROM, DSP-1 (cart type `$05`), header at
`$FFC0`. Reset `$80FF70` → boot `$80803A`. Main loop `$808056` spins on the
vblank flag at DP `$44`; game mode at DP `$36` dispatches through `$808197`
(main) and `$8081BF` (NMI), 15 entries each; IRQ index DP `$D0` → `$808B12`,
6 entries.

**002** — Graphics codec decoded from `$84E09E`/`$84DF38` (identical bar the
output bank, `$7F` vs `$7E`). 8 commands incl. inverted back-references.
Proven three ways: 69 assets decode; 49 sit at prev-blob start+consumed;
independent encoder round-trips all and beats the original size on every one.

**003** — SUPERSEDED by 005. Hypothesis: track layouts would show up as
16384-byte decompression outputs in a ROM-wide scan. Scan found exactly one
(at `$C70B29`) and it was tile pixels, not a map. Conclusion at the time:
"track geometry is stored some other way".

**004** — Asset uploads located by DMA size. Of 36 `sta $4305` sites, two are
large: `$81E7B5` (16384 bytes → VMDATAL only = Mode 7 tilemap low bytes) and
`$81E769` (12288 → VMDATAH only = 192 tiles). Mode 7 interleaving makes the
half-word DMA a positive ID.

**005** — Track tilemaps are compressed **twice**. Loader `$81E745`:
table `$81EB5B[track*3]` → decompress ROM→`$7F:C000`, then decompress
that →`$7F:0000`. All 24 entries yield exactly 16384 bytes. This is why the
003 scan failed: the outer stream's output is itself a stream.

**006** — Tile expander `$84E3C7`: source = 256 palette-base bytes + 32
bytes/tile of 4bpp pixels, low nibble first; non-zero nibble OR'd with the
tile's base, zero left alone. `0x100 + 192*32 = 6400` matches the tileset
blob exactly. Tileset table `$81EBA3`; only entry 1 is a full 192-tile set —
per-course binding not yet traced (roadmap P1).

**007** — Palettes: table `$81EBBB`, 8 × 512 bytes, all words bit-15-clear
(BGR555). Palette 0 is the dirt/tan Mario Circuit ramp.

---

**008** — RISK R1 RESOLVED (scoped, not eliminated). The DSP-1 *is* used by
gameplay code, at DR `$6000` with status `$7000`. Four commands only, each
identified from the write/read counts around the command byte:

| cmd | site | shape | operation |
|---|---|---|---|
| `$00` | `$81B043` | 2 params → 1 result | signed multiply |
| `$04` | `$84FE3F` | angle, radius → 2 results | sin/cos |
| `$0C` | `$81B2C1` | angle, dx, dy → 2 results | 2D rotate |
| `$28` | `$81B2F3` | 3 params → results | vector length |

Confirmed by use, not by assumption: `$84FE3F` writes `tya` (angle) then a
pulled radius and stores result 1 and its two's complement to `$02,x`/`$04,x`
— textbook sin/cos. `$81B2C1` writes an angle from `$9C,x` then a dx and dy
built by subtraction, and reads two results — textbook 2D rotate.

Consequence for the roadmap: P3 does **not** need a general DSP-1
reimplementation, only these four, and all four are ordinary fixed-point
maths. The oracle emulates them directly. Their exact output scaling still
has to be matched bit-for-bit — that is what the oracle is for.

---

**009** — Course→theme binding found (kills ledger S3). Two adjacent tables:

* `$81EC1B`, 20 bytes — cup/course → track index. Index is
  `$0150*5 + $0152`, i.e. cup*5 + course. Cup order:
  `[7,19,16,17,15] [18,1,2,3,0] [13,10,12,9,14] [11,6,8,4,5] [2,0,4,12,8]`
  (the fifth row is Special Cup reusing earlier courses).
* `$81EC2F`, 24 bytes — track → **theme*2** (routine `$81EC5E` stores it to
  `$0126`, whose consumer multiplies by 1.5 to index a stride-3 table).

8 themes, distribution: 0→{1,8,16} 1→{0,7,14,15,21} 2→{2,11,19,22}
3→{10,18} 4→{4,12,20} 5→{6,13,23} 6→{3,9,17} 7→{5}.

**010** — Asset loads are **not independent**, and a strict decoder gets the
wrong answer. `$81E67A` runs `$EC5E` (theme) → `$E745` (tilemap) → `$E6D4`
(tileset) → `$E72E` (palette), and every decompression stages through
`$7F:C000`. Theme 6's tileset stream contains back-references that reach
before its own start; on hardware those read what the *tilemap* load left
there. Decoded standalone it looks malformed.

Two further hardware details the C port initially got wrong, both found by
diffing against the oracle:

* the write cursor (`$0E`) and the absolute back-reference pointer (`$04`)
  are **16-bit and wrap inside the 64 KB bank** — `sta $7F0000,x`;
* the expander always processes 192 tiles no matter how much the stream
  produced, reading past the end into whatever WRAM held. Several themes
  rely on this; refusing to over-read loses real tiles.

The port now models one 64 KB WRAM bank and performs the loads in the game's
order. Result: C tilemaps, tilesets and palettes are byte-identical to the
game's own code for all 24 tracks, checked in `tools/test.py`.

---

**011** — Surface behaviour found, and with it the start of the kart RAM map.
`$80FA62` is the per-kart surface lookup and reads as plainly as it gets:

```
lda $1C,x        ; kart Y
asl A x4 / and #$3F80      ; (Y>>3)*128
lda $18,x        ; kart X
lsr A x3 / ora $00         ; + (X>>3)
tax / lda $7F0000,x        ; tilemap byte
tax / lda $0B00,x          ; <- surface table, RAM $0B00
sta $68,x                  ; kart's current surface
```

RAM `$0B00` is filled by `$81EB11`: decompress `$87:FDBA`, then copy **192
bytes** (one per Mode 7 tile) from the per-theme 16-bit offset in table
`$81EB4B`. Offsets: `$100 $40 $129 $283 $205 $2E7 $1A9 $367`.

The blob decompresses to only 883 bytes, so themes 5 and 7 read past its
end. That is not a decode error — Rainbow Road (theme 7) genuinely comes
back as `$00` for almost every tile, which is exactly right for a course
that is road surrounded by nothing.

Semantics established so far, from the consumer at `$80F8A5`:

* **bit 5 (`$20`) = solid.** `lda $68,x / and #$0020 / bne` jumps to the
  collision response, which writes `$8000` to `$42,x` and `$80` to `$26,x`.
* bit 7 (`$80`) is a separate class, branched out at `$80FA8F`.
* out of bounds (`>= $400` on either axis) sets bit 0 of `$10,x` and forces
  surface `$40`.

Kart RAM map so far (indexed by a per-kart X):

| addr | meaning |
|---|---|
| `$18,x` | X position, world pixels 0..1023 |
| `$1C,x` | Y position |
| `$2A,x` | angle (fed to DSP-1 sin/cos at `$80F8CF`) |
| `$10,x` | flags; bit 0 = off the map |
| `$42,x` | collision state |
| `$58,x` | current tilemap index |
| `$68,x` | current surface byte |

Verified: our surface tables are byte-identical to the game's `$81EB11`
for all 8 themes. Rendering each course coloured by surface class produces
clean regions that follow road, grass, walls, water and Rainbow Road's void.

---

**012** — Text encoding. Table `$81DC7F` is a string table:
`letter = byte - $0A` with `A = 0`, and `$FF` terminates. `16 0A 1B 12 18 FF`
= MARIO; LUIGI, BOWSER, PRINCESS follow. Bytes `$00-$09` are presumably the
digits and `$24+` punctuation (`$29`/`$2C` appear around "BEST"). Needed for
P8 (menus/HUD), not before.

**013** — `$81DBB1`'s `track*20` is **SRAM save data**, not track geometry.
`sta $306660,x` writes bank `$30:$6660`, which is the cart's 2 KB SRAM
(HiROM maps SRAM at `$20-$3F:$6000-$7FFF`). `$81DB94` initialises six
three-byte BCD records per track with `$99 $59 $0A` — the 9'59"0A "no
record" time. So the layout is 6 best times x 3 bytes + 2 = 20 bytes per
track. Ruled out as a source of start positions.

**014** — ~~RULED OUT: the Mode 7 matrix is not written by direct stores.~~
**SUPERSEDED by 018.** The claim was that nothing touches `$211B-$211E`.
That was a *coverage artifact*, not a fact: the static trace reaches only
~8% of the ROM. Running the game (entry 018) shows it writing `$211B` and
`$211C` during boot. HDMA is still likely involved for the per-scanline
matrix, but "nothing writes the matrix" is wrong and was stated too
strongly. Lesson: never report absence from a trace that covers 8%.

**015** — HONESTY ITEM: the DSP-1 model in `tools/smktool/dsp1.py` is an
**assumption, not a decode**. The four commands were identified from their
call shapes (entry 008), and the maths implemented from the documented
DSP-1 behaviour — sin/cos with a 65536-unit circle, multiply returning
`(a*b)>>15`. None of the output *scalings* has been verified against
anything. The oracle therefore verifies routines that do not touch the
DSP-1, and only those.

This blocks P3: porting kart physics on top of an unverified DSP-1 would
bake a guess into the core of the game, which is exactly what the roadmap's
principle 1 forbids. Confirming it needs either a reference DSP-1
implementation to diff against, or a place in the game's own code where a
DSP-1 result is compared to a known constant. Neither is in hand yet.

---

**016** — P3 reconnaissance: the kart motion model, and its units.

The DSP-1 sin/cos wrapper at `$80F8CF` is the movement primitive:

```
lda #$04 / sta DR        ; cmd 4 = sin/cos
lda $2A,x / sta DR       ; angle
pla       / sta DR       ; radius  <- this is the kart's SPEED
poll SR, then
lda DR / sta $22,x       ; result 1  -> velocity component
lda DR / eor #$FFFF / inc A / sta $24,x   ; -result 2 -> the other component
```

That settles two things the DSP-1 model could only assume:

* **result order is sin then cos** — confirmed by the sibling routine at
  `$84FE3F`, which lays the two results out as `[r1, -r1, r2, r2]`, i.e. the
  `[sin, -sin, cos, cos]` of a rotation matrix;
* **the radius argument is speed**, so `$22,x`/`$24,x` are the velocity
  vector, not a matrix.

Units, all cross-checked against code that constrains them:

| quantity | where | unit |
|---|---|---|
| angle | `$2A,x` | 65536 = full circle. `$80F79D` adds `#$0400` for a 1/64 turn |
| position | `$18,x`, `$1C,x` | whole pixels 0..1023 (`cmp #$0400` bounds them at `$80FA65`) |
| velocity | `$22,x`, `$24,x` | 8.8 fixed point, pixels/frame — floor of `±$0100` (= 1.0 px) at `$80F9C1` |
| friction | `$80FA4A`, `$80FA52` | 8.8 multipliers: `$0080` = 0.5, `$00F0` = 0.9375 |

`$80F9A7` clamps: if `|$22,x|` and `|$24,x|` are both under `$00C0` the
velocity snaps to `±$0100`. Above that it multiplies each component by a
factor from the two 4-word tables indexed by `$56,x` (a wall/edge index) via
the helper at `$80FC74`.

Position is integer pixels while velocity is 8.8, so there is a fractional
accumulator somewhere — probably the words just below (`$16,x`/`$1A,x`).
Not yet confirmed; do not port the integration until it is.

**Deliberately NOT ported yet.** Two things are missing: the fractional
position accumulator, and DSP-1 output scaling (NOTES 015). Porting now
would bake both guesses into the core. The native game keeps its clearly
labelled placeholder motion (ledger S1) until they are settled.

---

**017** — Position integration decoded, and it closes the DSP-1 scaling
question for the command that matters.

Kart position is **16.16**, kept as two words. `$80FD9D` copies the whole
block and shows the layout: `$16` X fraction, `$18` X integer, `$1A` Y
fraction, `$1C` Y integer, `$1E`/`$20` Z fraction/integer.

The integration at `$80879D` is one 32-bit add of `velocity << 8`, written
as two 16-bit adds:

```
clc
lda $21,x / and #$FF00 / adc $16,x / sta $16,x   ; frac += (vel & $FF) << 8
lda #$FF00 / and $22,x                           ; high byte of velocity
bpl + / ora #$00FF                               ; sign extend
xba                                              ; arithmetic >> 8
adc $18,x / sta $18,x                            ; int += (vel >> 8) + carry
```

Reading `$21,x` rather than `$22,x` is the trick: the word straddling the
byte boundary puts velocity's *fractional* byte in the high position, which
is `(vel & $FF) << 8` for free.

**This resolves S9 for DSP-1 command $04 by unit analysis.** For the
arithmetic to be consistent - 8.8 velocity feeding a 16.16 position, with
the `±$0100` velocity floor at `$80F9C1` meaning exactly 1.0 px/frame - the
DSP-1 must return `radius * sin(angle)` **unshifted**, with radius being the
speed in 8.8. No other scaling makes the units work. Commands `$00`, `$0C`
and `$28` remain unverified, but movement no longer depends on them.

Angle convention, from velocity being `(sin, -cos) * speed`: **0 points
along -Y and increases clockwise** (a compass bearing).

Ported to `src/kart.c` in the ROM's own arithmetic. What is still invented
is only how player input drives `speed` and `angle` (ledger S1) - the
acceleration curve, drift, hop and per-surface response are undecoded.

---

**018** — The oracle now **runs the game**, and that changes the plan.

Static decoding has hit a ceiling that is structural, not incidental: the
ROM contains **177 `jmp ($0000,x)` and 81 `jsr ($0000,x)`** dispatches where
the pointer is already in a register, loaded from a state-machine record.
Those cannot be resolved by reading. Every remaining behaviour phase (P3
onward) is gated on being able to *observe* the game instead.

So the interpreter was extended into a minimal machine:

* **APU stub** — no SPC700. The 65816 only needs the IPL handshake: ports 0/1
  read `$AA`/`$BB` for "ready", and the upload loop then waits for port 0 to
  echo the counter it wrote. Echoing walks the game through its whole sound
  upload (108k port writes observed).
* **`$4210` RDNMI / `$4212` HVBJOY** with an NMI flag that clears on read.
* **NMI dispatch** (`CPU.nmi`) pushing PB/PC/P and vectoring through
  `$00:FFEA`, plus `run_frames()`, which fires NMI from the main loop's
  vblank spin and runs until the spin is reached again. That is one
  simulation step per vblank — the game's own pacing.

Result: **the game boots and runs.** 1.53M instructions to reach the main
loop, then ~1200 frames in 0.1 s. The frame counter at `$34` advances
correctly and `$81E02D` sets the initial mode 13, which is genuine.

**Where it stops.** It idles in mode 13 and never advances. `NMITIMEN` is
`$B1`: NMI enabled, auto-joypad enabled, **and H/V IRQ enabled**. The IRQ
handler at `$80801F` is not being driven because there is no scanline
timing, so anything sequenced from IRQ never happens. INIDISP goes to `$80`
(forced blank) after ~60 frames, consistent with a transition that never
completes.

**Next concrete step for this line:** scanline timing — an H/V counter,
`$4207-$420A` (HTIME/VTIME) compare, IRQ dispatch through `$00:FFEE`, and
`$4211` TIMEUP. HDMA (`$420C`) after that. Neither is exotic; both are a
day's careful work, and they unblock P3 completely.

This is scope the roadmap deliberately deferred (risk R2 said build an
emulator "when a whole-frame question appears, not speculatively"). The
question has now appeared.

---

**019** — Scanline timing and IRQ added; the game advances, then hits the
real wall: **it needs a working SPC700.**

Added: an H/V counter, `$4207-$420A` HTIME/VTIME compare, IRQ dispatch via
`$00:FFEE`, `$4211` TIMEUP (clears on read), `$213C`/`$213D` counters, and a
nesting guard so an interrupt is never re-entered.

With IRQ the game *does* progress: **mode 13 -> mode 0 at frame 41**, which
is real progress the NMI-only build never made. Mode 0's handler is a bare
`rts`, so mode 0 is a legitimate idle state.

Then it stops, and the reason is unambiguous:

* `NMITIMEN` is back to `$00` — the game deliberately disabled NMI and IRQ;
* execution is spinning at `$81F510`, `cpx $2140` — **the APU handshake
  again**.

So this is not a timing bug. Having finished its first phase the game talks
to the sound driver a second time, and now expects specific replies rather
than the IPL echo. Our stub answers the *boot* protocol only.

**Consequence for the roadmap: audio is not an optional late phase.** The
SPC700 is on the critical path for making the game *run at all*, because
the 65816 blocks on it. P7 has to move up, or at least the SPC700 core does.
Two options, and the first is almost certainly right:

1. **Emulate the SPC700 + S-DSP properly** and upload the game's own driver.
   The SPC700 is a small, well-documented 8-bit CPU; the DSP is harder but
   is only needed for *sound*, not for the handshake. A CPU-only SPC700 with
   a stub DSP would unblock the 65816 immediately and give real audio later.
2. Reverse engineer SMK's specific 65816<->SPC700 command protocol and fake
   the replies. Cheaper now, wrong later, and it has to be redone for audio.

Recommend option 1: an SPC700 interpreter is a day's work with the same
shape as the 65816 one already written, and it converts a permanent
blocker into a solved problem.

---

**020** — The APU handshake, modelled properly. The game now boots, uploads
its sound driver and progresses through several game modes.

The conversation, observed rather than assumed (this is the whole protocol):

```
P2=lo P3=hi     destination address
P1=d0           FIRST DATA BYTE - it doubles as the "data follows" flag,
                so it must be non-zero
P0=$CC          kick; the IPL echoes $CC
P0=$00          commits d0; the IPL echoes 00
P1=d1 P0=$01    ... and so on, the IPL echoing the counter each time
P2/P3=entry P1=$00 P0=counter+2    ends the block and runs the driver
```

Three things had to be right, each found by watching the game stall:

1. **The block-end test is at block boundaries, not per byte.** Port 1 holds
   *data* during a transfer and is frequently zero; treating any zero as
   "end of upload" truncates it to 26 bytes.
2. **The final echo must survive.** The CPU is still waiting to read back
   the value that ended the block, so advertising "ready" immediately
   destroys the reply it is spinning on. Echo first, go ready on the next
   read.
3. **After the driver is running, commands to port 0 must not clobber the
   ready flag.** The game sends a reset-style command (`P1=$3F … P0=$1F`)
   and then polls for `$AA`/`$BB` again, expecting the driver to have jumped
   back into the IPL. Echoing that command leaves `$1F` in port 0 and the
   game polls forever.

Result: **55825 bytes uploaded across 8 blocks, entry `$0800`**, and the
game runs — mode 13 → 0 → 2 → 0 → 3, with the frame counter advancing and
`$4218`/`$4219` being read, so input reaches it (holding Start moves 3 → 2).

Not yet reached: the race. Modes 2/3 are the title/attract screens and
menu navigation needs an input pattern we have not found. Mode 2's `$18`/
`$1C` are static, so it is not a demo race.

**021** — Sound driver dumped without emulating the SPC700.

Since the upload protocol tells us every byte and its destination, the SPC700's
64 KB RAM image can simply be *recorded*, and that image plus the register
block is exactly what an `.spc` file is. `smk spc` writes one: 53132/65536
bytes populated, entry `$0800`, structurally valid 66048-byte file.

This matters for the audio plan: it means music can be rendered locally from
the user's own ROM by any SPC player, so the project never ships audio.

Honest caveat: this is the state *immediately after upload*. The driver has
not executed, so the S-DSP registers are zero and the driver is idling
waiting for a "play track N" command on its ports. Producing a dump that
plays a chosen track needs that command byte, which is game-specific; we
have the command *stream* logged (`APU.commands`) but have not yet mapped
values to tracks. Playback itself is **unverified** - there is no SPC player
in this environment to test against.

---

**022** — **The game runs a race, and the ported kinematics are verified
against it.** This is the P3 acceptance criterion met.

Three fixes got there, each found by watching where it hung:

1. **`$4212` bit 6 is HBlank, and the game waits on it.** `$808B3C` is
   `bit $4212 / beq` — a two-instruction infinite loop when bit 6 never
   sets. We have no dot counter, so the flag alternates on each read; every
   such wait then terminates in a couple of iterations, which is all the
   game needs from it.
2. **Mode changes go through `$32`, not `$36`.** `$81E09A` does
   `lda $32 / sta $36 / stz $32`, then `cli` and `sta $4200 = $B1`. Writing
   the *pending* mode is the game's own transition path and it performs the
   setup; writing `$36` directly skips it. Setting `$32 = 12` enters race
   mode cleanly.
3. **The kart state is not in the direct page.** `$B4` holds a 16-bit base
   — `$1000` — and every `$18,x` style field is relative to it. Chasing
   `$0018` absolute gives nonsense.

With those, race mode runs: the frame counter advances, the joypad reaches
the game, and the kart drives.

Field confirmed by measurement: **`+$EA` is speed**, the magnitude of the
velocity vector — which matches the static read of `$80F9DF`
(`lda $EA,x / cmp #$0500`).

### The verification

Captured the real kart state for 240 frames and checked the rule in
`src/kart.c` against it:

| prediction | result |
|---|---|
| `pos += velocity<<8` using the **earlier** frame's velocity | 190 exact, 288 differ (worst 0.074 px) |
| `pos += velocity<<8` using the **later** frame's velocity | **478 exact, 0 differ, error exactly 0** |

So the integration is exactly right, *and* the ordering question is settled:
**within a frame the game updates velocity first, then integrates position
with the new value.** `src/kart.c` already does `smk_kart_face()` then
`smk_kart_move()`, which is that order.

Also confirms, from live data, three things previously derived only by
argument: angle 0 really does point along -Y (the kart drove with
`ang = 0` and only Y decreasing), position really is whole pixels 0..1023,
and velocity really is 8.8 (`vy = -589` gave -2.30 px/frame, and Y moved
526 -> 512 over six frames).

`make verify-physics` runs this end to end. It regenerates the trace from
the user's ROM every time, so no captured game data is committed.

---

**023** — Player control, the kart array, and the whole motion core.

**`$0E32` is the demo flag.** With it set the karts are AI-driven and the
joypad does nothing; clearing it hands control to the player. That is why
"input reaches the game but nothing responds" — both were true. With it
cleared, holding accelerate takes the kart from 194 to 646 with a tapering
per-frame gain, which is a real acceleration curve.

**The kart array is at WRAM `$1000`, eight karts, stride `$100`.** `$B4`
holds the base of the kart currently being processed. Every `$18,x` style
field in the physics is relative to that, which is why chasing absolute
`$0018` gave nonsense.

**The motion core, `$80A4E1`** — this is the whole chain:

```
clc
lda ...   / adc $EC,x / sta $E8,x   ; speed fraction += accel fraction
lda $EA,x / adc $EE,x / sta $EA,x   ; speed          += accel + carry
bpl +
lda #$0000 / sta $E8,x / sta $EA,x  ; negative speed clamps to zero
+   sta $6000                        ; DSP-1 sin/cos, radius = speed
    lda $6000 / sta $22,x            ; vx =  sin * speed
    lda $6000 / eor #$FFFF / inc A / sta $24,x   ; vy = -cos * speed
```

So speed and acceleration are **both 32-bit**, split across two words, and
the *high* word is the 8.8 value handed to the DSP-1 as its radius:

| field | meaning |
|---|---|
| `$E8,x` / `$EA,x` | speed fraction / speed (8.8) |
| `$EC,x` / `$EE,x` | acceleration fraction / acceleration |

This also confirms the DSP-1 result order independently for a third time:
first result is sin (into `$22`), second is cos, negated into `$24`.

Ported to `src/kart.c` as `smk_kart_accelerate()`, mirroring the ROM's
field layout. What is still invented is only *what writes `$EC`/`$EE`* —
the input and state logic that decides acceleration.

**024** — NEGATIVE RESULT, and worth keeping. Forcing race mode with
`$32 = 12` gives every track the same starting grid:
`(951,755) (919,731) (951,708) (919,683) ...` — eight karts in two staggered
columns. Tempting to read as "the start grid is fixed in world space".

It is not. Checking that grid against each course's own surface table puts
**5 of 24 tracks starting inside solid geometry** (tracks 1, 3, 5, 9, 16).
So this is a default position left over from skipping the real race setup,
not the game's per-track start. Ledger S2 stands.

The lesson is the cheap check: a start position that lands in a wall is
obviously wrong, and testing it took one query. Any observed value from a
forced state needs a plausibility test before it is believed.

---

**025** — Player acceleration and steering decoded. Found by instrumenting
the running game: watching which PC writes the player's `$EE` (acceleration)
and `$2A` (angle) fields pointed straight at them.

**Acceleration — `$80B035`:**

```
jsr $B074          ; A = TARGET speed
sec / sbc $EA,x    ; target - current
bcc  decelerate
    ldy #$0690 / sty $10
    jsr $A7E1      ; A = accel, from a table indexed by current speed
    stz $EE,x
    sta $ED,x      ; writing at $ED spans $ED/$EE: accel32 = A << 8
    rts
decelerate:
    eor #$FFFF / inc A          ; how far over target
    cmp #$0200 / clamp to $01FF
    asl / asl / xba / and #$0006
    lda $B064,y / sta $EE,x     ; four-entry deceleration table
```

**`$80A7E1`**, the accel lookup: clamp speed to `$03FF`, multiply by 8, mask
`#$FE00`, `xba` (an arithmetic `>>8`), add the table base from `$10`, and
read a word. So **acceleration is a function of current speed via a table**,
and deceleration is a function of how far over target you are.

**Target speed — `$80B074`:** indexes `$0800` by the kart's stat field
`$C0,x`, takes two bits of that, adds `$C8,x`, and reads a **target-speed
table**, then adds a bonus chosen by `$DA,x` or `$E6,x` from two small ROM
tables at `$80B099`/`$80B0A1`.

**Steering — `$80AFBE`:** `$FA,x` is the *target* angle and `$A2,x` the
current steering angle. If the difference is within `±$0200` the angle snaps
to the target (`sta $A2,x / sta $2A,x`); otherwise it slews via `$80AFF9`.
So steering is a slew-rate-limited follow, not a direct write.

**The constraint that matters for the port.** The acceleration table
(`$0690`) and the target-speed table (`$06B0`) are in **WRAM**, built at race
setup from the character and engine class. Their contents are therefore game
data and must not be baked into this repository as constants. The port has
to locate the ROM source of those tables and read them at runtime, the same
way it reads tilemaps and palettes. That is the next step for S1, and it is
the reason S1 is not being closed with measured numbers.

Structure confirmed against the running game; the exact index arithmetic
matched about a third of sampled frames on a first pass, because the
deceleration branch also writes `$EE` and the sampling straddles both. Worth
redoing carefully when the ROM-side tables are found.

---

**026** — S1's data dependency closed: the physics tables have a ROM source.

Instrumenting writes into `$0690-$06CF` during race setup found exactly one
writer, `$81FEB6`:

```
ldx $0030          ; engine class
ldy $FED5,x        ; -> source for that class
ldx #$0000
-  lda $0000,y / and #$00FF
   asl A x4        ; the ROM stores BYTES; the game widens each by <<4
   sta $0690,x
   iny / inx / inx
   cpx #$0080      ; 128 bytes written = 64 words
   bne -
```

`$81FED5` holds three pointers — `$FEDB`, `$FF1B`, `$FF5B` — exactly 64
bytes apart: **one 64-byte table per engine class** (50cc/100cc/150cc).
Storing them as bytes is why every value in RAM is a multiple of 16.

Layout within the 64 words, from the consumers:

| words | meaning | RAM |
|---|---|---|
| 0..15 | acceleration, indexed by current speed (`$80A7E1`) | `$0690` |
| 16..31 | target speed, by character stat and class (`$80B074`) | `$06B0` |
| 32..63 | further per-class constants, not yet identified | `$06D0` |

Verified: reading the ROM this way reproduces the table the game builds in
RAM exactly, and 150cc's accelerations are uniformly larger than 50cc's.

Ported as `src/physics.c`. The native game now uses **the ROM's own
acceleration curve and target speeds**, read at runtime — so no game data is
compiled in, and `--class 0/1/2` selects the engine class.

What remains invented in `step_kart()` is only *policy*: which target-speed
entry the player selects (the ROM picks it from undecoded per-character
stats), the braking rate, and the steering rate. Those are now the whole of
ledger S1.

---

*(next entry: 027)*
