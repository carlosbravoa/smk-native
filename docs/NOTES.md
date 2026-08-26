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

**027** — Steering architecture, and `$81F638` identified as atan2.

`$80AFBE` is the *applier*: `$FA,x` is a target angle and `$A2,x` the
current steering angle; within `±$0200` the angle snaps to the target,
otherwise it eases toward it. So steering is a slew-rate-limited follow, not
a direct write — which is why the kart's heading lags the stick.

`$80B0B1` is the **AI's** target-angle producer, and it reads plainly:

```
lda $10,x / and #$0003 / beq (player branch)
lda $C0,x / asl / tay          ; this kart's waypoint index
lda $0A00,y / sec / sbc $1C,x  ; waypoint Y - kart Y
pha
lda $0900,y / sec / sbc $18,x  ; waypoint X - kart X
jsl $81F638                    ; -> angle
```

So `$0900`/`$0A00` are the AI waypoint X/Y tables in RAM, and **`$81F638` is
atan2** — which explains the earlier confusion when its magnitude-comparison
loop showed up as a hot spot and looked like a stall. It is just the
normalisation step of an arctangent.

The player's steering is the `$10,x & 3 == 0` branch, not yet followed.

Note the shape here for P6: opponent AI is "steer toward the next waypoint",
with the waypoint list per track. That is a small, tractable decode once the
per-track data is located.

---

**028** — PPU model added, and with it the kart sprites.

The oracle now models **DMA, VRAM, CGRAM and OAM**. That is not an attempt
at a PPU; it exists so asset formats can be *read out of the machine*
instead of inferred. It paid for itself immediately.

**Verification of the Mode 7 pipeline, end to end:**

| | |
|---|---|
| Mode 7 tiles, our expander vs VRAM | **12288/12288 identical (100%)** |
| Mode 7 tilemap, our extraction vs VRAM | **16306/16384 identical (99.5%)** |

The 0.5% residual is genuine: the game edits the tilemap at runtime
(`$81B797` writes 2x2 blocks when item boxes and coins are used).

That comparison also caught a mistake in my own harness: VRAM matched
**track 14**, not track 0. Writing `$0124` before forcing mode 6 is a no-op,
so every observation I have made through `boot_into_race(track=N)` was
actually on track 14. That also explains NOTES 024's identical start grids.

**Kart sprites.** Logging DMA during a race showed 128-byte transfers from
banks `$C0/$C2/$C4/$C5` at addresses `$200` apart. So a frame is **512 bytes
= 16 tiles = 32x32 pixels**, uncompressed 4bpp, stored in PPU order: a 4x4
sprite with a **16-tile row stride**, frames advancing 4 tiles across then
64 tiles down.

Colours need no extra work: CGRAM arrives in one 512-byte DMA from
`$7E:3A80`, which is the same palette blob the track uses. Sprite palettes
sit at `$90` (Mario), `$A0` (Luigi), `$B0` (Peach).

`src/sprite.c` reads them from the ROM at runtime and the native game now
draws the player's kart. The frame *choice* is a placeholder — the ROM picks
it from heading relative to the camera plus steering state, undecoded — but
the frames and their layout are the ROM's.

---

**029** — The attract loop reaches a **demo race**, and it corrects two
earlier entries.

Leaving the game running with no input: mode 13 -> 0 -> 2 (title, ~28 s of
game time) -> 0 -> **mode 1**, with 43 sprites in OAM and eight karts sitting
on a starting grid at speed 0. Mode 1's handler at `$808067` is nearly the
same routine chain as mode 6's — `$84ECC0`, `$877B`, `$8621`, `$81856D`,
`$8E91`, `$861A`, `$8E60`, `$818587`, `$84D56F` — so **mode 1 is the demo
race** and mode 6 the played one. Waiting for mode 6 was looking for the
wrong thing.

**Correction to 024 and 028.** The demo race runs on **track 7**, not 14,
and all eight karts start on surface `$40` (road):

```
kart 0 (952,756)   kart 1 (920,732)   kart 2 (952,708)   kart 3 (920,684)
kart 4 (952,660)   kart 5 (920,636)   kart 6 (952,612)   kart 7 (920,588)
```

Those are within a pixel of what forcing mode 6 produced, so that grid is
**genuine**, not a default — NOTES 024 was too pessimistic. What remains true
is that the same grid lands on solid ground for 5 of 24 courses, so those
must place their karts differently. Two staggered columns 32 px apart, 24 px
between rows.

**Kart sprites in OAM.** A kart is **four 16x16 sprites** forming a 32x32
block, tiles `N, N+2, N+32, N+34` (the 16-tile VRAM row stride), and each
kart has its own tile slot: `$C0`, `$C4`, `$C8`, `$CC` … four tiles apart,
with its own palette (0,1,2,3 …).

That is the important part: the game **streams the chosen frame into a fixed
per-kart VRAM slot** every frame, in 128-byte quarters. So the frame the game
picked is recoverable from the **DMA source address**, which makes the
frame-selection rule measurable rather than guessable.

---

**030** — Sprite sheet structure, and the demo race's limit.

Measuring each frame's silhouette shows the sheet is **three size tiers**,
not one rotation sequence: heights fall into bands of ~30 px (frames 0-10),
~27 px (11-21) and ~24 px (22-31), with fill counts dropping the same way.
That is SMK drawing distant karts smaller — a distance LOD, about eleven
rotation steps per tier. Frames 0, 11 and 22 are outliers (centroid ~18
against ~14.5), so each tier's first slot is something other than a plain
rotation step.

The native game now uses tier 0 and leans through neighbouring frames with
the steering input. **This is inferred from the sheet, not decoded** — the
ROM picks its frame from the kart's heading relative to the camera and that
rule has not been read out of the code.

**Why it is not decoded yet.** The frame the game chooses is recoverable
from the DMA source address (NOTES 029), but only while karts are actually
being drawn, and neither route reaches that state:

* the demo race (mode 1) puts eight karts on the grid at speed 0 and then
  **ends** — the frame counter resets to 33 and the attract cycle restarts —
  without the countdown ever releasing them. No kart sprite DMA occurs.
* forcing mode 6 gives moving, AI-driven karts and working physics, but the
  game never draws them: the only per-frame VRAM traffic is 128 bytes from
  `$7F`, nothing from the `$C0-$C7` sprite banks.

So both paths give *half* a race: one initialises properly but never starts,
the other runs but never renders. The countdown is the thing to chase; the
likely candidate is that it is gated on the sound driver, since the APU
stub answers the handshake but nothing else, and a race start in this game
is music-synced.

---

**031** — NEGATIVE RESULT: acknowledging driver commands does not release
the race countdown.

Hypothesis: the countdown is sequenced against the sound driver, so the APU
stub's refusal to acknowledge commands leaves it waiting. Tried making the
stub echo port-0 command writes the way a driver would, with a fallback to
`$AA`/`$BB` after a long poll.

Outcome: the countdown still never runs — eight karts sit at speed 0 for
600+ frames — **and it made things worse**. The sound upload dropped from
two uploads of eight blocks (55825 bytes) to one of seven (54320), because
with commands echoed the game never sees `$AA`/`$BB` when it wants to send
the next bank. Reverted.

So the countdown is gated on something else. What is now known about the
two ways into a race, neither of which is complete:

| | demo race (mode 1) | forced race (mode 6 via `$32`) |
|---|---|---|
| karts initialised on the real grid | yes | yes (same coordinates) |
| physics runs | **no** — speed stays 0 | yes, AI-driven |
| karts drawn (sprite DMA) | no | no |

The next diagnostic is which of the physics routines actually execute in
each case — if the speed integration at `$80A4E1` never runs in the demo,
the gate is upstream of it and can be found by walking back from there.

---

**032** — All eight drivers, and where the demo race actually stops.

**Sprite sheets.** Rendering one frame from every bank at `$2000` found
seven sheets, and pairing them with palettes came from a grid of every sheet
under every sprite palette:

| driver | sheet | palette |
|---|---|---|
| Mario | `$C0:2000` | `$90` |
| Luigi | `$C0:2000` | `$A0` |
| Bowser | `$C1:2000` | `$80` |
| Peach | `$C2:2000` | `$B0` |
| DK Jr | `$C3:2000` | `$B0` |
| Yoshi | `$C4:2000` | `$80` |
| Koopa | `$C5:2000` | `$90` |
| Toad | `$C6:2000` | `$90` |

Seven sheets for eight drivers: **Mario and Luigi share one**, differing
only by palette, which is how the game does it too. `$C7` is not a sheet.

Worth knowing: the **sprite half of the palette is not theme-independent** —
37 to 53 of its 256 bytes change between themes, so the game re-tints the
drivers per course. The indices above are right; the exact colours follow
whichever track is loaded. Still not decoded: the game's own character
table, which is what really binds a driver to a sheet and a palette.

**Where the demo race stops.** Counting executions of the physics routines
over ~60 frames settles it:

| routine | demo race | forced race |
|---|---|---|
| surface lookup `$80FA62` | 8 | 8 |
| acceleration `$80B035` | **0** | 5 |
| steering apply `$80AFBE` | **0** | 8 |

In the demo, *no kart update runs at all* — only the surface lookup. The
karts are not stalled mid-race, they are held before the countdown ever
releases them, and whatever releases it never fires. In a forced race the
opposite holds: the updates run but nothing draws the karts.

So the gate is a race-state transition upstream of the kart update, and it
is the one thing standing between here and a complete race. Finding it means
diffing low RAM between the two states and looking for the flag the update
path tests.

---

**033** — The kart state machine, which is what the demo race is sitting in.

`$80AD6F` dispatches per kart:

```
phx
lda $AC,x        ; per-kart STATE index (already doubled)
tax
jmp ($AD76,x)    ; every handler starts with `plx`
```

Table at `$80AD76`, and the handlers are small enough to read at a glance —
each one just sets the acceleration field `$EE,x`:

| state | handler | effect |
|---|---|---|
| 0 | `$80B035` | **drive**: accelerate toward the target speed |
| 1 | `$80A647` | `accel = 0` — coast |
| 2,3,5,6,7 | `$80A5A8` | `jsr $B768` |
| 4 | `$80A5AD` | the boost/brake check |
| 8 | `$80B015` | |
| 9 | `$80A606` | |
| 10 | `$80A5A1` | `accel = -$38` — brake |
| 11 | `$80A55A` | |

Beyond 11 the table is not a table. Also nearby: `$80A64F` sets
`accel = -$10` and `$80A656` sets `-$08`, so the deceleration rates are
plain constants in their handlers.

~~This is the gate: in the demo race `$80B035` never executes, so `$AC,x`
is not 0.~~ **WRONG, and disproved by the next measurement** — see 034. The
state table above is correct; the inference from it was not.

---

**034** — Correcting 033, and what mode 1 actually is.

Forcing the karts into the driving state was a one-line test of 033's
inference, and it failed for an instructive reason: **`$AC,x` was already 0
for all eight karts.** State 0 — drive — was selected the whole time, yet
`$80B035` never executed. So the karts are not parked in an idle state; the
per-kart dispatcher at `$80AD6F` is not being reached at all.

That points upstream, to the mode handler. Comparing the two call chains:

```
mode 1 ($808067):  $84ECC0 $877B $8621 $81856D  $80FC  $8E91 $861A $8E60
                   $818587  $80EC  $A120  $84D56F
mode 6 ($808136):  $84ECC0 $877B $8621 $81856D $83F37F $8E91 $861A $8E60
                   $818587 $83F360 $9C3D $84D56F
```

They share most of the frame, but mode 6 calls `$83F37F` and `$83F360`
where mode 1 calls `$80FC`, `$80EC` and `$A120`. Those two `jsl`s into bank
`$83` are the likely kart-update entry.

Which also means **mode 1 is probably not the demo race**. Eight karts sitting
on the grid, 43 sprites, nothing moving, and a different per-frame chain
reads much more like the pre-race course intro — the camera pass over the
starting line before a race begins. The real attract-mode race is then a
further mode we have not reached, and the user's note that the game "goes
into demo/attract mode after a few seconds" is consistent with a sequence we
are only part-way through.

Lesson recorded because it nearly cost more: 033 stated a conclusion drawn
from a table plus an absence, and the cheapest possible test contradicted it
within minutes. An absence ("routine X never runs") constrains *where* to
look, it does not identify *what* is wrong.

---

**035** — Mode 1 *is* the demo race after all; it just takes longer than I
was waiting.

Following the attract sequence without stopping: mode 13 -> 0 -> 2 (title,
~28 s) -> 0 -> 1, karts on the grid at speed 0 for a long stretch, and then
**karts 2 and 3 start moving** (speeds 44, 48). Earlier runs sampled only
karts 0 and 1, or gave up before the countdown finished — both mistakes,
and both mine.

So NOTES 034's guess that mode 1 is the course intro is wrong too. It is the
demo race, and the sequence is: karts placed on the grid, a long hold, then
they are released one after another.

What remains true from 034: the per-kart dispatcher does not run *while the
karts are held*, and `$AC,x` is 0 throughout. So the hold is implemented
somewhere above the dispatcher, not by parking karts in an idle state.

Still no kart sprite DMA even while they drive. That reframes the sprite
question usefully: the graphics are evidently uploaded in bulk before the
race rather than streamed per frame, so the frame the game picks shows up in
the **OAM tile number**, not in a DMA source. Which is easier to read, not
harder.

Two lessons, both cheap to have avoided:
* when watching for "something happens", watch **all** the actors, not the
  first one;
* an attract sequence has its own pacing — a wait that feels generous in
  wall-clock terms can still be short in game time when the simulation runs
  at a fraction of real speed.

---

**036** — NEGATIVE RESULT: VRAM kart tiles do not match the ROM sheets
byte-for-byte.

With karts moving in the demo race, the frame the game picked should be
readable by taking the 4x4 tile block at each kart's OAM tile slot
(`$C0`/`$C4`/`$C8`/`$CC`, sprite base `$8000` from `OBSEL = $02`) and finding
it in the character sheet. Over 400 frames: **zero matches**, against all
seven sheets and all 32 frames of each.

So one of these is wrong, and the next step is to find out which rather than
guess: the sprite tile base, the order tiles are assembled into a 32x32
block in VRAM, or the assumption that VRAM holds sheet bytes unmodified.
Dumping the VRAM block and rendering it will settle it in one look — if it
draws a kart, the bytes are there and only the correspondence is wrong.

Also worth noting: no kart-sprite DMA occurs at all while the karts drive,
so whatever puts those tiles in VRAM does it another way — probably direct
`$2118`/`$2119` writes during vblank, which the PPU model does capture.

**037** — World-space sprite projection, and a bug worth naming.

`smk_project()` inverts the ground-plane mapping: take the offset from the
camera (wrapping on the 1024-unit plane), split it into forward and
rightward components, and the row and column follow directly, with
pixels-per-world-unit as `focal / forward`. Everything that sits on the
plane goes through it.

The native game now draws the **rest of the starting grid** — real positions
from the game's own grid, real sprites, scaled by distance across the three
size tiers. They do not drive; there are no opponents yet.

The bug worth naming: this was the *second* time a feature was added to the
interactive render path and silently missing from `--shot`, so screenshots
disagreed with the game. Both now go through one `draw_scene()`. If two code
paths render, they will drift; give them one function the first time.

---

**038** — ROOT CAUSE: the karts are not drawn because the DSP-1 model is
badly incomplete, and a race leans on it constantly.

The trail: VRAM at the kart tile slots turned out to hold HUD graphics, and
in a *moving* race OAM contains only HUD — "FINAL LAP", digits, item boxes,
portraits. The karts are simulated but never reach OAM at all.

Counting DSP-1 traffic over 60 race frames explains it:

```
$00 multiply     x943      modelled
$04 sin/cos      x138      modelled
$28 vector len   x15       modelled
$0C rotate       x4        modelled
everything else  ~1500     NOT modelled
```

The long tail is partly an artefact — an unknown command desynchronises our
parameter stream, so the following parameter bytes get counted as further
"commands" — but that cuts the same way: **once one unmodelled command
arrives, everything after it is garbage.** A race issues DSP-1 work every
frame for projection, and kart screen positions come out of it. With the
maths wrong, the karts project nowhere and are culled.

This promotes ledger **S9** from "unverified scalings" to the top blocker.
It is no longer only a P3 fidelity question; it gates **P4 (which frame the
game picks), P6 (AI), and any attempt to compare our renderer against the
game's own output**. NOTES 008's "only four commands" was measured from a
static scan of gameplay code and was simply too small a sample.

What it needs: implement the DSP-1 properly rather than command-by-command —
the parameter/result shapes for the full command set, and scalings checked
against something. Until then, everything observed *through* a race that
touches projection is suspect; the physics results in 022 and 026 are not,
because they were checked against the game's own arithmetic directly.

---

**039** — The DSP-1, implemented properly. S9 substantially closed.

Replaced the four-command model with the full documented command set (30
commands, our own maths), then corrected it against the game's own traffic.
The method that worked: log every DSP write **with the program counter**,
and compress the stream into runs. Command bytes come from single-byte
stores at their own PC; parameters come as two-byte word stores — the PC
pattern makes the framing unambiguous.

**Three corrections the traffic forced:**

1. *The `$01` command was a mirage.* What the old model counted as 64
   attitude calls was the high byte of `$02`'s fifth parameter, misread
   after a desync. With raster fixed there are no `$01` calls anywhere.
   (My first "fix" — reshaping `$01` to two parameters — was wrong twice
   over; the PC-context trace killed it before it shipped.)
2. *Raster (`$0A`) is a streaming mode, not a call.* One command byte, one
   starting Vs, then the chip serves a 4-word Mode 7 matrix group per
   scanline, auto-advancing as each group is read — the game reads **96
   groups per screen half** (split screen: two rasters per frame, Vs
   `$0087` and `$FFB7`). The mode ends with **`$8000` sentinel words**
   followed by the next command byte. One byte-ambiguity: after a sentinel,
   `$00` could open another sentinel word or be the multiply command; the
   real stream always means the sentinel, so the model prefers the word.
3. *The camera model was upside down.* `$02`'s `F` is not the camera — it
   is the **focal point on the ground** (the player's kart, in quarter-pixel
   units: Fx = x·4). The eye sits `Lfe` away at elevation `Azs` on the far
   side of azimuth `Aas`; `Les` is eye→screen. Race values: Lfe=Les=256,
   Azs=$3400 (73°) → eye 18.5 px behind and 61 px above the kart — exactly
   SMK's camera. Forward is `(sin Aas, −cos Aas)`, the same 0 = −Y
   convention as kart headings (NOTES 017). With that fixed, `$06` projects
   kart 2 to (H=−3, V=155, M=208) instead of "offscreen".

**Results.** Boot: `$02`×129, `$0A`×129, `$80`×128 — and `$80` is explained:
`$81E3EC` writes it 128 times as a flush before first use; consume-nothing
is the right handling. Race: `$02`/`$04`/`$06`/`$28` only. **Zero unknown
commands anywhere; the stream never desyncs.** `verify-physics` still
passes bit-exact (238/0), and new OAM entries with kart-block tile patterns
appeared in the first post-fix race snapshot.

Still approximate, and marked in code: `$14` gyrate (passthrough), the
`$08`/`$18` fixed-point conventions, exact raster output scaling, and the
`Vof`/`Vva` sign conventions. None is on the current critical path; each is
logged when traffic first touches it.

---

**040** — Kart sprite streaming, decoded from live DMA with the fixed DSP-1.

With karts actually drawn, their graphics traffic became observable, and it
settles several structural questions:

* **A frame upload is four 128-byte chunks `$200` apart** — one chunk per
  tile row, because the sheet is 16 tiles wide (16 × 32 bytes = `$200`).
  This independently confirms the sheet layout formula from NOTES 028: the
  chunk-0 source encodes the frame as
  `src = $2000 + (frame/4)*$800 + (frame%4)*$80`.
* **Uploads happen only when the displayed frame changes.** Straightline
  driving produces zero sprite DMA; the earlier conclusion that "the game
  streams the chosen frame every frame" (NOTES 029) was too strong.
* **At a race start, every kart's initial frame (frame 7) is uploaded in a
  burst** across all seven banks — banks `$C0..$C6`, with `$C0` serving two
  karts (Mario and Luigi), one more confirmation of the shared sheet.
* **The sheet region runs to 48 frames** (`$2000..$8000`). Frames 32-47 are
  mixed content: spin/tumble poses, far-tier variants and specials, so "3
  tiers × ~11 rotation steps" from NOTES 030 describes only frames 0-31.
* The 2-chunk transfers from `$7C00/$7E00` during the start-line phase are
  16-px-tall effects (start revving/exhaust), not kart frames.

Rare DSP commands also surfaced once the stream was clean: `$01`, `$0B` and
`$10` are real but rare (race transitions) — so NOTES 039's "the `$01`
calls were a mirage" is right about the boot stream and wrong as a general
claim.  All three decode correctly now.

---

**041** — The sprite frame-selection rule, measured.

Method: force-spin a kart in place in the running game (write its `$2A`
heading each frame, ~1.1°/frame) and log which sheet frame every upload
came from.  Three full rotations, 473 uploads, transitions repeatable to
about a degree.

Averaging the two approach directions (the game applies ~±3.6° ≈ `$280` of
hysteresis at each boundary), the thresholds land exactly on round angle
units.  For `rel` = kart heading − camera azimuth, folded to `0..180°` with
the far half mirrored by hflip:

| |rel| below | frame |
|---|---|
| `$1000` (22.5°) | 1 — squarely from behind |
| `$1800` | 2 |
| `$2000` | 3 |
| `$2800` | 4 |
| `$3000` | 5 |
| `$3800` | 6 |
| `$4800` (101.25°) | 7 |
| `$5800` | 8 |
| `$6800` (146.25°) | 9 |
| else | 10 — the frontal arc through 180° |

Steps of 11.25° through the rear/side arc, widening to 22.5° toward the
front.  Frame 1, not 4, is the rear view — the silhouette-based guess in
NOTES 030 picked the wrong frame.  The heavy hflip usage seen in OAM
(2973 flipped vs 1875 not) is this rule's mirror half.

Ported to `src/sprite.c` as `smk_sprite_for_heading()` and wired through
the game: grid karts now show the correct view for the camera angle.

Still assumed, and labelled: tiers 1/2 share these boundaries (measured on
the near tier only), which side maps to hflip (visual check pending), and
the *player* kart's `rel` — in the ROM it is real camera lag during turns;
our camera tracks exactly, so a small lag is synthesised from steering.

---

**042** — The per-track course container: sectors, racing line, finish line.
P2's data and P6's data turn out to be one structure, and it is now decoded
and verified byte-exact.

**Where it lives.** Word tables at `$81:FF9B` (record stream) and `$81:FFCB`
(waypoints), 24 entries each, both into bank `$C6`; `$0E68` selects an
alternate source at `$08:847B`/`$08:84C6` (other modes). Loader at
`$81FBC0-$81FEB5`.

**The sector map.** The record stream paints a 64×64 map of 16-px cells at
`$7F:5000`, one SECTOR per record, `$FF`-terminated. Record =
`[type][pos.lo][pos.hi]` + payload, `cell = pos.lo + (pos.hi << 6)`. Seven
paint shapes: type 0 rectangle (w,h); types 2/4/6/8 four triangle
orientations (run right/left × rows down/up, width shrinking); types 10/12
diagonal wedges (columns of h cells, next column at +63/+65, shrinking).
Types 10/12 carry a payload byte the paint loop never reads.

**Semantics** (reader at `$808931`): low 7 bits = sector, bit 7 = the
finish-line strip, `$7F` = off-course (sets kart flag bit 1). The kart's
current sector is kept near `+$DC` in the kart block. The finish strip is a
w×h rectangle of bit-7 ORs from the params table `$81:80D4` (6 bytes/track:
a lap word → `$014A`, strip cell, w, h).

**The racing line.** One waypoint per sector from the second stream, 3 bytes
each: x/8, y/8, and an attribute whose **low 2 bits select the AI's
target-speed row** (`$80B074` reads `$0800,y & 3`) — 0 slow through 3 fast,
visibly slow before hairpins. The loader repeats point 0 at the end to
close the loop.

**Why the map first compared at only 63%:** the game never zeroes
`$7F:5000`, and the Mode 7 tile expander's output buffer overlaps it — the
"fill" in unpainted cells is leftover tile pixels. Masked to painted cells,
our builder matches **2606/2606 (100.00%), finish flag included**, and the
racing line matches the live game word-for-word.

**Ported** as `tools/smktool/course.py` and `src/course.c` (twins, both
tested). The native game now has: opponents driving the racing line with
the decoded speed classes, and lap counting from sector progress + the
finish strip.

**Honest status of the opponents:** the data is the ROM's; the steering
CONTROLLER is ours and incomplete. In a harness, AI karts complete genuine
full laps on 5-6 of 20 GP tracks at plausible times (20-40 s) and fail on
the rest by two identified modes: *orbiting* (turn radius at speed exceeds
waypoint distance — the ROM must brake on heading error in a way we have
not decoded) and *jump segments* (three stuck segments cross solid cells
the game vaults over; we have no Z axis). Tuning the controller by trial
and error made it worse, so it stays simple and labelled; the fix is to
instrument the ROM's own AI update (`$80AFF9` slew, its brake rule) next.

---

**043** — The AI controller, measured from the demo race — and a rule that
does not exist.

Captured 2000 frames of the six AI karts' controller state (heading,
target angle `$FA`, speed, acceleration) and correlated.

**The brake-on-error rule does not exist.** Mean speed is flat (~700-730)
across every heading-error bucket from 0 to 60+ degrees. The ROM's AI does
not slow for corners; it out-turns them. My orbiting theory (NOTES 042) was
half wrong — the fix is turning harder, not braking. Two controller hacks
built on that theory made lap completion *worse* and were reverted.

**The turn law, completed.** `$80AFF9` is a table lookup: word index
`32 + ((min(err,$1FF) >> 6) & 7)` plus the per-kart `$C8` row, into the
same per-class physics blob (words 32-63 = four turn-rate rows). The
measured per-frame steps (±352, ±576) match **class 1, row `$C8`=8**
exactly, and target speeds (700-1050) match the target rows at offset +4 —
so the demo runs class 1 with the AI on row 8/+4 of each table.

**A turnaround mode.** Steps of exactly `$800`/frame appear 451 times, and
bucketing by error shows the split cleanly: below ~90° the table rows
dominate; above ~90° the `$800` step does. Rule: |error| > ~$4000 → turn
$800/frame (about-face in 16 frames).

All three are in the native AI now (turn table row 8, target row +4,
turnaround above $4000). Lap completion in the harness: 6/20 GP tracks at
realistic times (15-28 s/lap).

**The remaining blocker is not the controller.** The stuck tracks fail at
identical sectors under every controller variant tried. Three cross solid
cells the game jumps over (no Z axis yet); the rest wall-grind where the
game's collision state (`$80F8C0`: `$42,x`=$8000, `$26,x`=$80, with its own
recovery handler selected via `bit $42,x` at `$80F8A0`) would bounce the
kart free. Decoding that response is the next scoped item, and it is also
ledger S6.

---

**044** — The wall response, measured — after three capture attempts that
each taught a method lesson.

Attempt 1 sampled `$B4` for the player kart and got a non-kart block
mid-loop; attempt 2 identified the player by input response but steering
blind never touched a wall in 900 frames; attempt 3 aimed at a known solid
cell but wrote the heading every frame, and the *fourth* run showed why the
one-shot aim also fails — under player control the game rewrites the target
angle `$FA` every frame from input. The data finally came from attempt 3's
own tape, which had recorded repeated impacts I initially dismissed.

**The response, read off the trace** (surface class `$80`, track 7):

```
f48  v=(-770,-42)  $10=$2000        approaching the wall
f49  v=(+770,-42)  $10=$6000        impact: into-wall component REFLECTED
f50+ v=(0,+4096)   $10=$7000        ~8 frames of a fixed $1000 knockback
f58  v=(-106,-760) $10=$2000        state clears, normal driving resumes
```

Speed (`$EA`) is preserved through the whole event. Flags: `$4000` marks
the impact, `$1000` the knockback phase. Notably this wall's surface byte
is `$80` (the bit-7 "special" class), not the `$20` solid bit — the classes
respond differently and only this one is measured.

Ported to `smk_kart_move()` as: reflect the blocked component, then an
8-frame `$1000` kick away from the wall with speed kept — replacing the
old refuse-and-slide placeholder (ledger S6 upgraded from invented to
measured-shape). AI lap completion moved 6/20 → 7/20.

**Still open on the same thread:** the remaining stuck tracks are the jump
segments (no Z axis) and courses where the AI needs behaviour we have not
measured; the per-class wall differences; and the knockback direction rule
(observed along one axis, our port picks the blocked axis).

---

**045** — The Z axis, decoded exactly. And NOTES 044's "magic constant"
explained away.

**Finding it.** Two copy routines (`$80E6E0`, `$80FD9D`) move `$16/$18`,
`$1A/$1C`, `$1E/$20` between blocks as one group — X, Y and **Z**, all the
same 3-word shape. `$80FDBC` then clamps `$1F/$20` to zero when the value
goes negative: a ground clamp. An empirical sweep for ballistic signatures
in 1800 frames of demo racing found *nothing*, because track 7 has no
jumps — the useful move was to inject height into the running game and let
its own code integrate it.

**The law, from the ROM's only `sbc #$001A`** (`$80B1D6` — a unique
instruction, so the identification is not a guess):

```
lda $26,x / sec / sbc #$001A / sta $26,x   velocity -= 26
clc / adc $1F,x                            height word += velocity
bpl still-airborne
stz $1F,x / stz $26,x                      landed: clear both
lda $E2,x / and #$7FFF / sta $E2,x         clear the airborne flag
```

Z is a 24-bit value at `$1E..$20`; adding the velocity to the *word at
`$1F`* is `z += zvel << 8`, and the landing test is that word's sign.
Pixel height is `z >> 16`. Verified frame by frame against the game: launch
`$0080` peaks at 0.99 px and lands on frame 8; `$0180` peaks at 10.34 px
(z = 677632) and lands on frame 29. Both are now pinned in the selftest.

A second mode at `$80DFED` uses gravity **18** (`$0012`) instead of 26.
Ramp launches read their velocity straight from the DSP-1 (`$80B7D6`:
`lda $006000` → `$26,x`, then `$E2 |= $8000`) — another reason the DSP-1
had to be right first.

**NOTES 044 corrected.** `$80F8C0` sets `$42,x = $8000` **and
`$26,x = $0080`** — a wall hit *launches the kart*, using the same velocity
as a hop. So the "8-frame knockback" measured there was never a constant:
it is exactly the ballistic flight time of velocity 128 under gravity 26.
The port now expresses it that way, and the invented `BOUNCE_FRAMES`
constant is gone.

**Effect.** AI lap completion went 5/20 → **10/20** once flight ignored
solid cells and bounces resolved ballistically — several of the rest are
now near-misses (44/46, 30/35, 24/29 sectors) rather than hard stops.

Still labelled as inferred: that flight skips the solid check. A gate
exists (`$80F897`: `bit $12,x / bpl` skips the whole collision routine) but
which bit it tests is not pinned; jumps cannot work without it.

---

**046** — Camera lag: a bounded negative, not an answer. Plus where the
surface effects live.

I set out to measure how far the ROM's camera yaw lags the kart's heading,
because that lag is the input to the sprite frame rule (NOTES 041). Four
searches, all negative:

* **Not a DSP-1 parameter.** Both of `$02`'s angles are *constant* through
  a whole race — `Aas` = 192, `Azs` = 13312 (495 frames, one distinct value
  each). The camera does not yaw through the DSP.
* **Not a global.** Correlating every word of `$0000-$07FF` against all
  eight kart headings at eight lags (900 frames) found nothing above
  R = 0.97.
* **Not in the `$04` stream.** The sin/cos inputs during a race are exactly
  the eight kart headings — that is `smk_kart_face`, not a camera.
* **Not visible at the PPU.** Only 1 frame in 900 writes `$211B` directly;
  during racing the Mode 7 matrix arrives by **HDMA**, which the oracle
  does not model. Racing also issues *no* `$0A` raster calls at all — the
  raster command is a boot/menu path, and the in-race matrix is built on
  the CPU.

So the honest position: measuring the camera requires modelling HDMA first,
and that is the prerequisite for this item rather than a detail of it. Our
native camera keeps yaw = kart heading with no lag; that remains an
**assumption**, now explicitly bounded rather than vaguely open. The
player's turning lean stays synthesised from steering input, as labelled in
`frame_for()`.

Also located, for whoever picks up surface handling: the per-class surface
dispatch is at **`$80E09D`** (`lda $68,x`, special-cases class `$4C`, then
`jmp ($0000,x)` through the pointer table at `$80E0B4`), with a second
entry at `$80E1D2`. Not decoded.

---

**047** — HDMA modelled. The camera is still not measured, but the reason
has changed — and NOTES 046's framing was half wrong.

**HDMA now works** (`tools/smktool/cpu.py`): per-scanline transfers with
repeat and indirect entries, driven from the frame loop, plus `$211B-$211E`
decoded as write-twice 8.8 latches. What it revealed about SMK's Mode 7:

* **Four separate HDMA channels**, one per matrix register — ch1 → `$211B`,
  ch2 → `$211C`, ch3 → `$211D`, ch4 → `$211E` — all `dmap = $42`: mode 2
  (two bytes to the same register) with the **indirect** bit set, tables in
  bank `$00`, data in bank `$7E`. That is 4 × 2 × 170 = **1360 matrix
  writes per frame**, and the model reproduces exactly that count.
* Per-line variation is real and correct (`$0F00` at line 0, `$0B80` by
  line 24) — the perspective ramp is being transferred properly.

**Why the camera still is not measured.** *(Revised — see 049. The
"parked kart" explanation below was wrong.)* In the states sampled, slots 0
and 1 read speed 0 while 2-7 raced, and the matrix was constant:
A = B = C = D = 2944 across 900 frames.

The inference drawn here — that the camera follows a parked kart — did not
survive the next experiment. See 049.

Our native `yaw = kart heading, no lag` stands unchanged and still
labelled as an assumption.

---

**048** — Surface speed modifiers: two failed measurements, and what the
next attempt should do differently.

**Attempt 1, free-running demo.** Sampled surface byte (`$68,x`) against
speed for 1500 frames. Confounded: the means were dominated by the two
parked karts (surface `$00`, n = 7800), and the racing karts almost never
change surface — only **one** transition pair reached six samples, and its
median speed change was zero. Free-running AI karts stay on the road, which
is exactly what makes them useless for this measurement.

**Attempt 2, controlled probe.** Found a world position for each surface
class on the track and pinned a kart there at rest, expecting it to
accelerate to that surface's terminal speed. Speed stayed **0 for all 150
frames on every class** — writing position and velocity every frame
suppresses the kart's own update, so the probe measured nothing at all.
(The Z-axis probe worked because it wrote *one* field and let the game run;
this one wrote four and froze the object.)

Useful by-product: track 7 carries only **three** surface classes
(`$26`, `$40`, `$54`), so it is a poor track for this measurement anyway.
Theme 3 has eleven and theme 0 has fourteen — pick one of those.

**What to do next:** teleport once and let the kart drive freely, sampling
only while it stays on the target class; or drive the acceleration handler
directly through `Oracle.call()` with the surface byte set, which sidesteps
the need for a reachable game state entirely. The dispatch to decode is at
`$80E09D` (table at `$80E0B4`, second level of pointers).

---

**049** — `$B4` is not the player kart. Correcting 047, and the sprite lean
explained from outside.

Two corrections, both prompted by the observation that SMK pins the
player's kart to a fixed screen position and moves the world underneath it,
the kart only leaning, hopping and spinning in place.

**1. `$B4` is the current-object pointer, not a player pointer.** Logging
it every frame gives eleven distinct values in one race — `$1000` through
`$1700`, plus `$1840`, `$18C0` and `$1C00`. It is the `this` register that
every handler reads (`ldx $B4`), reloaded as the game walks its object
list, so its value depends entirely on *when* in the frame you sample. The
earlier reading of `$18C0` was not a mystery and `$1100` was not evidence
that the player is slot 1. Any conclusion of the form "`$B4` says the
player is X" is unsound.

**2. So 047's explanation of the constant matrix was wrong.** The matrix is
still constant when correlated against the object `$B4` names, so "the
camera follows a parked kart" was never the reason. What remains true is
only the measurement: A = B = C = D = 2944 at a fixed scanline across 900
frames, while per-line variation within a frame is correct. The live
suspect is now our **IRQ/HDMA interleaving** — SMK builds its per-scanline
matrix in a scanline IRQ, and our frame loop runs a line's HDMA before that
line's instructions, so an HDMA read can precede the write that fills it.
That is a model-ordering question, testable directly.

**What the thread does not block.** The reason to want the camera angle was
the player kart's turning lean, which I had assumed came from camera lag.
It does not: the lean is a **sprite animation** on a kart that is always
drawn from directly behind. So camera yaw = kart heading with no lag, and
`frame_for()`'s steering-driven lean is the right model — the assumption
flagged in 046 is now explained rather than merely bounded. Sheet frames
32-47 are the hit/spin-out rotation, cycled over time rather than selected
by heading (NOTES 040 guessed at their content and can now be stated).

**Audit of what depended on the bad assumption:** nothing decoded. The
sprite frame rule (041) force-spun a chosen kart against a constant
reference; the AI controller (043) measured karts 2-7 as AI regardless of
which is the player; the kinematics verification compares all eight slots.
The slot confusion was confined to this camera thread.

---

**050** — The physics verification was testing the wrong object. Now fixed,
and the result is finally real: **1428 exact, 0 differ** across six driving
karts.

The chain, in order:

1. After the register-census fixes, `make verify-physics` FAILED (119
   exact / 119 differ, constant 2.0 px error). First instinct — my new
   hardware code broke something — was wrong: an A/B bisect with each fix
   disabled, and with **all three** disabled, failed identically. The
   regression predated the day's work.
2. The real faults were in the verifier itself, compounding:
   * It forced race mode by writing `$32`, which reaches a race **scene**
     where every kart sits at speed 0 forever — no start signal is given.
     (In that scripted scene the game also writes 7 into every kart's
     `$20,x` via `$80B239` under flag `$2000`; in a real race driving karts
     read 0 there, so NOTES 045's field layout stands.)
   * It then chose its "kart" with `w($B4)` — the current-object pointer
     (NOTES 049). It verified the motion of whatever object the game
     happened to be processing. That object obeyed `pos += v<<8`, so the
     check *passed* — for years of session time, for the wrong reason.
   * 150 settle frames landed inside the countdown, where positions are
     scripted, producing the constant "+2 px with zero velocity" residue
     that finally exposed it.
3. The rewrite: reach the game's own attract-demo race (karts genuinely
   drive), sample **all eight** kart slots by address, compare only
   kart-frames in free motion — moving, grounded, velocity consistent with
   the kart's own heading per `$80F8CF` — and report skipped frames.  A
   window with no freely-driving kart is INCONCLUSIVE, not a pass.

Result on the corrected machine: karts 2-7 each 238/238 exact, worst error
0.0000 px; karts 0-1 (parked in the demo) correctly skipped. The
`position += velocity << 8` rule is now verified **on karts**, which the
old "478 predictions, 0 mismatches" never actually established.

Method note for the skill: a verification harness is subject to the same
model-blindness as the machine itself. This one had unstated preconditions
(kart is driving, pointer names a kart) that silently stopped holding.
The INCONCLUSIVE outcome — refusing to report success when the
preconditions fail — is what was missing.

---

**051** — Two decoded rules end the AI loops; and a lesson about surrogate
metrics.

**The loops diagnosed.** The "stuck" tracks were never frozen: the karts
drive at full speed forever, with `sector < best` — bounced backward into
an earlier sector's paint, they aim at that sector's successor, hit the
same wall, and orbit. Two decoded facts fix it:

1. **Sector acceptance** (`$808962-$808983`): a new sector is accepted
   unless the cell reads `$7F` (off-course - keep the old sector) or the
   kart is **airborne and the new sector's waypoint attribute has bit 7
   set**. That is what attr bit 7 means: do not capture progress from this
   sector while flying - the anti-shortcut rule for jump zones. The
   accepted sector is stored to both `$DC,x` and `$C0,x` (the copy the
   target-speed lookup reads).
2. **The bounce runs along the wall, not backward.** NOTES 044's measured
   knockback - approach `-X` into a wall, knockback `(0, +$1000)` - is
   *tangential*, perpendicular to the approach. My port reflected the full
   velocity instead, which drove karts ~144 px back down the track during
   the ballistic flight and seeded every loop. Now: the into-wall component
   is killed and the kart slides along the tangent at the measured `$1000`.

**Effect.** Lap completion under the loose criterion went **10/20 → 16/20**.

**The surrogate-metric lesson.** Tightening the harness's lap test to
require per-lap sector coverage exposed that two "laps" were shortcut
artifacts - and that several karts legitimately cover only ~85% of sectors
per lap (jump sectors are flown over, per rule 1 above; the finish strip on
some tracks lies wholly in the LAST sector so a `sector<=1` crossing test
never fires there). Chasing a threshold that makes the surrogate agree with
the eye is the wrong game: the honest numbers are **16/20 circulating and
crossing repeatedly** and **10/20 under strict per-lap coverage**, and the
correct fix is decoding the ROM's own crossing routine - `$808994`, called
exactly when a finish-strip cell is accepted - instead of tuning a
surrogate. That is the next lap/checkpoint item.

---

**052** — The lap system decoded (`$808994`), and the parked-kart mystery
finally closed for real.

**The crossing routine**, called exactly when a finish-strip cell is
accepted:

* The **lap is the high byte of `$C0,x`**: a forward crossing does
  `adc #$0100 / and #$FF00` — lap +1, sector byte cleared; a backward
  crossing (`$8089ED`) does `sbc #$0100`. So `$C0,x` is a single progress
  word, `(lap << 8) | sector`.
* **`$F8,x` is the monotonic guard**: the new progress must exceed it to
  count (`cmp $F8,x / bcc,beq skip / sta $F8,x`). That is what prevents
  double-counting and line-farming — no coverage heuristics anywhere.
* Crossing **direction** comes from comparing the kart's cell against the
  per-track word `$014A` (params table `$81:80D4`); the race-over test is
  `sbc $014C` against the total-laps value.
* Flag `$04` in `$10,x` marks the crossing; the final-lap path sets
  `$0100` in `$D4,x` and calls `$8A89` (finish handling).

Ported: racer and player progress now use the decoded shape — lap ±1 on
sector wrap with the monotonic guard — replacing both of the harness's
surrogate criteria (NOTES 051's "best > half" and the coverage
percentage).

**And the bonus that closes NOTES 047/049:** `$808A03` does `cpx #$1100` —
the routine special-cases kart blocks `$1000` and `$1100`. **Slots 0-1 are
the two PLAYER karts; slots 2-7 are the AI.** The "parked pair" in every
demo measurement was the two players waiting for input that never comes in
attract mode. Not a model bug, not a scripted scene: just players with no
controller. (Why the real attract demo drives them — recorded input
playback — is still unexamined; our forced `$4218 = 0` may be overriding
it. That is the remaining camera-measurement blocker, now precisely
located.)

---

**053** — A driving player kart in the oracle, at last. The gate was four
state words — and one wrong button.

The full story of why every control attempt failed:

* **Wrong button all along.** SMK accelerates with **B**, which is
  `$4219` bit 7. Every earlier attempt held `$4218` bit 7 — that is **A**,
  the item button. (The joypad byte layout: `$4218` = A/X/L/R,
  `$4219` = B/Y/Select/Start/dpad.)
* **B alone is not enough.** In the demo, clearing `$0E32` and holding B
  still left P1 parked: the kart's *state machine* is not in the driving
  state, and the demo flag does not reset it.
* **The gate is four kart-block words.** Diffing P1 against a driving AI
  kart: `$10` = `$8000` vs `$2000`, `$12` = 0 vs `$0002`, `$C4` = 0 vs
  `$8000`, `$C8` = 0 vs `$0010`. Copying the AI's values (plus B held)
  had P1 accelerating within 30 frames. Bonus confirmations: `$C8 = $10`
  is exactly the per-kart row offset measured in NOTES 043 (bytes → row
  +8 words), and `$12` bit 1 relates to the collision gate `bit $12,x`
  at `$80F897`.

Also from this stretch: the forced race scene ($32 write) runs Lakitu-style
placement (position glides to the grid with speed 0) but never a start
signal, and injected speed is zeroed by the state machine within 1-3
frames — so the forced scene cannot measure surface behaviour without the
state constants (test of that pending).

This unblocks, in order: per-surface speed caps measured with a driven
kart, the in-race camera matrix (finally a moving camera), and the
drift/hop decode.

---

**054** — The camera thread, closed with a final precise negative.

With a driven, turning player (335 distinct headings, speed 709), the
Mode 7 matrix at every sampled scanline **still does not vary**. Forcing
the kart's state words wakes the kart's physics but not the camera update,
which evidently lives in the player-handler dispatch that stays dormant in
attract mode. So the camera's dynamics remain unmeasured, and the thread
is closed rather than continued:

* Every gameplay-relevant camera fact is already established — yaw = kart
  heading (the kart's lean is a sprite animation, per the user's own
  description of the original), and `$02` fixes Lfe = Les = 256,
  Azs = 73°.
* What is not established is only whether the ROM smooths yaw over a few
  frames, and that cannot be measured until either the demo's recorded
  input reaches the players or the full player handler is woken.  Neither
  is worth the cost while it blocks nothing.

Four attempts, four different failure reasons, all recorded (046, 047,
049, here). If someone resumes this: wake the player DISPATCH, not just
the kart state - the difference is exactly what this measurement exposed.

---

**055** — Walls made sticky; the NOTES-044 fling reattributed; and an AI
regression taken knowingly.

The playtest report "hit a corner and bounced forever" unravelled a chain:

* **The measured fling belongs to bit-7 special surfaces.** NOTES 044's
  launch + `$1000` along-wall knockback was captured on a class-`$80`
  cell.  Nothing was ever measured for plain `$20` walls, and porting the
  fling to them produced the ping-pong: held throttle refills speed
  between bounces faster than any damping drains it, so it never settles.
* **Plain walls are now sticky** (labelled feel model, not a decode): the
  into-wall velocity component dies, speed scrubs in proportion to the
  blocked share (a graze loses little, a head-on nearly stops), corners
  stop the kart.  A synthetic held-into-the-corner test verifies **zero
  direction reversals** - scrape and clear, no ping-pong.  The fling still
  applies where it was measured: `$80`-class surfaces, once per contact.
* **A real lap-counter bug surfaced on the way**: the strip holds paint of
  both ends of the loop, so one transit could fire +1 then an unguarded
  -1 and lock the counter against the monotonic guard forever.  Lap events
  are now one-per-transit (90-frame cooldown).

**The cost, stated plainly:** the AI relied on the fling to escape walls
its cornering drives it into.  With sticky walls the strict-lap score
drops 14 → 6/20; karts complete lap 1 and stall in corners on lap 2, and
neither realign-probing nor waypoint lookahead recovers them.  Player feel
wins this trade - the walls are for the person holding the pad - and the
honest fix for the AI is following the ROM's line tightly enough not to
hit walls, which is a decode item (the AI's real cornering inputs), not a
heuristic to tune.  Recorded as the top P6 open.

---

**056** — The real AI cornering decoded: a flow field at `$7F:4000`.
The stalls' root cause, and the end of waypoint-chasing.

The path there mattered as much as the answer:

1. `$FA` (the AI target angle) turned out to have its **low byte always
   zero** - a 256-step quantized direction.
2. It matches **no waypoint bearing** (86% of frames miss by >8°) and no
   segment tangent either.  Both aiming models we had were wrong.
3. Grouping by map cell: **one `$FA` value per cell** (84% of cells) - a
   per-cell direction field.  A WRAM scan for the table failed - because it
   required ≤16 distinct values, and the real field is fine-grained.
4. The reader is explicit at `$80AD62` / `$80B0B1`: **on course,
   `$FA = byte[$7F:4000 + (py/16)*64 + px/16] << 8`** (a 16-bit read at
   `$7F:3FFF+idx` whose `and #$FF00` keeps exactly the table byte).
   `atan2(waypoint - pos)` is only the **off-course recovery branch**
   (flags `$10` bits 0-1).  Treating it as the main rule was why our karts
   clipped corners into walls.
5. The builder at `$81FCFC`:  for every on-course cell,
   **`flow[cell] = high byte of atan2(waypoint[sector_of_cell] - cell
   centre)`** - each cell aims at its own sector's waypoint, precomputed at
   load.  Since waypoints sit at sector exits on the racing line, the field
   never points into a wall.  (The loop above it fills the odd bytes of the
   `$0800` attr table with per-waypoint direction bytes.)
6. A wrinkle: my first live comparison of `flow[cell]<<8` vs `$FA` used
   angle data captured **before the CPU-divide fix** - on that machine the
   atan2 could only produce cardinals, which is why the field first looked
   4-valued.  Stale captures lie; re-measure after machine fixes.

**Verification:** our reimplemented builder matches the game's 4096-byte
field **95.2% byte-exact, 100% within ±1 step (±1.4°)** - the residual is
the ROM's table-atan2 rounding at step boundaries.  Live, `$FA` equals the
field byte in 62% of frames with the rest within a few degrees (the
`$80ABxx` incremental adjusters add small per-frame offsets on top - not
yet decoded).

**Effect:** with sticky walls AND class-fair AI speeds (target row +0, so
50/100/150cc scale player and AI together - the attract demo's row +4 was
outrunning the player at every class), strict laps went **6 → 14/20**, now
achieved without the fling crutch.  Chronic failures 1, 8, 14, 18 lap
cleanly.  Remaining: 3, 9, 11, 15, 16, 17 - mostly jump tracks and
off-course fallback cases.

---

**057** — 20/20: every GP track lapped by the AI under the strict rule.
The last six tracks, and what each one actually needed.

Starting point after the flow-field port: 14/20. The remaining six fell to
four distinct causes, each found by instrumting the end-state rather than
tuning blind:

* **Jump gaps** (3, 9, 11, 17): stalls sat at `$22/$24` solid fields - the
  gap pits the real game vaults.  These are jumpable barriers (the landing
  code remaps `$22` → `$4C` at touchdown, `$80B1F2`); hitting one at speed
  now launches off the ramp edge (placeholder velocity, labelled).  Four
  tracks cleared by one rule.
* **High-speed wedges** (4, 8, 9, 17 early exits): a kart pinned square
  against a wall KEEPS its speed - the proportional graze loss is ~0 and
  the position only crawls sub-pixel - so the low-speed escape trigger
  never fired.  Stagnation (40 identical positions) is the reliable
  trigger.  Also: the escape's open-ground scan sampled from 8 px and was
  blind to walls 1-7 px away, and karts wedged in one-pixel concave
  notches needed a physical 3 px nudge (labelled last resort).
* **Deep pockets** (8, 16): karts that get off-line can enter paint whose
  own waypoint lies across a wall; the flow field then pins them.
  Escalating escape lengths (25→120 frames) were still not enough alone.
* **Adjacent-sector oscillation** (15, and 8's remnant): the final pair
  circled between two sectors for minutes, resetting every
  sector-change-based timer.  The fix is the game's own answer:
  **Lakitu**.  Ten seconds without *monotonic* progress - keyed on the
  max of `(lap<<8)|sector`, which oscillation cannot reset - sets the kart
  down at its sector's waypoint facing the next.  The rescue is the real
  game's behaviour; our trigger and the missing animation are labelled.

Result: 20/20 strict laps, times 19-74 s, both suites green.  The
remaining honest gaps in the AI: ramp-launch velocity is a placeholder,
the `$80ABxx` lane-offset adjusters are undecoded, there is no
rubber-banding, and Lakitu is a teleport without his animation.

---

**058** — Playtest round two: barriers, off-road, and grip.

Three reports, three corrections:

* **"Barriers are passable."**  My own NOTES-057 ramp rule was the bug: it
  vaulted ANY type-1/2 solid, but `$22` is Ghost Valley's RAILS - the
  `$22 → $4C` landing remap exists for feather jumps OVER them, not for
  driving through.  Restructured to the decoded shape: the bit-7 classes
  (`$80/$82/$84`) are the JUMP BARS, and driving onto one launches (the
  class-$80 response measured in NOTES 044 - which also finally explains
  what that measurement was: a jump bar, not a wall).  All solids stay
  sticky; gaps without a bar stop you at the edge.  AI still 20/20 (bars +
  Lakitu cover the gap tracks).
* **"Off-road doesn't slow me."**  True: the placeholder caps sat just
  under the 50cc top speed (grass 640 vs top 672 - a 5% drop).  Lowered to
  a felt range (384..224 by type), still labelled placeholders pending a
  driven-kart measurement.
* **"Drifting is absent - 100% grip."**  Correct: `smk_kart_face()` snaps
  velocity to the heading every frame, which IS full grip.  The player now
  blends velocity toward the facing direction: full grip at low speed,
  slight slip above 550, strong slide while the hop button is held, and
  near-ballistic mid-hop.  Space hops (the decoded $80B69D launch, zvel
  `$0080`, needs speed) and hop-into-a-held-turn power slides.  Grip
  constants are labelled placeholders; the ROM's drift state machine
  ($E2 bits) remains the honest decode target.

---

**059** — Per-track surface feel: how the ROM actually composes it.
(Decode complete; measurement in progress.)

The user's request - "different tracks have different friction; read it
from the ROM, not a guessed common metric" - led through the surface
plumbing.  What the code says:

* **There is no per-track friction table.**  Per-track feel is composed:
  each THEME assigns surface CLASSES to its tiles (the class array is
  copied live to WRAM `$0B00`, read per tile - `bit $0AFF,x` tests the
  jump-bar bit), and one GLOBAL set of per-type tables gives each class
  its behaviour.  Ice feel exists because the ice theme's *road* is class
  `$4C/$4E` (types 6-7) where Mario Circuit's is `$40` (type 0) - same
  tables, different assignment.  Our loader already reads the class arrays
  from ROM, so per-track feel falls out once per-TYPE behaviour is right.
* **The slide-energy machine** (`$80B12F-$80B180`): `$C2,x` charges toward
  cap `$0E20` at rates `$0E22/$0E24`, decays at `$0E26-$0E2A`, gated by
  surface type < 10 and state bit `$E2.2`; thresholds `$2000/$2DC0/$2E80/
  $3000/$30C0` flip drift-state bits in `$E0/$E2`, and the hop path drains
  `$70` per frame above `$2000`.  The six parameters load from
  **`$81:EFE7`** - two sets (cap `$3FFF` rates `$120/$80` vs cap `$5FFF`
  rates `$200/$40`), selected by `$0030` - per MODE, not per track.
* Known per-type tables so far: coasting drag `$80A590`, over-cap decel
  `$80A65D` (both ported); the per-type grip/handling is being MEASURED
  from the running game across themes rather than guessed.

Process trap, re-hit: the first grip sweep returned bit-identical results
on seven "different tracks" because it forced `$0124` - which mode entry
recomputes from `$0150/$0152` (`$81EC47`: `$0124 = map[$0150*5+$0152]`,
map at `$81EC1B`, store at `$81EC5D`).  NOTES 028 documented this trap;
I walked into it again.  Negative results are load-bearing - reread them
before reusing a state-forcing trick.  The sweep now sets cup/course and
tags results by the track that ACTUALLY loaded.

---

**060** — The 16-type surface system, and per-track feel shipped the way
the ROM composes it.

The key correction: surface TYPES are the class low nibble - **(s>>1) &
$0F, sixteen types** - and `$80A65D` is ONE 16-entry per-type decel table,
not two rows of eight.  Its "second row" is types 8-15, where the ice-theme
road classes `$56/$58` (types 11/12) sit at only -12/-28: the ROM's own
numbers mark ice as near-frictionless.  Our old `&7` fold aliased types
8-15 onto 0-7, which is why every theme felt the same.

Now in the game: type extraction fixed to 16 types; drag/decel tables
extended accordingly (types 8-15 drag mirrors the decel ratios, labelled);
caps keyed by 16 types with ice roads uncapped; and the player's grip is a
16-type table - road 1.0, ice 0.35/0.30, off-road 0.65-0.80 - so Vanilla
Lake slides while Mario Circuit bites, composed per track exactly as the
ROM does it: theme class array (read from ROM) x per-type behaviour.

Still labelled as ours: the grip VALUES (the honest source is the
`$AA`/`$C2` slip machine - slip angle with thresholds $0C00/$1800 flipping
`$E2` drift bits, decay from the record table at `$80AC38+` whose index
derivation is still undecoded) and the caps.  Measurement via a driven
kart stays blocked on the dormant input-steering dispatch (the woken kart
accelerates but ignores the d-pad, so turn/frame reads zero).

---

**061** — The player-input steering gate: hunted, not found.  Thread
parked with exact coordinates.

Chasing the dormant input-steering (the blocker for measured grip, drift
states, and the camera), the dispatch at `$80A354/$80A3B7` decodes as:
per kart, `$10,x` == 0 → inactive; bit 15 CLEAR → `$80AD8E`/`$80AD5E`
(the AI/flow paths - what our forced state runs, which is why the woken
kart accelerates but ignores the d-pad); bit 15 SET → `$809E29` +
`$80B112`.

But `$809E29` is NOT input handling: it is the **wrong-way detector** -
flow-field direction minus heading, thresholds `$3000/$7000`, gated on
submode `$2C < 6`, `$10` bit `$0400` selecting the strict variant.  With
bit 15 set and no other life, the kart simply parks (verified: accel 0,
turn 0 under held B+Left).

So the input→steering translation is NOT selected by `$10` bit 15 and
remains unlocated.  Candidates for whoever resumes: the `cpx #$1100`
pattern (`$808A03`) suggests player-kart special-casing by BLOCK ADDRESS,
not flags; and the countdown/start state (`$2C`, `$0E50`) gates several
paths.  Until the real handler wakes (or the menu walk reaches an actual
race), per-type grip values stay labelled placeholders.

---

**062** — Playtest: dust still inert, grip still total.  Both were OUR
wiring, and the cap-read trap came back empty.

* The `$80A707` cap read **never executes during demo racing** (400 trap
  attempts, zero hits) - that whole path belongs to some other state, so
  neither my "physics-blob row" theory nor the earlier scratch-table
  reading describes the live off-road mechanism.  Still undecoded.
* Meanwhile the felt bugs were in our own 16-type tables: types 9/10 -
  Mario Circuit's dust `$52/$54` - were classified road-like (uncapped,
  grip 0.95/0.90) from the ice-theme reasoning.  Dust now caps at 360/330
  with grip 0.70/0.65.  Only true roads (0, 1, 8) and ice roads (11, 12)
  run free.
* Grip convergence was too fast to see: 0.35/frame aligns velocity with
  heading in ~3 frames.  Now 0.03-0.14 at speed (visible wide-running),
  0.15-0.50 mid-speed, full grip below 300, plus a breakaway: past ~20
  degrees of slip the convergence halves, so oversteering actually slides.

All feel constants remain labelled ours.  The honest sources stay the
same two undecoded pieces: whatever applies off-road physics live (not
`$80A707`), and the `$AA/$C2` slip machine.

---

**063** — Three playtest bugs, and the reason the last two rounds changed
nothing: my surface-cap edits had silently failed.

* **The silent edit.**  `smk_surface_cap` was still the ORIGINAL 8-entry
  table - indexed by the new 16-type values, i.e. reading past the array -
  because two successive patches used `str.replace` against a stale
  pattern with no assertion: both no-opped, and I shipped, announced, and
  the user tested builds that never contained the change.  The fix landed
  only after adding asserts AND verifying the rebuilt binary's live output
  (a class histogram now prints type and cap straight from the library).
  Rule going forward: every scripted source edit asserts its match, and
  behavioural claims are checked against the built artifact.
* **Diagonal tunneling** ("cannot hit barriers - I go inside and get
  stuck"): the mover tested the X-step and Y-step cells but never the
  diagonal destination, so a fast kart slips between two solid cells into
  the interior where everything blocks.  The diagonal is now tested, and a
  kart already embedded is allowed to move out.
* **The infinite plane**: the ROM's world is one 1024x1024 plane - beyond
  it, `$80FAAE` sets the off-course flag and clamps.  Our lookup WRAPPED
  coordinates, tiling the plane forever.  Outside is now solid wall.
* Track 0 ground truth (from the new histogram): road `$40`, dust
  `$26`(type 3) and `$54`(type 10) with a thin `$52`, plus a `$00` VOID
  band between dust and the `$20` barriers - the "area you get stuck in".
  `$00` now crawls (cap 160, grip 0.45) pending its real semantics.

---

**064** — Track objects decoded and shipped: item boxes, pipes, coins.

The chain, each step verified live:

* The collector at `$81B797` (tile-pair swap when a kart touches an
  object) was the known anchor; the PLACER is the stamp blitter at
  `$84F1A4-$84F235`: per object, copy a w x h tile stamp (sizes from
  `$84F384`, graphics via the pointer table at `$84F23D`, staged through
  WRAM `$1800`) into the tilemap, `$FF` bytes transparent.
* Object records are `[kind][cell:word]`, `$FFFF`-terminated; cell packs
  x/8 + (y/8)*128; kind bits 0-5 = stamp graphic, bits 6-7 = size class.
* **The per-track list needs no index table**: `$84F15D` computes
  `list = $85:D000 + track*128` (battle mode: `$30:7000`).  Track 7 →
  `$D380`, byte-identical to the live capture; its 21 records match the
  count observed running.
* Bank `$81:B470` holds a separate structure - per-track lists of stamp
  GRAPHIC ids to install (records delimited by their own pointer table) -
  used to load the right tile graphics; not needed for object data.
* Two capture-method lessons: `run_to` cannot cross frames (the main loop
  spins waiting for an NMI only the frame-stepper delivers), and our bus
  write-log records the PC AFTER the store instruction - a PC-keyed hook
  must use the post-instruction address.

Ported: `objects()` in course.py and the `obj[]` array in
`smk_course_load`, selftest-pinned to the live capture; the renderer draws
objects at their true positions (PLACEHOLDER visuals - gold blocks for
item boxes, green for big stamps - until the animated stamp tile graphics
are decoded).

---

**065** — FULL PLAYER CONTROL in the oracle.  The gate, after everything:
the demo flag is consumed at RACE SETUP, not per frame.

The winning move: a bus hook that makes every read of `$0E32/$0E33`
return zero, installed **from boot** - the attract flow still launches its
race, but the setup path configures player 1 as a real player.  Result:
P1 accelerates under B within 30 frames and steers at the ROM's own rate
(-37.1 degrees over 45 frames of held Left).

Why every earlier attempt failed, in one line each: wrong button (A vs B);
kart state forced into the AI dispatch (drives, ignores pads); `$10`
bit 15 routes to the wrong-way checker, not input; `$0E50` and mid-race
`$0E32` masking change nothing because the DEMO configuration is baked in
when the race is set up; pad presses in attract are consumed as the
demo-exit trigger.

Unblocked and running: the full calibration battery - per-surface-class
terminal speed, deceleration curve, turn rate, and slip, measured on the
live player with the surface-table swap ($0B00) so the road itself becomes
each class in turn.  Next after that: drift-state capture (hop + held
turn) and per-engine-class scaling.

---

**066** — Longitudinal surface physics MEASURED from the ROM.  The
calibration the project owed.

Method (all on the live player from NOTES 065's un-demo hook):

1. Flow-steer the player through the real pad - bang-bang Left/Right
   toward the game's own `$7F:4000` direction byte - so it laps
   indefinitely.  (The unsteered first attempt crash-looped into walls
   and produced noise; the AI-karts route was already dead because the
   original's AI ignores surfaces - its rubber-band cheat, NOTES 057.)
2. Swap every driveable entry of the live WRAM surface table (`$0B00`) to
   class X: the whole road becomes X while the kart keeps lapping.
3. Record the terminal speed; slow classes re-measured behind a recovery
   gate (pace ≥ 650 on the restored road first) after the first pass
   showed wreckage contaminating successive rows.

Results (fraction of road `$40` = 951): `$42` .81, `$44` .92, `$46` .94,
`$48` .97, `$4A` .89, `$4C` .88, `$4E` .94, `$50` .85, `$52` .66,
**`$54` .615** (Mario Circuit dust - a firm drag, not a crawl; my
placeholder was too harsh), `$56` .30 (the true heavy class), `$58` .57,
`$5A` .60, `$5C` .60, `$5E` .68, **`$26` 0.00** (full-stop hazard -
deep water).  Entry curves are clean monotone decays (e.g. `$54`:
788→700→620→580 plateau).

Ported as `smk_surface_cap_frac()` - thousandths of road speed, scaled by
the engine class's own top at runtime so 50/100/150cc keep the ROM's
ratios.  Player uses it directly; the AI gets a softened version
(labelled: the real AI ignores surfaces, ours stays honest but
competitive).  Unmeasured residuals, marked: the void band `$00` (crawl
guess), classes absent from the demo theme (nibble-neighbour fallback),
and lateral grip - the next measurement on the same rig.

And Step 23 bit within the hour of writing it: the first port spliced
physics.c mid-function, the library failed to build, and the suites
"passed" against the stale binary - caught only because the histogram
tool still showed old caps.  Behavioural verification against the built
artifact is not optional even when you just wrote the rule.

---

**067** — The off-road bite calibrated too: measured deceleration rates.

Playtest report: leaving the track felt milder than the real game, and the
real behaviour is a firm deceleration down to the surface's max, not a
switch.  Correct on both counts: our cap was measured (NOTES 066) but the
RATE toward it still came from the `$80A65D` table - only 9 units/frame
for the dust type - while the calibration entry curves record the ROM's
actual rates: `$54` falls 788→700→620→580 at 5-frame spacing.

Measured per class (speed units per frame): `$54` 18, `$56` 22, `$58` 22,
`$5A` 16, `$5C` 16, `$5E` 22; `$50/$52` take the generic 18 (one curve
carried a crash artefact, the other decayed after the sample window);
`$26` collapses within frames (160).  Ported as `smk_surface_decel()`,
wired into the player's over-cap branch; verified empirically - 900 →
dust cap in the expected ~15 frames at 18/frame.

The `$80A65D` interpretation is hereby demoted: whatever that table is,
it is not the off-road deceleration.

---

**068** — Grip and drift MEASURED and ported.  And the drift state machine
is real after all.

Two battery runs on the calibration rig (short excursions, spin-abort,
teleport recovery after the first run's wreckage cascaded):

* **Steady cornering slip is ~200-310 units (~1.7 deg) on every class** -
  `$42` at 770: slip 310; `$54` at 585: slip 202; `$56` at 289: slip 203 -
  converging at ~0.5/frame.  Grip class-differences do NOT show in steady
  slip.
* **Breakaway is by lateral acceleration** (speed x turn rate): 950x307
  breaks away (slip grows ~130/frame, steering authority collapses to
  ~-20/frame - a progressive plow), while 770x307 and 585x307 hold.  The
  limit sits near 250k.  So SMK's surface CAPS are most of its grip
  system: capped surfaces cannot reach breakaway speeds.  Elegant.
* **The drift state machine is real** - correcting the previous session's
  "emergent-only" reading, which came from a 4-frame hop tap that never
  engaged it.  A 6-frame tap into a held turn walks `$E2` through
  `$8000` (hop) → `$8004` (slide) → `$8024` (slide + the charged-`$C2`
  state from `$80B0F5`), with `$C2` charging to ~12000.
* Slip recovers at ~150/frame below the limit; airborne the kart keeps
  its momentum (near-zero grip), which is what makes hop-into-turn START
  the slide.

Ported into `step_kart` as the measured model: 0.5/frame convergence,
lateral-acceleration breakaway at 250k, plow past it, held-slide grip
while hopping with slip present.  Ice types 11/12 keep a labelled
multiplier - absent from the demo theme, unmeasured.  All suites green,
AI 20/20, corner settles.

---

**069** — "Is that in my build?"  Yes - and it was inert at 50cc.

The measured breakaway limit (250k lateral) was captured at the demo's
speed scale (top ~951) and shipped as an ABSOLUTE.  At the default 50cc
(top 672) the maximum possible lateral force is 206k: the model was in the
build and mathematically unreachable - the playtest's "no difference" was
exactly right.  The limit now scales by the class top (breakaway at ~86%
of top under full lock, the measured ratio), so every class slides near
its own edge.  The hop-slide gate also dropped its slip precondition
(hop held + speed is enough - the state machine's own behaviour).

Two visibility fixes so this class of question answers itself in-game:
the window title carries the git build hash, and the speedometer gained a
SLIP readout in degrees (gray planted, orange sliding, red spinning).

---

**070** — Objects are two families; the pipes are sprites, not tiles.
(Playtest: "internal barriers are traspassable; objects are big squares.")

The probe settles the object system's shape:

* **Tiles 192-255 have surface classes** - the live table at `$0BC0`
  continues past 192: stamped item-box tiles are class `$14`, coins `$16`
  (both non-solid: you drive THROUGH them to collect - the `$81B797`
  collector fires on contact), plus `$40/$80/$10/$18/$1A` bands.
* **Stamp graphics are overlapping 4-byte-stride windows** into one tile
  ramp (`$C0..`), sized by kind bits 6-7.
* **The pipes are NOT ground tiles at all** - object kinds >= `$C0`
  (`$DC/$E4/$E8/$EC` on the pipe tracks) are SPRITE OBSTACLES, the extra
  object blocks ($1840/$18C0/$1C00 seen in `$B4` long ago).  They scale
  with distance like karts and carry their own collision - which is
  exactly the "internal barriers" a kart could drive through in our build,
  because they had no substance at all.

Ported: kind >= `$C0` objects now have cylinder collision (12 px, push-out
plus speed halving - sticky-style, labelled) for player AND AI, and render
as distance-scaled billboards; kinds < `$C0` render as small flat
ground-scaled decals.  Pixels for both remain placeholders - the object
graphics stream is the open decode (VRAM slots 192-255 DMA trace running).

---

**071** — The real wall crash, measured head-on and ported.  SMK walls
REBOUND at full speed.

Crash lab (the rig driving into a `$20` wall at 791):

* The bounce is a pure velocity ROTATION: in (0,+791), out (-644,-460) -
  magnitude exactly preserved (791 -> 791).  My "sticky walls" eyeball
  model (NOTES 055) is wrong for real walls and is now replaced.
* **`$42,x` is a countdown, not a flag**: $0A -> $01, a 10-frame ballistic
  window - no steering, no thrust - during which the velocity vector curves
  and the kart clears the wall.
* No vertical launch on plain walls (`$26` stays 0): the hop belongs to
  the bit-7 bars alone, closing that loop from NOTES 044/058.
* `$10` bits seen on contact: `$0400` while touching, `$4000` variant on
  the graze - the touching-wall flags.

Ported: solid contact reflects the blocked velocity component with the
magnitude kept, and `bounce_cool` now models the $42 window - 10 frames of
ballistic flight-out with face()/thrust suspended.  Bars keep their
measured launch.  All gates green (make check, AI 20/20, corner settles).

Open: the angle-dependence runs sampled at creep speed (the placement
zeroed velocity) - re-run with speed injection to see whether shallow
angles deflect rather than rebound.  Pipe (sprite-object) crash response
is the next lab target; sprite tier thresholds sweeping now.

---

**072** — The environment-and-sprites pass: pipe crash, player frames, and
the scaling truth.

* **Pipe crash, measured** (rig at 581 into a track-14 pipe): contact sets
  `$10` bit `$0002`, the velocity REFLECTS, speed scales to 308/581
  (~0.53), and a ~9-frame knockback window follows with the velocity
  frozen (`$AC = $16`, `$10 = $C000`) before control returns.  Ported into
  `collide_objects` - reflect, scale, 10-frame ballistic window.  The spin
  component remains unmeasured (the lab pinned the heading) - open.
* **Player frame mapping, measured** (nine scripted input phases logging
  sheet uploads): the driven kart uses exactly TWO rear-view frames -
  **1 (centred) and 47 (deep lean)** - flipped for direction.  Brief taps
  upload nothing (the frame does not change); the lean engages on
  sustained holds and slides and persists through release.  `frame_for`
  now implements that mapping, replacing the synthesized three-step lean.
  Which of frames 44-46 serve intermediate leans (if any) was not
  observed - only 1 and 47 ever uploaded.
* **Sprite scaling, measured**: the original NEVER scales continuously.
  The OAM canvas stays 32x32 (1/8 screen width) across the whole near/mid
  range - the depth sweep shows 32x32 at every bucket to 160+ - with
  apparent size stepping through the art tiers INSIDE the canvas, one
  16x16 switch far out, and a cull.  The renderer now draws constant-canvas
  karts with tier steps at 96/160 depth, the 16px switch at 224, cull at
  320 (thresholds labelled-estimated; the constant canvas is the measured
  part).  This replaces the continuous 1/depth shrink - the "funny" look.

---

**073** — Playtest round: the centred frame, the missing plow, and the
visible hop.

* **"Starts turning right"**: frame 1 - my "centred" pick from the
  rotation measurement - is visibly a turned pose; the straight rear view
  is **frame 2** (visual identification against the sheet).  The rotation
  rule's frame-1-at-centre (NOTES 041) numbers the AI rotation set, which
  is offset from the visual centre.  Player mapping now: 2 straight,
  1/hflip steering, 47/hflip slide.
* **"Slide detected but nothing happens"**: correct - I had ported the
  breakaway TRIGGER but not its consequences.  g=0.08 EQUILIBRATES against
  the turn rate at ~26 deg and the steering kept full authority, so the
  kart just cornered harder.  The measurement says: authority collapses
  (~307 -> ~20/frame) and slip GROWS unbounded.  Ported: past 4000 slip
  units the turn rate drops to 6% and g falls to 0.02.  Speed loss in a
  plow is NOT the OG behaviour (measured: 791 -> 801 held), so none is
  added.
* **Slide sprite**: >12 deg of slip or hop-slide now shows frame 47, so
  oversteer LOOKS different from steering.
* **The hop was invisible**: physics existed, the player sprite never
  lifted.  It now rises with kart height (shadow grounded), making
  hop-into-slide testable by eye.

---

**074** — Real object graphics: the full stamp chain decoded, ported, and
pinned live-exact.  And a surprise: the "pipe" kinds stamp COINS.

The chain (all from the ROM, no captures needed at runtime):

* Object tile pixels: `$81E6B9` decompresses the blob at **$C4:0000**
  (our own codec) to `$7F:0000`, then the expander at `$84E3C7` (already
  ported as `expand_tiles`) produces 64 8bpp Mode 7 tiles that DMA to
  VRAM `$3000` = tile slots 192-255.  Ported into `smk_track_load`:
  `tiles[]` now holds 256 tiles.
* The stamp blitter `$84F1A4` (hand-decoded this session): per record,
  `kind & $3F` indexes the pointer table `$84F23D` (overlapping windows
  into a tile sequence; entries 32+ step 25 bytes), `kind >> 6` picks
  (w,h) from `$84F384` = 2x2, 3x1, 1x3, 5x5; stamp bytes are tile
  indices written row-major into the TILEMAP at the record's cell
  (+128/row), `$FF` transparent.  **The blitter has no kind filter** -
  kinds >= $C0 stamp too.
* Ported as `smk_track_place_objects` - a separate step after
  `smk_track_load`, because tools/test.py cross-checks the loader against
  the game's own LOADER ($81E67A), which has not stamped either.
* Selftest pins track 7 against the live capture: tiles 196/199 x12
  (item boxes) and 254 x35 (37 stamped minus 2 overlaps) - EXACT.
* Surface classes for tiles 192-255: still the live-captured 64 bytes
  (item box $14, coin $16); the ROM code that fills WRAM $0BC0+ remains
  undecoded.

The surprise: rendering the stamped map shows the >= $C0 kinds lay down
**coin clusters** (sparse 5x5 scatters of tile $FE, unmistakably coins),
NOT pipe shadows.  On Mario Circuit 1 (track 7) the $DC/$E0/$EC records
are the coin groups on the road.  This BREAKS the NOTES 070 reading that
kinds >= $C0 are pipe obstacles: our billboards + solid cylinders at
those positions are wrong at least for coin tracks.  Where the real
sprite obstacles (pipes, moles, Thwomps) come from is now the open
question - probing the live object blocks ($1840/$18C0/$1C00).

---

**075** — The object list is GROUND ONLY; fake pipes removed.

Cross-checking the live entity blocks against the track 7 object list:
the four live entities at race start ($1800/$1840/$1880/$18C0 - paired
records, types $C0/$C4, handler ptrs $E4E7/$E4F7, positions (268,92) and
(164,132)) match NO object-list record.  The list's >= $C0 kinds on track
7 all decode to coin scatters at road positions.  Conclusion: the
$85:D000 list holds only stamped ground features (boxes, coins, oil);
sprite obstacles (pipes, moles, Thwomps, Lakitu) are spawned by a
separate system driving the $1800 blocks - undecoded, on the backlog.

Ported accordingly: the green billboard "pipes" and their cylinder
collision at >= $C0 positions are REMOVED (they were fake pipes standing
on coin clusters).  The measured pipe crash response (NOTES 072) is kept
in a comment at collide_objects for when the entity system lands.

SUPERSEDES the NOTES 070 reading of kinds >= $C0 as sprite obstacles.

---

**076** — Kart distance scaling, finally MEASURED: the law is BINARY.

Method that worked after four failed rigs: let the attract race run
naturally past the parked player and log every frame's kart-tile OAM
entries WITH opaque pixel bboxes (decoded from VRAM), then associate
sprite clusters to karts offline by projected bearing (projection
constant fitted from single-kart frames: x - 128 = ~89 * lat/depth).
2600 frames, plus a clean teleport run for the near range.

* Near range (measured to depth 72): the FULL 32x32 art, four 16x16 OAM
  blocks plus a shadow block.  No shrinking anywhere in the near range.
* Far range (measured from depth 96 to 470+): a SMALL ~18x15 sprite
  drawn from 8x8 OAM entries.  Constant size across the whole far range.
* NO intermediate sizes appear at any depth: the sheet's rows 1-2
  (27/24px art) are NOT depth tiers - purpose unknown (rear-view or
  2P-mode candidates).  My 96/160/224 tier thresholds and the 320 cull
  (NOTES 072, labelled estimates) are all WRONG: there are no steps, and
  karts render past depth 470 - no distance cull at all.
* The switch sits in (72, 96] - ported as 84 until pinned tighter.

Failed-rig lessons are in the skill file: HUD churn, post-race freeze,
the rear-view half, and teleports fighting the 30Hz sprite pipeline all
produced convincing-looking wrong data before the natural-motion run.

---

**077** — The measured binary scaling, ported.  Far art is COMPOSED at
runtime - not stored.

Searching the ROM for the far sprites' VRAM tile bytes finds NOTHING:
the ~16px far kart is built at runtime by a software minifier (the small
shapes carry a black outline the sheet does not have at that size).
Decoding that composer is on the backlog; until then the port samples
the full frame 2:1 and keeps outline pixels (smk_draw_sprite_mini,
labelled approximation - correct measured SIZE, approximate pixels).

Renderer now: near karts (depth <= 84) draw the full 32x32 art at the
constant 1/8-screen canvas; far karts draw the 16px mini, same rotation
rule; no distance cull.  The tier stepping and 320 cull are gone.
Verified visually: the grid field at depths 94-260 renders as minis.

---

**078** — The sprite-obstacle spawner DECODED: per-track entity list at
$85:C800 + track*64.

Chain: writer trace on the live entity blocks ($1800-$18FF) -> spawn-time
writers at $84:DC56 -> hand-decode of $84:DC20:

* `LDA $0124 : XBA : LSR LSR : ADC #$C800` with bank $85 - the list is
  **$85:C800 + track*64**, WORD records, zero-terminated.
* Record: low 7 bits *8+4 = x, next 7 bits *8+4 = y (same cell scheme as
  the ground objects, +4 centres), top 2 bits a kind field (only used on
  tracks 20-23).
* Verified: track 7's first records (268,92),(164,132) equal the live
  entity positions EXACTLY; Donut Plains tracks (1/8/16) have empty
  lists, matching their obstacle-free design.  Selftest-pinned.
* Entity TYPE is per-track via a handler table ($84:DD15 indexed by
  $0D2C) - not yet decoded; MC tracks get pipes, Bowser tracks Thwomps,
  Choco moles, etc.

Ported: `ent[]` in smk_course, static cylinder collision with the
MEASURED pipe response at every entity, billboards at the true
positions.  LABELLED interim: movers (Thwomps/moles) stand still, and
the billboard pixels are still the placeholder green pipe - the entity
sprite art and the motion handlers are the open decode.

---

**079** — Grip table COMPLETE (12 classes, two batteries).  Ice is not a
grip value - it is cap-vs-limit geometry.

gripcal3 over the previously unmeasured classes, same protocol:

    $4E turn -801 slip 9445   (VL icy road: instant breakaway at pace)
    $48 turn -307 slip 7741   (breakaway)
    $4A turn -546 slip 3057   (partial breakaway)
    $58 turn -307 slip  329   (holds; VL snow)
    $5A turn -307 slip  310   (holds)
    $5C turn -307 slip  289   (holds)

One ABSOLUTE lateral limit (~250k at demo scale, = the NOTES 068 road
measurement) predicts every outcome: class cap x 307 above the limit
breaks away, below holds.  Steady slip and convergence are identical on
every class.  So Vanilla Lake's slipperiness is EMERGENT - its road cap
(.938) is high enough to cross the limit - and no per-class grip value
exists in the ROM.  The removed x0.35 ice multiplier is confirmed wrong.
Our per-class-top limit scaling (NOTES 069 feel adaptation, labelled)
preserves the cap-vs-limit relationship at 50cc speeds.

---

**080** — The player pose rule, finally measured PIXEL-EXACT.  The
straight pose is a MIRRORED HALF; the drift poses are the rotation set.

Method that settled it (framelab6 after five broken attempts): drive the
un-demo player through scripted phases with flow-steering keeping it on
the road, assemble the P1 sprite's pixels from its OAM entries (tiles
$140-$1FF at screen centre) + VRAM, and pixel-match against every sheet
frame under both flips.  Exact 0.00-mismatch identifications:

* STRAIGHT: a left-right symmetric sprite matching NO full frame - it is
  **frame 0's left half, mirrored** (0.000 on the half-compare).  Frame 0
  stores only the half; that is why it renders as fragments.  Every
  previous "centre frame" pick (1, then 2) was a rotation pose - the
  "head leaning right at rest" bug.
* STEERING (held, no slip): **frame 1** - hflip 1 for LEFT, 0 for RIGHT.
  Frame 1's base art is the RIGHT-turn pose.
* DRIFT ONSET (slip $400): **frame 47**, hflip 0 for LEFT - its base art
  leans LEFT, mirrored sense vs the rotation frames.
* DEEP SLIDES: the ROTATION set by relative angle - slip $1640 -> frame
  2, $21C0 -> frame 4, matching the measured AI bands ($1800/$2000/$2800
  boundaries).  The kart visibly goes sideways as slip grows.

The input is heading minus the LAGGING camera.  Ladder: <$0400 mirrored
straight, <$1000 frame 47, <$1800 frame 1, then 2/3/4... per the AI
rule.  Ported for player AND peers; our rigid camera synthesises the
steer lag (~$0C00 over ~8 frames - bracketed by the lab: 8 frames of
slip-free steering already shows frame 1; labelled).  The old
"frames 1 and 47" upload reading (NOTES 072) was correct data,
wrong interpretation - uploads are 512-byte STRIPS, not frames.

---

**081** — Playtest round on the pose rule: three sign/gate bugs, each
against data already in hand.

* "Sideways at rest": at speed 0 the slip angle is atan2 of a zero
  vector vs the heading - garbage that fed the ladder (and was also the
  ORIGINAL "head lean at rest" of NOTES 073).  Slip now gates to zero
  below walking pace.
* "Wrong side, only head lean": the slide term entered the ladder as
  velocity-minus-heading; the ROM's input is heading-minus-camera, so
  slides contribute MINUS the slip.  Wrong sign cancelled the steer
  lean.  Also lag and slip combine as the LARGER magnitude, not the sum
  (the lab pins drift rel ~= slip alone: $1640 -> frame 2).
* Steady steer lag re-bracketed: the lab shows frame 1 (band
  $1000-$1800) after 8 frames of slip-free steering, so the lag target
  is $1400, not $0C00 (which sat in 47's band - "turning only leans the
  head").
* Slide dynamics refitted to the lab's own slip trajectory (1024@6f,
  5696@20f, 8640@40f): hop-drift g = 0.045 (0.10 capped slip below the
  sideways poses), turn-authority collapse does NOT apply during a
  hop-drift (the lab drifts steer at full rate), and the plow's
  negative-g growth requires HELD steering - released steering recovers
  (measured ~150/frame).  Sim-verified phase table now walks
  MIRROR -> 47 -> 1 -> 2+ and back.

---

**082** — The race projection decoded from the DSP-1 stream; kart size
made road-proportional.

* SMK is a DSP-1 cart and the whole camera lives in DSP command $02.
  Our DSP model's "streaming raster" parse swallowed interleaved
  commands (garbage vs values, saturated M7 tables); a raw DR/SR access
  trace let the $02 frames be read directly:
  **F = kart, Lfe = 256, Les = 256, Azs = $3400** - sent twice per frame
  (top view + rear view).  A PPU-multiply model ($211B x $211C ->
  $2134-36, previously unmodelled) was added along the way.
* Sprite-row law measured from natural data + the P1 sprite: far karts
  pin at screen line ~99 while the player's sprite bottom sits at ~102 -
  with Lfe=256 the fit y = 97 + 1250/(256+depth) is self-consistent
  (eye ~5px above ground, ~1 degree pitch).  KEY INSIGHT: this sprite
  law is far FLATTER than the visible ground perspective - the SNES
  pairs different projections for sprites and ground.  Blindly porting
  the constant-canvas sprite rule onto OUR steeper ground is what made
  AI karts look gigantic at distance (playtest).
* Ported: AI kart size now follows our own projection (screen px per
  world px), capped at the measured near canvas (a sprite twice its
  ~16px ground footprint), full art to the measured depth 84 then the
  mini - including a mirrored-mini for the straight pose.  Labelled: the
  true fix is matching our ground to the SNES M7 line law, which needs
  the DSP raster protocol finished (open).

---

**083** — THE DSP RASTER DECODED END TO END; the game's exact ground
projection now drives our renderer.

The chain, each link measured:

* The race never issues DSP command $0A at all - the "$0A"s in the live
  stream were parameter bytes of $06 (project) calls.  Raster runs ONCE,
  at BOOT: the builder at $81:F97D (hand-decoded) writes command $0A,
  then exactly ONE Vs word ($1C - $9C = -74), then only READS: 96 groups
  of 4 results, the DSP auto-incrementing the line; a $8000 write
  terminates.  Our model's every-write-is-a-Vs parse was the desync.
* The boot loop sweeps the AZIMUTH ($94 += $100, 128 steps), building
  per-heading blocks of 96 per-line A and D words at $7E:4000/$A000
  (192-byte blocks; C uses the quarter-turn-shifted block - the sine).
  At race time the header builder ($81:FA9D, decoded earlier) just picks
  block |heading byte| * 192.  Pitch and height are BAKED at boot.
* With the protocol fixed and the $02/raster math rewritten to the
  snes9x DSP-1 reference flow (floats; the DSP1ROM fixed-point tables
  are Nintendo data and stay out of the repo), the generated tables read
  out as an EXACT law:  A(i) = 4960/(i + 3.65) in 8.8, i = ground line.
  Self-consistent constants: camera height 18.5 world px, pitch $3400,
  Les 256, Vs base -74 -> the camera ground row is frame line 102 -
  exactly the measured kart sprite row.  24 sky lines, 84 ground lines.
* PORTED: smk_render_mode7 now renders the measured law -
  scale(i) = 19.375/(i+3.65), forward = scale * (102 - line) - and
  smk_project uses the game's flat SPRITE law (d = depth+256,
  x = centre + 256*lat/d, row = 97 + 1250/d).  With both of the game's
  projections in place, the measured constant-canvas kart sizing is
  restored and finally looks right.  Physics verify: still exact.

---

**084** — HUD CONTAMINATION found: NOTES 076/077 were measuring the
scoreboard.  The projection is now one self-consistent law.

Chasing the "far kart art" led to the DMA at $81:E89C, which uploads
$7F:C200 (the blob decompressed from **$C1:0000**) to the sprite tiles.
Rendering that blob shows what it really is: **the HUD set** - the
digits 0-9, "LAP", "FINAL LAP".  The tiles I had measured as "the far
kart" ($4E $4F $5E $5F) are scoreboard sprites, which is exactly why
they came back as a constant ~18x15 at every depth from 96 to 470: a
HUD element does not move.

Re-analysing the natural-motion capture with a MOVING-vs-STATIC filter
(a HUD sprite's (tile,x,y) never changes; a kart's does) separates them
cleanly: static = {$4E,$4F,$5E,$5F}, moving = {$40,$42,$44,$46,$48,$4C}.
The moving clusters give kart bottom-row 72 at depth 150 and 43 at depth
330 - a real 1/depth curve, not a flat line.

SUPERSEDED: 076's "binary near/far law", 077's "runtime minifier" (the
art was never missing - I was looking at the wrong sprites), and the
depth-84 mini switch.

**The projection, now derived once and used everywhere.** From the boot
raster stream (Vs = line - 98) with the race camera (Les 256, Lfe 256,
Azs $3400 -> camera height 18.6 world px), the DSP's own arithmetic
gives, per SNES frame line L:

    depth(L) = 4972 / (L - 20.36)        world px from the EYE
    scale(L) = depth(L) / 256            world px per SNES pixel

The depth/scale ratio comes out at **exactly Les = 256** - the
cross-check that the chain is right.  The player's kart at line 102
(measured) is therefore 61 world px from the eye: the camera TRAILS the
kart by 61 px, which is why sprites and ground finally agree.  Ported to
both smk_render_mode7 and smk_project, with kart size following the same
law anchored on the player's 32 px (labelled divergence: the SNES cannot
scale sprites and quantises to a few art sizes; ours is continuous).

---

**085** — Race furniture on the game's own art: HUD set, clock, lap,
start countdown.

* The blob at **$C1:0000** (decompressed to $7F:C000 by $81:E856, offset
  $200 DMAd to sprite tiles $40-$BF by $81:E89C) is the HUD sprite set:
  digits, "LAP", "FINAL LAP", separators.  Ported as `smk_hud_load`.
* Digit mapping, read off the rendered sheet: **0-4 = tiles $A7-$AB,
  5-9 = $B7-$BB** - a 5-wide strip that wraps by the 16-tile VRAM row.
  Separator tile $A2.  Sprite palette **$C0**, from the live HUD OAM
  attribute ($28 -> (a>>1)&7 = 4 -> $80 + 4*16).
* Ported: the lap counter and race clock (M ' SS " HH) draw with the
  ROM's own tiles, inside `draw_scene` so the interactive loop and
  --shot cannot drift.  The clock counts frames, which is what the
  console's own timer counts.
* Start sequence: karts (player AND AI) are held for a countdown, then
  released, with 3-2-1 shown in the ROM's digits.  LABELLED interim -
  the observable cadence (60 frames a step) is right, the exact ROM
  start-frame count and Lakitu's light art are not decoded.

---

**086** — The entity (pipe) sprite chain located; art confirmed, source
offset still to pin.

What is now certain:

* The live pipe on Mario Circuit is VRAM sprite tiles **$CE-$D7**,
  arranged **2 wide x 5 tall** (16 x 40 px), sprite **palette 7**
  ($80 + 7*16 = $F0) - rendered from the running machine's own VRAM and
  CGRAM, unmistakably the green pipe.  The entity block at $1800 carries
  exactly that tile list at offset **+$0A**.
* The load chain: `$81:E592` decompresses **$C7:0000 -> $7F:4400**;
  `$81:E5A0` then expands it into the sprite staging with a
  copy-16-bytes / zero-16-bytes loop up to source $2000 - i.e. the
  entity art is **2bpp** widened to 4bpp (planes 2-3 zero), which is why
  only palette entries 1-3 are used.  `$85:81A9` DMAs 8192 bytes from
  $7F:A000 to VRAM $8000 (tiles $00-$FF), after which the HUD blob
  overwrites $40-$BF - so tiles $C0-$FF are the entity set.
* Mapping: VRAM tile n <- staging $A000 + n*32 <- source $4400 + n*16.

Open (next session, short): decompressing $C7:0000 gives 4096 bytes, but
the loop consumes 8192 from $7F:4400, so a second stream fills
$5400-$63FF; and tiles rendered from `blob[n*16]` do not yet match VRAM
byte-for-byte, so the staging is not a plain 1:1 image of that one
stream.  The reliable fix is to replicate the game's own sequence (both
decompressions into a $7F image, then the expand loop) exactly as
`smk_track_load` already does for tilesets - all anchors above are
verified.

---

**087** — Correction: **Super Mario Kart has no mini-turbo.**

The drift state machine and its `$C2` counter are measured facts - `$E2`
walks `$8000` (hop) -> `$8004` (slide) -> `$8024`, and `$C2` charges
~85/frame toward the cap at `$0E20`.  Calling that charge a
"mini-turbo", as ROADMAP did, was NOT decoded: it imported a **Mario
Kart 64** mechanic that this game does not have.  SMK's drift is a
hop-slide with no charge-and-release boost.

What `$C2` actually drives is therefore OPEN, not "the mini-turbo we
have not ported yet".  Candidates to test against the running game
before anything is ported: slide duration or its exit condition, the
hop/slide animation phase, or the sound trigger.  Recorded here because
a wrong label on a real measurement is worse than no label - it reads
like a decoded fact six months later, which is exactly what the
roadmap's principle 4 exists to prevent.

---

**088** — Playtest round: four measurement batteries, and the bit-7
"ramp" rule finally killed.

**Surface battery** (drive head-on into each class at pace, 40 frames):

    $20 $24 $26   speed -> 0,   moved  3 px, z 0      DEAD STOP
    $80 $82 $84   speed KEPT,   moved 50 px, z 0      WALL, state $C000
    $10           608 -> 701,   moved108 px, z 247    RAMP (the launcher)
    $22           885 ->  14,   moved 64 px, z 141    fall/pit
    $40 $42 $44 $4C  speed held/rising, 100-134 px    road
    $4E $54 $56 $5A  big speed loss, 79-100 px        off-road

This inverts the rule I had: **bit-7 is a WALL, not a ramp** - which is
also what NOTES 044 measured head-on long ago, and what the playtest
reported ("impossible to trespass").  The invented "bit-7 bars launch
you" rule let a kart at speed vault Mario Circuit's barrier blocks and
fly off the world.  `smk_surface_solid` tested only bit 5, so `$80` was
not even solid.  Both fixed; the launcher is class **$10**.  The two
solid families are now distinct: `$20/$24/$26` stop dead, `$80/$82/$84`
deflect with the speed preserved.

**Acceleration battery** (from a standstill, throttle held, on road):
speed climbs 0 -> 711 over ~150 frames (2.5 s) on an S-curve, still
rising at the end.  Our curve matched the game frame-for-frame to about
half speed and then ran ahead and SNAPPED against a hard clamp.  Fitting
the measured approach (12, 9.2, 7.6, 4, 3, 2, 2, 1.8 units/frame at
speeds 355..702) gives a taper on the remaining headroom; with it our
class-1 curve tracks the ROM within 1-2% the whole way (f90 506 vs 507,
f120 649 vs 653, f150 717 vs 711) and keeps climbing instead of
clamping.  LABELLED: the taper is a fit to measured behaviour, not a
decode of the ROM's near-target law.

**Braking battery**: from 589 the game reaches 99 in 85 frames - about
**5.8 units/frame** - while merely coasting loses 5.2/frame.  Braking in
SMK is barely stronger than lifting off.  Ours was 32/frame, 5.5x too
strong.

**Hop battery** (sprite row through a hop): the kart rises **12 screen
pixels** and is back down after **~19 frames**.  The old pair
($0080 launch, gravity 26, height = z>>16) peaked under ONE pixel in 10
frames - the hop happened but was invisible ("no jump").  The captured
arc in NOTES 045 is a **wall bounce**, a different event: same gravity,
different launch.  So `SMK_BOUNCE_VEL` keeps the captured arc (still
selftest-pinned) and `SMK_HOP_VEL` is 247 = 9.5*26, with one screen
pixel = 25029 z-units.

Also fixed from the same round: the cornering stutter (my plow used a
negative grip gain, which SHRANK the velocity vector - speed fell in
steps, grip returned, repeat; the ROM keeps speed through a plow, so the
plow now rotates velocity away from the heading at the measured
130/frame and renormalises), and the hop is no longer gated on speed.

---

**089** — Playtest round: the wall is not a bounce, the slide saturates,
and a verification claim I had been making was hollow.

**The wall, measured by DISPLACEMENT** (head-on at 820, tracking where
the kart actually ends up rather than just its velocity):

    f0-6   fwd -1.8 px, held      pushed back under two pixels
    f8+    fwd 2.7 .. 29.4 px     drives on, scraping along the wall
    speed  820 the whole time     never lost

A wall does not throw you back at all.  It cancels the into-wall motion,
holds for about six frames, and then you scrape along it.  Our port
REFLECTED the velocity and then ran ten ballistic frames, which is ~30 px
of backward flight - the "bounce is a few meters long" report.  Ported:
cancel the blocked component, keep the tangential one, keep the speed.
($20/$24/$26 still stop dead, as NOTES 088 measured.)

**The slide, measured over 150 frames of hop-drift:**

    f5 11.2 deg  f10 22.8  f20 43.0  f30 62.3  f45 75.0  f60 83.8

The growth DECAYS - per-frame steps of 423, 366, 352, 154, 106 - so slip
approaches a ceiling near 17000 units (93 deg) at about 0.03 of the
remaining gap each frame.  My constant 130/frame grew it without bound:
the kart swung past 90 degrees and travelled backwards, which is exactly
the reported "magically drifts opposite to where you are heading" and
the side-on sprite that comes with it.  Speed in the same capture holds
around 850 and then sags to ~0.70 of pace.  All three ported.

**The hop never fired at all**: `input_edges_clear()` ran immediately
before `step_kart()`, so `in.hop` was always false by the time the
physics read it.  Reported as "no jump" twice; it was never a physics
question.

**And a correction about verification, not about the game.**  The "AI
completes a lap on 20/20 tracks" gate I have been quoting all along ran
from a harness outside the repo, and `racer_step` was `static` in
main.c - so that harness could only have been exercising a SECOND COPY
of the AI logic.  It could have passed while the shipped AI was broken.
The AI now lives in `src/ai.c` in the library, the regression
(`tools/ailap.c`) links the same code the game runs, and it is part of
`make check`.  The labs themselves have moved into `tools/labs/` for the
same reason: /tmp was cleaned this session and took every rig with it.

---

**090** — There is no grip loss in normal cornering.  The whole
breakaway model described something this game does not do.

The playtest report was "past the orange zone you cannot turn at all,
it is an on-off switch, and you never get control back".  Two of those
are straightforward bugs in my model; the third turned out to be the
model itself.

**The measurement** (`tools/labs/authority.py`): hold full lock at pace
and record, per frame, the change in HEADING and the change in VELOCITY
DIRECTION, so understeer and oversteer can be told apart.

    full lock, throttle held   dHead -307 every frame, dVel -300..-320
    full lock, no throttle     dHead -307 every frame, dVel -300..-320
    slip in both               ~300 units (1.7 deg), steady, 120 frames

The kart turns at its full rate the whole time, at every speed from 850
down to 171, and the velocity follows within a few units.  The residual
~300 is just the one-frame lag between the heading update and the
velocity update.  The only large slips in the capture appear AFTER a
collision (speed collapsing 589 -> 368, slip jumping to 110 deg).

So: **no lateral-force limit, no breakaway, no progressive plow.**  The
"authority collapse 307 -> 20" of NOTES 068 was a crash being read as a
corner.  All of it is deleted.  Normal driving is full grip - the
velocity IS the heading direction, which is also what `smk_kart_face`
does in the ROM.

Two consequential bugs went with it: the breakaway state LATCHED (enter
above 4000 slip, leave below 2800, while the branch itself kept slip
growing - so it could never leave: "you do not get control back"), and
the drift rotated the velocity away from the heading ON TOP OF the
heading's own rotation, double-counting the slide and opening it twice
as fast as measured, straight past 180 degrees.

**What a slide really is**, kept from NOTES 089 and now modelled the
right way round: the kart PIVOTS while the velocity keeps going.  The
heading turns at its full rate, the velocity rotates slower, and the gap
is the slide - opening ~410 units/frame, holding that rate to about
11000 units and then tapering to nothing by ~16500 as the velocity comes
back up to the heading's rate.  Simulated against the capture: 11.0 /
22.0 / 43.9 / 65.6 / 82.6 / 88.0 degrees at f5/10/20/30/45/60 against
the measured 11.2 / 22.8 / 43.0 / 62.3 / 75.0 / 83.8.

---

**091** — Top speed was capped too low, and the surface table is
verified byte-exact against the running game.

**Top speed.**  `tools/labs/wall_top.py` holds the throttle along open
road: the game reaches **963**.  We selected entry 3 of the ROM's
target-speed table, which tops out at 672/816/880 for the three classes,
so the kart was limited well under the game's own top - the "still feels
slower than the real game" report.  Entry 6 (896/912/992) brackets the
measured value for every class and keeps the 50 < 100 < 150 ordering.
It also repairs the surface caps: those were measured as fractions of a
road top of ~951, so a top of 672 shrank every off-road cap with it.
`tools/labs/curve.c` prints our curve in the ROM battery's format for
direct comparison.  Residual, labelled: our tail runs slightly fast, and
963 sits above class 1's entry-6 value, so the exact entry is uncertain.

**The surface table is right.**  `tools/labs/surftable.py` dumps the
live $0B00 table and it is **byte-identical** to what our loader builds
for track 7 - classes $26, $40, $52, $54, no bit-7 anywhere in the 192
theme tiles.  Mario Circuit's barrier blocks are NOT theme tiles: they
are tiles **240-243**, in the object range, whose classes come from the
live $0BC0+ capture and are $80.  A lab that stopped scanning at tile
192 therefore reported "no bit-7 wall on this track at all" - it was
looking in the wrong half of the table.

**The wall, third attempt.**  Two earlier tries failed in opposite
directions: reflect + a 10-frame ballistic window threw the kart ~30 px
backwards, and cancelling the component instead made it STICK.  The two
clean captures are NOTES 044 (the into-wall component reflects, speed
preserved) and NOTES 088 (a kart held against $80 still covers ~50 px
per 40 frames, so it is not pinned).  Ported: reflect, keep the speed,
and hand control back after a short window.  LABELLED - the reflection
and speed preservation are measured, the 3-frame window is an estimate
bracketed by those two captures.  Both attempts to capture the impact
directly missed: filling the map with wall put the kart INSIDE a solid,
and the aimed run drove past the block without ever setting the contact
state.

---

**092** — The wall impact, finally captured; and NOTES 086's entity-art
source was wrong.

**The rig that worked.**  Two earlier attempts failed: filling the map
with wall put the kart INSIDE a solid, and aiming it by writing the
heading did not stick, because under player control the game rewrites
the target angle from the pad every frame (NOTES 044 says so - I had
read it and still made the mistake).  What works is to leave the kart
driving normally and PAINT wall tiles into the tilemap ahead of it
(`tools/labs/wall_impact.py`).  Tile 240 is class $80 on this track.

    f20  845  vx -844          approaching
    f21  845  vx +844  $C000   the into-wall component REFLECTS
    f22  422  vx +422  $C000   the speed EXACTLY HALVES (845/2 = 422)
    f22-29                     knockback, ~17 px travelled backwards
    f30  423          $8000    control returns
    f35  vx negative           driving forward again

**The speed halving is the piece I never had.**  NOTES 044 read the
speed as preserved (it sampled a kart that was re-accelerating), so our
port reflected at FULL speed - a violent bounce - and when I "fixed"
that by cancelling the component instead, the kart stuck to the wall.
Reflect, halve, hold ~9 frames: total rebound about 20 px, which is the
short bounce the playtest describes.  All three numbers are measured;
nothing here is estimated any more.

**Entity art: NOTES 086 was wrong.**  It named $C7:0000 as the source,
inferred from a nearby decompress call.  A byte comparison
(`tools/labs/entity_art.py`) kills it: the staging at $7F:4400 does not
hold that blob, and the live VRAM tiles do not appear in it anywhere.
The live tiles are a clean repeating pattern (16 27 16 27 ... - a pipe's
vertical edges).  Searching the graphics banks for those bytes is the
open thread; the DMA that fills the tiles ($85:81A9, 8192 bytes from
$7F:A000) and the copy-16/zero-16 expander ($81:E5B7/$E5C7) are still
good anchors, only the ROM stream behind them is unidentified.

---

**093** — The entity art found, and it was not where NOTES 086 said.

NOTES 086 named `$C7:0000`, inferred from a decompress call near the
expander.  A byte comparison against the running game refuted it
(NOTES 092).  The way to find it was to stop reasoning about call sites
and SEARCH: decompress every plausible stream in the graphics banks and
look for the bytes of a live VRAM entity tile
(`tools/labs/find_entity_gfx.py`).

    stream $C1:0F9B -> 1824 bytes = 57 tiles of 4bpp
    stream tile 14 == VRAM $CE, 15 == $CF, 16 == $D0
    so VRAM sprite tile $C0 + n is stream tile n

Two of my earlier readings were wrong together: the source (not $C7) and
the format (4bpp already, not 2bpp widened by the copy-16/zero-16 loop -
that loop belongs to some other asset).  The pipe is stream tiles 14-23,
2 wide x 5 tall, palette base **$F0** - its pixels use indices $A-$E,
which land on the greens at $FA-$FD.

Ported: `smk_objgfx_load`, and the entity billboards now draw the real
art, scaled continuously with the projection like the karts.  Flooring
the scale at one screen pixel per art pixel left distant pipes at full
size with their tops above the horizon, floating in the sky - the same
mistake as the kart mini-art, in miniature.

Still open: the entity MOTION handlers (Thwomps and moles are static),
and whether $C1:0F9B is global or selected per theme.

---

**094** — Three playtest bugs, all mine, none of them physics.

**The jump was never wired up.**  `smk_kart_gravity` is called by the AI
(`src/ai.c`) and by the selftest - and NOT by `step_kart`.  So a hop set
`airborne` and `zvel`, nothing ever advanced z, the kart never rose or
landed, and because the flag stayed set every later hop was refused too.
Reported as "no jump" three times, and the selftest passed throughout
because it calls gravity directly - a test that exercised the primitive
while the caller was missing.  One line.  The arc now lands on frame 19
with a 10 px peak, against the measured 19 frames / 12 px.

**The pipe was assembled wrong.**  I stacked ten consecutive tiles as
2 wide x 5 tall.  A multi-tile SNES sprite steps by the VRAM ROW STRIDE
of 16, not by its own width: the pipe is stream tiles 14,15 over 30,31 -
2x2, 16x16 px - which renders as a clean cylinder with a base rim.  The
consecutive tiles after 15 are OTHER objects, each successively
narrower, which is why the column came out offset and scrambled.

**Barriers pinned the kart** because every re-contact counted as a fresh
impact and halved the speed again: 800, 400, 200, 100.  The measured
evidence is one halving per IMPACT (NOTES 092) and speed PRESERVED under
sustained contact (NOTES 088), so the kart now remembers it is touching
a wall for 20 frames - long enough to span the knockback and the drive
back in - and a continued contact just cancels the into-wall component
and scrapes.  One clean bounce, then it rests against the barrier with
its speed intact and pulls away the moment you steer.

---

**095** — Walls: you never SLID along them.  Pipes: we were drawing the
smallest tier.

**The barrier "stick", found by simulation.**  `tools/labs/bandsim.c`
drives at a diagonal band from 72 angles: the kart never penetrates, so
the collision test was fine and the screenshot's embedded kart came from
somewhere else.  What the sim did show is that `smk_kart_move` RETURNED
without moving on any contact - discarding the along-wall component with
the blocked one.  A kart held against a barrier therefore froze in place
instead of scraping past it, which from the driver's seat is exactly
"stuck in the barriers".  The surface battery had already measured ~50 px
of travel while against a wall (NOTES 088); I had the evidence and did
not use it.  Now: move on whichever axis is not blocked.  A 70-degree
approach bounces once (speed halves, as measured) and then slides along
the wall with its speed intact.

**The pipe was the wrong SIZE TIER.**  Rendering the whole object sheet
shows the same pipe stored at many sizes - the SNES cannot scale a
sprite, so it keeps a tier per distance band, and a live entity's tile
list changes as you approach.  The tiles I had taken from one captured
entity ($CE) are a small FAR tier, which is why our pipes were squat
cans next to the original's tall cylinders.  Base 32 is the near tier -
a 12x16 cylinder with a dark rim - and matches the reference screenshot.

Two things stay labelled: we draw one tier scaled continuously rather
than choosing a tier by distance (the same divergence we make for
karts), and the 2x2 assembly uses the VRAM row stride of 16, verified by
rendering but not by a live OAM capture (the demo never draws entities).

---

**096** — The barrier stick was a "dead stop" I invented from a
degenerate rig.  Every contact bounces.

Playtest: "you hit it, you don't bounce, speed goes to 0, you are
stuck", against the original where "you bounce back and can continue to
accelerate towards it so you keep bouncing".

Two things in our port were wrong, and both trace to the SAME bad
measurement.  The surface battery (NOTES 088) filled every driveable
tile with the class under test, so the kart was standing INSIDE a solid
rather than driving into one - and an embedded kart reads as speed 0
with ~3 px of travel no matter what the class does on contact.  From
that I concluded a "dead stop family" ($20/$24/$26 -> speed 0) and later
added a scrape that suppressed repeated impacts.  Zeroing the speed is
exactly what pinned the kart to the barrier.

The only clean impact capture (NOTES 092, wall tiles painted into the
path of a normally-driving kart) shows reflect-and-halve, and the
playtest says repeated bounces are real.  So: EVERY contact reflects the
blocked component and halves the speed, on every solid class, with no
family distinction and no scrape suppression.  Verified with the
throttle held into a wall - impacts at f10/24/38/51/64, each bouncing
back and re-accelerating between, speed never reaching zero.

This is the third time the fill-the-map rig produced a confident wrong
answer (the first two: the bogus wall displacement, and "speed preserved
under sustained contact").  It only ever measures an embedded kart.
Recorded in the skill file so it is not repeated.

---

**097** — The stick was never in the collision.  The player overwrote
its own rebound.

After a wall impact the ROM runs `$42,x` frames with no steering and no
thrust (NOTES 071) - the kart flies on the rebound velocity.
`smk_kart_face` honours that window, which is why the AI has always
bounced correctly and why every library-level test of the bounce passed.

The PLAYER does not go through `smk_kart_face`: `step_kart` computes the
velocity itself.  It never checked `bounce_cool`.  With grip at 1.0
(NOTES 090) that line is `velocity = heading * speed` every frame, so
the rebound was wiped the frame it was applied and the kart pressed
straight back into the barrier for ever.  Four rounds of "still stuck"
reports, and each time I re-measured the COLLISION - which was right the
whole way - because my tests exercised the library primitives and the
bug was in the caller.  The same shape as the missing gravity call: the
piece under test was fine, the thing that uses it was not.

Fixed by gating the player's velocity update on the ballistic window.
`tools/labs/playerwall.c` now drives the PLAYER'S tick order into a wall
and asserts it travels: impacts at f10/33/54, each rebounding ~26 px and
re-accelerating between, which is the "bounce back and keep bouncing"
the playtest describes.

---

**098** — The object graphics are PER THEME.  Rainbow Road gets Thwomps.

Playtest compared Rainbow Road against the original: the game draws
**Thwomps** - grey blocks with faces - where we drew the Mario Circuit
pipe tinted grey by the track palette.

The open question from NOTES 093 ("is $C1:0F9B global or per theme?") is
answered.  Searching the ROM for pointers to that stream lands on
**$81:EBD3**, a 3-byte-per-entry table sitting right beside the tilemap
($81:EB5B), tileset ($81:EBA3) and palette ($81:EBBB) tables - the same
per-theme family:

    theme 0 $C0:0000   theme 4 $C1:0F9B
    theme 1 $C1:0F9B   theme 5 $C1:1706
    theme 2 $C0:05D6   theme 6 $C0:1070
    theme 3 $C1:0AA5   theme 7 $C0:1070   (Rainbow Road: Thwomps)

Every theme's set decompresses to the same 57-tile, 1824-byte shape with
the same tier layout, so only the artwork changes - which is why the
2x2/stride-16 assembly and base tile 32 carry over unchanged.  Themes 6
and 7 share a set, and theme 1 and 4 share the pipes.

Ported: `smk_objgfx_load` takes the theme and reads the table; the game
reloads it on every track change.  Verified on track 5 (theme 7) -
Thwomps with faces, in the game's own art.

Still open, unchanged: the entities do not MOVE (a Thwomp should rise
and slam), and we draw one size tier scaled rather than choosing a tier.

---

**099** — Entity size and passability: two invented numbers replaced.

Side by side with the original on Rainbow Road, our Thwomps were about
twice the kart's height where the game keeps them SMALLER than the kart,
and the row of four sealed the road.

**Size.**  We drew one tier scaled continuously with the projection, so
a near object ballooned - while the karts beside them use a constant
SNES-proportion canvas.  The hardware cannot scale a sprite at all: the
sheet stores the object at several sizes and the game picks one, which
is exactly why a live entity's tile list changes as you approach (an
observation from NOTES 078 I had noted and not used).  Measured off the
sheet, identical in every theme:

    base 32 -> 12x15     base 34 -> 11x13     base 36 -> 10x11

Ported: the tier whose art height best matches 4096/depth (16 world px
seen at that depth, in art pixels), drawn at the constant SNES
proportion like the karts.

**Passability.**  The collision half-width was 12 px - invented back
when entities were placeholder billboards and never revisited.  Rainbow
Road's Thwomps sit 8 and 16 px apart, so a 12 px radius sealed the 16 px
gap you are meant to thread.  The near tier's art is 12 px across, so
the half-width is 6.  `tools/labs` harness confirms lanes 40-48 now pass
between the Thwomps while the entities themselves still block.

---

**100** — Distant objects DO shrink.  NOTES 084's constant canvas was
wrong, and it took the entities down with it.

Playtest: "far away look bigger than getting closer - they should grow
as we get closer".  Measuring our own frames confirmed it: near Thwomps
45 px, far Thwomp 48 px.  Not inverted so much as FLAT - and against a
road that shrinks with distance, a flat sprite reads as growing.

Two causes, one root.  The tier ladder I had picked spans only 15 -> 11
art pixels, so switching tiers is nearly invisible; and the draw size
was the constant SNES proportion, which by construction cannot change
with distance.

The root is NOTES 084.  It concluded "constant canvas at every depth"
from an OAM sweep - the same family of sweep that produced the HUD
contamination.  The reference screenshot settles it directly: three
opponents up the road are about a THIRD of the player kart's height.
Distant karts are smaller, plainly, and I had built the opposite into
the renderer and then matched the entities to it.

Ported: karts and entities both scale with the projection, anchored so
that an object at the player's own depth (the 61 px camera trail) draws
at the SNES's own size, and shrinks from there - clamped so nothing
exceeds that size up close.  The size tier now only selects the
ARTWORK.  Measured on our own output: a Thwomp is 40 px at ~66 depth,
18 px at ~156, 14 px at ~296.

The continuous scale remains a labelled divergence: the hardware
quantises to the sheet's tiers, we interpolate.

---

**101** — Entities quantise to the sheet's real size tiers.

The SNES cannot scale a sprite; it swaps to a smaller drawing.  The
sheet carries that ladder, and the same descending family exists in
every theme:

    theme 1   b0 12x15   b32 12x16   b34 11x14   b36 10x12
    theme 7   b0 12x16   b32 12x15   b34 11x13   b36 10x11

So the whole range is 16 -> 11 art pixels: an object grows to the
largest drawing and stops, and distant ones settle at the smallest
rather than dwindling away.  Ported as four tiers drawn at the fixed
SNES proportion, so the size POPS between steps.

One arithmetic slip worth recording, because the symptom was so
misleading: the apparent height of an object H world px tall is
H * LES / depth, and I first anchored it on the camera TRAIL (61)
instead of LES (256).  A factor of four meant `want` never reached the
upper tiers, so EVERY object drew at the smallest one - which looks
exactly like "no scaling at all" and sent me hunting the draw code
rather than the one line that chooses the tier.  Verified by
instrumenting the choice: depth 111 -> tier 0, 221 -> tier 0, 341 ->
tier 2, 461 -> tier 3.

Karts still scale continuously (NOTES 100); quantising them needs their
own tier ladder off the kart sheet, which is the rows-1-2 question still
open in P4.

---

**102** — Karts quantise to the kart sheet's own tiers too.

The ladder was identified long ago (NOTES 030) and the constants have
been sitting in the header the whole time - `SMK_SPR_TIER0/1/2` - as
three 11-frame rotation sets.  Measured max art height per set:

    frames  0-10   31 px
    frames 11-21   28 px
    frames 22-32   25 px
    (frames 33-47 are the special poses: the leans and the mirrored
     straight, all 32 px)

plus the half-size drawing NOTES 072 saw beyond those, which is what
makes the far range work: 31 -> 28 -> 25 alone is far too narrow to
explain a reference shot where distant opponents are about half the
player's height.

Ported exactly like the entities: the tier is chosen from the height the
projection asks for and drawn at the fixed SNES proportion, so kart
sizes POP between steps.  The rotation frame is re-picked inside the
chosen tier, so a kart keeps its correct facing as it changes size.

With that, both the constant canvas (NOTES 084) and the continuous scale
(NOTES 100) are gone - each was a half-truth: the hardware neither keeps
one size nor glides between sizes; it swaps between a few drawings.

Smooth scaling is not deleted, it is DEFERRED: recorded as the first
entry of the new roadmap phase P9 (quality of life), where the rule is
that it must reproduce the quantised sizes at the tier distances and
only interpolate between them.

---

**103** — The "largest tier" was a skewed drawing.  Torn sprites, and
no scaling.

Playtest: objects still not scaling, and the near ones visibly torn.

Rendering EVERY candidate base side by side settles what the sheet holds
(`/tmp/bases.png` from the lab): bases **0, 2, 4, 6 are skewed
perspective variants** - the object seen at an angle, sheared - and
bases 8/10/12/14 and 32/34/36 are the clean front-facing drawings.  I
had put base 0 at the top of the ladder because its opaque bounding box
was the biggest (12x16), which it is: a sheared drawing fills more of
its box than an upright one.  Measuring extent found the largest
rectangle, not the right sprite.

That single wrong entry caused both symptoms: everything nearer than
about 250 depth selected tier 0, so it drew the sheared art (the
"tearing") AND every near object used one tier, so nothing scaled in the
range where scaling is most visible.

The clean ladder, by art height:

    base 32  12x15      base 34  11x13
    base 36  10x11      base 12  14x9    (the far drawing)

Verified in place: at depth 121 the tier is now base 32, at 537 base 12,
and the render shows a large clean near Thwomp with a small one behind.

Lesson for the sheet work: a bounding box tells you how much of a box is
inked, not whether the drawing is the one you want.  Look at the art.

---

**104** *(size conclusion SUPERSEDED by 105 - the tier is the base
drawing, not a size cap)* — The object sheet, analysed properly instead of one tier at a
time.  And a hard limit worth stating.

We had circled this three times, so: render the whole sheet in its VRAM
layout, measure EVERY 2x2 origin, and check each for aspect and for
touching the box edge.  Both themes give the same structure.

    theme 1 (pipes)                theme 7 (Thwomps)
    b0  12x15  h/w 1.25  edge      b0  12x16  h/w 1.33  edge
    b2  11x16  h/w 1.45  edge      b2  12x16  h/w 1.33  edge
    b8  16x11  h/w 0.69  edge      b8  16x10  h/w 0.62  edge
    b10 14x11  h/w 0.79            b10 16x11  h/w 0.69  edge
    b12 14x9   h/w 0.64            b12 14x9   h/w 0.64
    b32 12x16  h/w 1.33            b32 12x15  h/w 1.25
    b34 11x14  h/w 1.27            b34 11x13  h/w 1.18
    b36 10x12  h/w 1.20            b36 10x11  h/w 1.10

* The "edge" flag is NOT a fragment marker: assembling four tiles wide
  shows two separate pipes side by side, each merely RIGHT-ALIGNED in
  its own box.  Bases 0-6 are complete drawings - a second view of the
  object (obvious on the Thwomps, which are visibly skewed there;
  invisible on a cylinder).
* The squat family (h/w 0.6-0.8, bases 8/10/12/14) is almost entirely
  RIM.  Using base 12 as the far tier is why distant pipes rendered as a
  lid with no length.  Dropped: the ladder is 32/34/36 and beyond the
  smallest we keep drawing the smallest.

**The hard limit.**  The biggest pipe drawing in the ROM is 12x16 SNES
px; the biggest kart drawing is 30x31.  A pipe is therefore 0.52 of a
kart's height ON THE HARDWARE, at any distance, because the SNES cannot
scale a sprite and no larger pipe exists in the data.  Our render is at
that cap already.  Drawing objects larger than their own artwork is not
a decode any more, it is a change to the game - so it belongs in P9
(quality of life) next to smooth scaling, not in the faithful path.

---

**105** — Object size: the game SCALES its billboards, and the law is a
single constant.  NOTES 104's "hardware cap" was wrong.

Playtest, four rounds running: "pipes are still same small size".  I had
concluded from the object sheet that 12x16 art was a hardware ceiling -
the SNES cannot scale a sprite, so the tier IS the size - and moved the
complaint to the quality-of-life phase.  That was wrong, and it was
wrong because I measured the ART instead of the GAME.

**The measurement.**  The live entity block carries a scale at +$06.
Moving a real entity to a chosen distance straight ahead of the kart
and reading it back (`tools/labs/pipe_tier.py`):

      d    +$06    0x4200/d
    320   $0035        53
    176   $0060        96
     64   $0108       264
     32   $0212       530

    +$06 = 0x4200 / d   (8.8 fixed point, ratio 1.000-1.004)

So an object stands at its NATURAL art size when it is 66 world px from
the kart, and at DOUBLE that from half as far.  No saturation down to
d = 4.  The distance is from the KART, not the camera: adding the 61 px
trail destroys the fit.

**It holds for karts too.**  The kart blocks carry the same field and
the same constant (ratio 1.009-1.018 over four opponents at 256-355 px),
which makes this the game's universal billboard rule rather than a pipe
quirk.

**Cross-checked against the reference screenshot.**  The player kart is
four 16x16 sprites - a 32x32 cell, ink 30x31 - confirmed from a live OAM
capture, which gives a ruler that does not depend on the capture's crop
or aspect.  Against it the original's near pipe measures 22x32, and the
outline that is one art pixel on the kart is 2.4 on the pipe: the pipe
is MAGNIFIED, not drawn from bigger art.  Our render put it at 16 px
flat - the 0.57-vs-1.03 height ratio the playtest kept reporting.

Ported: object and kart size both come from `SMK_OBJ_SCALE_K` (66.0)
over the kart distance; the tier only decides which drawing, never how
big.  The kart path had its own invented anchor (16 * LES / camera
depth) which held distant karts too large - now the measured law.

**Two corrections from the next playtest** ("the pipes zoom in as I get
closer, without cap"):

* The law is against the EUCLIDEAN distance from the kart, which is how
  it was measured.  I applied it to the along-axis depth, which collapses
  toward zero as an object draws level with you - so the pipe grew until
  it filled the screen.  Both draws now use the real distance.
* The scale FIELD never saturates (0x1038 at d=4), but the drawn PIXELS
  must.  The largest pipe the original is ever seen to draw is 22x32 -
  exactly twice the near tier, with an outline two art pixels thick
  where the kart's is one - so objects magnify by at most 2, and karts,
  where a peer alongside you is the same size as your own kart, not at
  all.  Cap taken from the reference, not invented, and labelled as
  such: it is the largest draw observed, not a constant read out of the
  ROM.

That puts a pipe at 32 SNES px from d=33 inward against the kart's 31 -
the reference's 1.03 height ratio.

Dead ends worth not repeating: the sheet has no tall pipe.  A 4x4-tile
assembly, a two-piece cap-over-body stack, and an alignment search over
horizontal offsets all fail to join, and the one 14x31 connected
component is two separate drawings touching across a tile-row boundary.
The tile list at block+$0A is never read by the game (traced) - it is
initialisation data, not a live tier selector, and the demo never draws
entities at all, which is why the OAM route needed the entity moved to
the kart rather than the kart driven to the entity.

---

*(next entry: 106)*


---

**106** — The player's physics DECODED: top speed, acceleration, steering,
the slide, the power slide, the spin-out, the hop, and off-road.  Verified
frame-exact against the running game.  MAME is the new oracle.

**Method.**  MAME 0.285 runs the cart with the real DSP-1 (`upd7725` +
`dsp1.bin`) at 350% headless and exposes memory per frame from Lua and the
debugger (`tools/labs/mame/README.md` records what does and does not
work).  The attract race is two HUMAN-recorded karts (Mario, Toad; 100cc)
doing hops and power slides, so it was logged field by field
(`demolog.lua`) and the decode was re-simulated from the logged pad words
(`resim.py`): every field - `$B2 $A4 $A8 $AA $FA $A6 $EA` - matches on
every frame of the race for both karts, except the frames inside a
mushroom boost.  The C port replays the same captures in the selftest
(`tools/selftest_slide.inc`, `_spin.inc`): 125/125 and 77/77 frames exact.

**Three angles per kart** (`$80A892`, run every frame after the jump
machine):

    $A4 += $B2 >> 3          heading: the stick turns this; the CAMERA
                             follows it (+$C0, measured: cam $94 - $A4 == 192
                             every frame, $808632)
    $A2  = $A4 + $A8         velocity direction (fed to the DSP-1 at $80A4D0)
    $2A  = $A4 - $AA         pose: what the sprite shows

`$A8` and `$AA` are the two halves of a slide.  In normal cornering both
are zero - full grip, as NOTES 090 measured - and a slide is a state, not
a force limit.

**Turn rate `$B2`** (`$80A80F`), from a per-character steering row
`[max, reversal, ramp, decay]`: holding a direction ramps `$B2` by `ramp`
per frame to `max`, reversing subtracts `reversal`, releasing decays by
`decay`.  Mario's plain row is `$0995/$98/$68/$70`: max `$995 >> 3` = 306
per frame, the exact figure NOTES 090 measured.  Holding a shoulder button
or Y selects the row at block+$50 (`$B00/$B0/$88/$90`): 352 per frame.
Above speed `$300` the rate is damped each frame by a DSP-1 multiply,
`$B2 += ($80A7FF[(speed-$300)>>5] * $B2) >> 15`, i.e. up to -2.7%/frame.

**Speed** (`$80A6F7`, B held): `$D6 = $B4 + 8 * min(coins, 10)` is the
target; `$B4` is the character's base top (`$81:8000`: Mario/Luigi 912,
Bowser/DK 944, Peach/Toad 880, Yoshi/Koopa 864; 50cc -128, 150cc +160
when `$2C == 0`).  Under it, accel = the character's 16-entry table by
speed/64 (`$81:8010` pointers, bytes << 4, x1.5 at 150cc), written as
`A << 8` into the 32-bit accel with a STALE low byte; over it, `$80A68D`
by the excess.  B released: `$80A67D` by speed/256 (-10/frame at 900 -
the capture's -9/-10 alternation is the stale `$EC` low word).  Y held:
`$80A66D`.  On a capped surface (types 10-15, per-character caps from
`$81:8060`, e.g. Mario dust 592, deep 288, +48 at 150cc) speed above the
cap decays by `$80A65D` by speed/256: -112/frame at 5xx.  Types 0-9 are
uncapped (`$FFFF`).  So "max speed goes below and acceleration is slower"
is: coins, character, class, and the taper of the table (12 units/frame
at mid speed, 0.25 at the top).

**The slide machine `$A6`** (`$80AA18`), with a parameter row from
`$80AC36` (8 rows x 8 words: `[window, spin rate, A8 max, A8 rate, A8
decay, A8 entry, AA rate, AA max]`).  The row is chosen every frame at
`$80A3CC`: shoulder or Y held -> row 7; else `$80A4A0[type]` (road $20 ->
row 2, dust $30, ice `$00` = no slide) + `$80A4C0[character]` (Yoshi,
Koopa -$10).

    state 0   armed.  A slide can start when speed is within row[0] of
              $B4 (road: 32 units; 150cc adds $120 to the speed first)
              and |$B2| >= $300, or whenever speed >= $B4.  -> state 2.
    state 2   sliding, steering held (B, or a shoulder above $1C0):
              $A8 -> +-row[2] at row[5] then row[3] per frame (velocity
              lags the heading: road 13.5 deg, power slide 18 deg, and
              5x faster);  $AA -> +-row[7] at row[6] per frame (the pose
              turns into the slide: road 46 deg, power slide 57 deg);
              |$AA| >= $C00 / $1800 set $E2 bits 2 / 5 - the drift
              sprites.  The spin accumulator $FA moves by row[1] per
              frame while the velocity lag has the turn's sign (road
              $100: 122 frames to spin; power slide $E0: 139), and by
              the stale scratch $08 in the other phase.  Released:
              $FA drains by row[1]+$E1, $A8/$AA decay at row[4]/row[6].
    state $0E/$10  SPIN-OUT ($FA past +$7A00 / below -$8600): $E2 bit 3,
              $AA spins +-$480 per frame, speed -16 per frame, steering
              still works, until speed < $180 and the pose wraps through
              zero.  -> $1C.
    state $1C settle: row-0 decay, and the drive path accelerates from
              the table regardless of B.  -> 0 when $A8 is spent.
    state $12 the reward: holding shoulder + steer for 128 frames sets
              $E2 bit 6; when the slide is spent, 48 frames at +2/frame
              up to $D6 + $C0.  Small - "the power slide does not make
              you considerable speed".

**The hop** (`$80B49D`): a FRESH L or R press with the other shoulder
free; `$26 = $E0`, gravity `$1A`, 17 frames in the air; on a capped
surface at or over its threshold it costs $40 of speed.  The hop itself
changes nothing in the slide machine - what the player feels as "jump into
the slide" is the shoulder being HELD, which selects row 7 and the faster
steering row for as long as it is down.

**Order within the frame** matters for exactness: motion (`$80A4D0`),
jump machine, heading, then control - and the jump machine and the
low-speed heading branch read the pad word `$C4` composed at the END of
the previous frame, so a hop lands one frame after the press.

**Ported** as `src/player.c` (`smk_player_setup/reset/step`), tables read
from the ROM at setup; `step_kart` in main.c now only translates SDL input
into the SNES pad word.  The old feel model (breakaway, plow, grip table,
slip ceiling) is deleted.  Camera = heading + $C0.  Sprite pose = `$2A`
relative to the camera, i.e. -($AA + $C0), fed to the NOTES 080 pose bands
(which were measured in exactly those units: $21C0 = row 2's $2100 + $C0).

**Not yet done / labelled:** coins are not collected in the port
(`coins = 0`, so the top is `$B4`); items, the `$60`/`$84` boost states and
the water/lava landings are skipped; the steering lean of the sprite when
turning without a slide is still the synthesised lag; the mini-boost
state $12 and the shoulder-held long-hold are transcribed but not observed.
MAME notes: Lua taps are blind to bank-$7E accesses, `bpset` never fires,
debugger `wpset` needs the exact bank (`00`/`7e`/`81`).

---

**107** — The demo race replayed through the port: an end-to-end accuracy
gate, and the three things it caught.

`tools/demoreplay.c` sets the port up from the attract race's first moving
frame (track 7, Mario/Toad, 100cc, coins from the log since the port does
not collect them yet) and drives it with the recorded pad words only,
comparing position, heading and speed with the game every frame and
resyncing from the log whenever the position error passes 4 px - so each
divergence is located and described.  Part of `make check`.

Before any fix P1 needed 21 resyncs; now (P1 / P2):

    within 1 px   93.0% / 95%+     mean error 0.20 px
    within 4 px   99.4% / 99.9%    longest clean run 930 / 902 frames
    resyncs       7 (mushroom x6, one collision) / 1

What the replay found, none of it visible in the field-level replays:

1. **Surface type comes only from driveable classes.**  `$80B3B7` updates
   `$B0` for classes >= `$40`; `$20-$3F` are the wall/hazard handlers and
   `$00-$1F` the object classes (item box `$14`, coin `$16`, `$1A` a no-op).
   Our `(s>>1)&$F` turned a one-frame `$1A` under the kart into "type 13"
   and applied the off-road bite; the game applies nothing.
2. **The DSP-1's sine is table arithmetic**, not a floating sine.  Fitted
   against all 2381 velocity samples of the race: 256-entry 1.15 tables,
   slope interpolation, negative angles by symmetry, `$7FFF` clamp, floored
   radius product - 2317 bit-exact, the rest +-1 (the `dsp1.bin` dump's
   own table does worse with that interpolation, so the exact microcode
   remains open; LABELLED).  A double sin/cos matched 22%.
   `smk_dsp_sincos` in player.c.
3. **Position integrates with the PREVIOUS frame's velocity**:
   `pos(N+1) = pos(N) + v(N)` - `$80879D` runs before the kart loop
   recomputes `$22/$24`.  Moving with the new velocity crept 0.08 px per
   frame with identical velocities.

Also: seeding the resync's accel from `$EE` removed a 2.5 px offset that
came from starting one frame late off the grid.  The residual after all
this is positional - a 2 px offset can put the port on an edge tile with
a cap for a few frames (seen after the mushroom) - and the +-1 sine.

---

**108** — The mushroom, and the replay is now exact but for one collision.

The demo's P1 uses an item mushroom at frame 1465 (`$E0` bit 15, consumed
inside the frame, road under the kart - not a zipper).  `$80B46B/$80B47C/
$80B489`: the velocity lag `$A8` is zeroed and the slide state set to
`$1C`, `$FC = $20`, `$E2 |= $80`, `$AC = $10`.  The player's drive table
`$A53B` sends `$AC = $10` to `$80A5E3`: count `$FC` down, accel `+$32`
per frame up to `$7E0`, then `$80A5FC` clears `$E2` bits 6-7 and `$AC`.
(The AI's `$AD76` table routes `$10` to `$80B015`, which adds a "sector
speed row == 3" test - I ported that first and the boost died in a row-2
sector; the player has no such test.)  Refused in the spin states
(`$809E0B`).  Ported as `smk_player_boost`; the replays fire it on the
frame the log's `$AC` turns `$10`, since the item use itself is input the
port cannot see yet.

Demo replay after this (tools/demoreplay.c, in `make check`):

    P1  99.8% within 1 px, mean 0.04 px, one divergence (frame 1736: a
        kart-to-kart hit, speed 735 -> 384 in one frame; the demo's AI
        karts are not in the port), clean run 1197 frames
    P2  100% within 1 px for all 1240 frames, mean 0.01 px

The game's own `--replay` shows the same with the real kart as a ghost.

---

**109** — Tyre smoke and dust: the ground-effect object, decoded and ported.

Playtest: "when the kart slides at that angle there is smoke from the
wheels, and smoke off-road".  Found by dumping the game's OAM shadow
(WRAM `$0200`, DMA'd each frame) during the demo's slide and diffing it
against straight driving: six 16x16 sprites, tiles `$100/$102/$104`
(small/medium/large puff) in two mirrored groups at fixed offsets from the
kart sprite, cycling on the frame counter.

The mechanism (`$80CF7B..$80D4A3`, one effect OBJECT per player at
`$1E00/$1E20`): every frame a grounded kart dispatches on the surface
class under it through the jump table at `$80D31A`.  The road handler
(`$80D37A`, classes `$00-$1E` and `$40-$52`) shows kind `$24` only while
**`$E2` bit 5** is set (|`$AA`| >= `$1800`, the deep-drift stage - the
boost frames with `$AA` = 2560 show none), kind `$18` while spinning
(`$80D44E`: `$E4` >= `$400` or `$E2` bit 3).  The dust handler (`$80D3B6`,
classes `$54-$58`) shows kind `$2A` when deep-drifting, kind `$00` at any
speed >= `$80`, `$1E` spinning.  Airborne or z >= `$200`: off.  Snow,
splash and water classes use other template blocks - not ported yet.

A kind is a record at `$80D1CE` [template block, script list, XOR]: the
list has one animation script per KART SPRITE FRAME (`$BC` -> `$1E3C`
through `$80CF2F`), so the puffs sit under the wheels of the pose drawn;
scripts at `$80D030` are `[duration, template]` pairs looping via `$80 lo
hi` (the interpreter at `$80D530` shows entry 1 first, then 2, 3, 0...);
templates are `[count][x, y, tile, attr]` OAM entries relative to the
kart sprite's top-left + (0, 16), the group mirrored with the sprite (x ^
`$FF`, attr ^ `$40`, `$80BFC8`) and XORed with the record's flags: `$05`
gives palette 5 (white/grey) on road, `$01` palette 7 (tan) on dust - the
same templates and tiles.  X wobbles by `$80D46F[frame & 7]` = 0,1,2,1,0,
-1,-2,-1.

Sources: templates from the stream at `$C5:EE00` (decompresses to WRAM
`$2000`; the block address is an offset into it), puff tiles from
`$C4:9C1A` (subtiles for VRAM `$101..` at 20 + 32k; VRAM `$100` is the 12
bytes before plus the stream's header - the game really shows that; VRAM
`$110` is subtile 15 with its two junk rows masked, LABELLED: one more
pixel differs from a source no stream contains).  Confirmed off-road with
the Python oracle: same tiles, palette 7.

Ported as `src/effects.c`, drawn after the player sprite from its anchor;
verified on the demo replay (smoke exactly over the game's `$E2 = $24`
frames) and by eye (`SMK_REPLAY_SHOT=1130:out.ppm`).  Labelled: the
drift-onset sheet frame 47 counts as `$BC` band 1 for the template choice.

---

**110** — Coins and item boxes, decoded and ported; and the surface lookup
had been one row off all along.

The collector (`$81B73B..$81B7D6`) runs once per frame for ONE human
player, alternating - every P1 pickup in the demo lands on an odd frame,
every P2 pickup on an even one - after the position integration and before
the kart update, so it sees the new cell with the previous height (`$1F`
== 0; a coin on the hop's launch frame still counts, demo frame 919).
It reads the CLASS of the cell under the kart (`$68,x`, from `$58,x`):

    $1A  coin:  $0E00,y += 1 wrapping at 100 (`cmp #$64 / lda #0`), the
                cell rewritten with the theme's erase tile ($81:8BBD by
                theme), $0FC0,y = 1 for the sound
    $14  box:   if no item is running, the roulette starts ($0D70,y =
                $A000) and the 2x2 stamp becomes the "used box" tiles:
                tile & 3 is the quadrant, $81B723 corrects to the top-left
                cell, tiles $81B72B + (tile & $C) - $D0.., road class, so
                a used box is inert until the game respawns it

Coins are tile `$FE` / class `$1A` (the `$16` band is something else).  No
code in banks $80-$85 ever decrements `$0E00` through any addressing form
- coin loss on a hit is elsewhere or not what it looks like; LABELLED with
the box respawn and the roulette.  Starting coins come from `$81E3DA` by
the kart's `$E6` (2,2,3,3,4,4,5,5; the demo starts at 5): the port starts
with 2, labelled.

**The lookup rule.**  Matching the game's coin count frame for frame
failed on a coin one row up until the game's own `$58` was logged: at
y = 232.76 the game is on row 28, not 29.  `$80FA62` computes the row from
**y - 1** (`lda $1C,x / dec A` before the shift), the column from x as is
- and a kart 4 px or more up reads plain road (`$40`).  `smk_track_surface`
claimed to mirror that routine and lacked the `dec`; fixed for everything
that uses it (walls, caps, effects, pickups).  The demo replay's position
score did not move.

Result: `tools/demoreplay` now checks the coin count too (the other
player's pickups are applied from its own log, since it takes coins off
the same map): 0 mismatches on both karts, and the gate requires that.
The HUD shows the real count.

---

**111** — The starting grid's characters: the ROM's per-character order
table.  And the karts are drawn far-to-near now.

Playtest: AI karts rendered on top of each other.  Two causes.  The port
put character i in slot i, so with any player but Mario an AI copy of the
player shared its slot; and the karts were drawn in index order with no
depth sort.

The game (`$81EE07..$81EE58`): a table of eight 16-byte rows at
`$81:EE97`, one per character, each a list of eight characters.  The row
is P1's in 1P mode and P2's in 2P (`$81EE72` by `$2E`); in 1P mode the
row's 7th entry becomes kart `$1100` - the designated RIVAL (`$81EE78`)
- and then karts `$1700` down to `$1200` take the row's entries in order,
skipping the two humans' characters.  Verified: the demo (2P, Mario and
Toad) fills Luigi, Koopa, Bowser, Peach, DK, Yoshi from `$1200` up, which
is Toad's row minus the humans.  Mario's row: DK, Peach, Toad, Luigi,
Koopa, Bowser, Yoshi, Mario - so a 1P Mario race has Yoshi as rival in
slot 1 and DK at the back.  The eight-kart log (`allkarts.csv`) also
shows the grid POSITIONS come from a per-track record (`[$0C],y` at
`$819207`, cell + 4/10 px offsets) - S2, decoded in location, not ported.

Ported: `smk_grid_order`, `smk_racer.character`, sheets by character,
painter's order in `draw_scene`.  Selftest pins the demo's row.

---

**112** — Kart-to-kart collision: not in the data.  `$42,x` is the rank
animation timer.  The demo replay is now exact end to end.

Hunting the last divergence (P1, frame 1736) as a kart bump found three
things instead:

* **`$42,x` is not a knockback window.**  Its only writer is `$84EF20`,
  the HUD position display: when a kart's RANK (`$E6,y`) differs from the
  last shown (`$40,y`) the number animates up (`$84D9AB`) or down
  (`$84D98D`) and `$42` counts 10 frames.  NOTES 071/092 read the same
  countdown during wall hits as a ballistic window; the hit changed the
  rank.  The measured ~9-frame velocity freeze (NOTES 092) stands on its
  own displacement data and stays as `bounce_cool`, LABELLED without the
  `$42` attribution.
* **No kart-to-kart response exists in the demo.**  The eight-kart log
  (`allkarts.csv`) has two AI karts passing within 3 px of each other and
  P2 within 7 px of an AI kart; no velocity, flag or position reacts, and
  the P1/P2 replays are exact through those frames.  Whatever contact
  response the game has (star, battle mode?) does not fire here; nothing
  to port from this evidence.
* **The frame-1736 divergence was ours**: track 7's entity 3 (a pipe)
  stands at (252,124), 5 px from P1's line, and the port's cylinder
  bounced the kart - but the attract race never spawns entities
  (NOTES 105), so the game drove straight through.  The replay tool no
  longer collides with entities; the gate now requires zero resyncs.

    P1  1208/1208 frames within 1 px, mean 0.03 px, max 0.1 px
    P2  1240/1240 frames within 1 px, mean 0.01 px
    coins exact on both, no resync anywhere

---

**113** — The other attract demos, and the hazard classes: water, the fall,
Lakitu's rescue.

The attract loop runs more than one race (the user's hint).  Logged with
`tools/labs/mame/multidemo.lua`: demo 1 is the 2P Mario/Toad GP race on
track 7 (`$2C = 0`), demo 2 is DK alone on track 19 in TIME TRIAL
(`$2C = 4`), demo 3 is Peach and Yoshi on track 18 (`$2C = 0`, `$2E = 2`).
So `$2C` is the game mode - 0 GP, 2 match race, 4 time trial, 6 battle
(`$85:85C0` writes it from the menu) - and in time trial the game places
**no coins and no item boxes**: the live tilemap has the theme's erase
tile where GP has them.  The replay tool strips them for a `$2C = 4` log.

**The hazard dispatch** (`$80B3F1`): class >= `$80` is a wall; `>= $40`
just sets the type `$B0 = class & $1E`; `< $20` goes through the object
table `$80B3A5`; `$20-$3E` through the hazard table `$80B39B` on
`class & $F`:

    $22 water   $80B56D: at speed >= $200 the kart SKIMS - speed loses
                $2C0 (>= $400) or $A0, $E2 bit 4, a $0800 launch - and
                below that it falls in ($80B5EC): everything zeroed,
                $CA = $102, state $A0 = $AC = 8.  In the water
                ($80B24D + drive state $80A5AD) B held under $7C
                accelerates by ONE per frame and anything else
                decelerates by one, so the kart wades at 123/124 until
                $CA runs out or the class changes, then it is launched
                out at $3E00 with speed $100.  MEASURED both ways by
                teleporting the demo kart onto class $22 at speed 100
                and 700 (the skim bounced twice, 700 -> 531 -> 363,
                before the wade).
    $24/$26     the fall: speed zeroed, $D4 flags, $AC/$A0 = 6 / $0A ->
                the rescue chain $A0 = $0A ($80B231, the sink counter
                $20) -> $0C ($80B2B6, carried 2 px per frame toward the
                kart's waypoint $CC/$CE) -> $0E ($80B32E, $1F down by
                $80 per frame) -> control.  Measured end to end with
                $2C forced to 6 on Ghost Valley: 106 frames down, ~90
                carrying, ~20 descending.
    $2A/$2C     the bump and the launch, as the object classes.

Ported into `src/player.c` (`smk_player.hazard`), the rescue target
supplied by the caller from the course's waypoint.  LABELLED: the sink
counter `$20` and the splash flags (`$10` bit 8, `$D4` bit 10) are not
modelled, so the rescue's segment lengths are the measured ones rather
than the ROM's own animation.

**Replays after this** - three of the four demos are exact end to end:

    track  7 Mario 1208/1208 within 1 px   Toad 1240/1240
    track 19 DK    1233/1233 within 1 px  (time trial, no coins)
    track 18 Peach  963/1223 (78.7%), mean 0.67 px - OPEN

Peach's residual: her `$A8` decays to 0 in the port where the game keeps
96, from about frame 1000, and the heading is then 17 units off, which
puts the port on the mud-jump class ($12/$1C) a frame early.  The drift
row selection (`$80A4A0[$B0]` + `$80A4C0[character]`) reads the same on
both sides, so the difference is in the decay itself - not chased yet.

**Also decoded here**: item boxes are not consumed while an item is held
(`$81B75D` tests `$0D70,y`), which is why the port collected boxes the
demo ignored; and `$80B79E`'s ramp clamp (mode `$0126 == $0C` floors the
speed at $400, everything else at $2E0).

---

**114** — The sky is the backdrop colour and the plane repeats character 0.
The black void was ours.

Captured a real race frame from MAME headlessly (`screen:pixels()` into a
PPM, `tools/labs/mame/pix.lua`) and read the PPU setup:

* **`M7SEL = $80`** at race init (`$84FF67`), i.e. screen-over `10`:
  outside the 128x128 Mode 7 map the PPU **repeats character 0**, it does
  not go transparent.  That is why the original shows ground all the way
  to the horizon where we drew a dark void.  The per-frame value comes
  from the WRAM shadows `$D4`/`$D6` (`$808ACC`/`$808B4F`), so the split
  screen can use a different setting per field.
* **The sky band is CGRAM[0]** - the backdrop colour, palette entry 0.
  On Mario Circuit the captured band is `(255,239,148)`, which is the
  track palette's entry 0 (`$4BBF`) as MAME scales it.  Our vertical
  gradient from entries 1-2 was invented (ledger S5).

Ported: `smk_track_texel` fills outside the plane from tile 0's own 8x8
pixels (the PPU's fill, and it tiles the same way), and the sky rows take
`palette[0]`.  The port's frame now matches the game's above and below
the horizon except for one thing.

**Still missing (S5): the horizon ART.**  The captured frame has a green
hill silhouette over the sand band, scrolling with the camera - a
separate layer, not the Mode 7 plane.  Mode 7 has no second BG, so the
game must switch BG mode mid-screen by HDMA on `$2105` (there are four
`sta $2105` sites, `$84F45E` and three in bank `$85`) and draw the hills
as ordinary tiles above the split.  CPU write taps see none of it during
a race, so the next step is to reconstruct VRAM from the DMA stream at
race setup and find the tilemap the top of the screen reads.

**The split, read from the live HDMA channels** (`tools/labs/mame/hdma.lua`,
reading `$43x0-$43xA` at a race frame):

    ch5 -> $2105 (BGMODE)  table $00:0674 = 18 00 | 58 07 | 18 00 | 58 07 | 00
    ch6 -> $212C (TM)      table $00:067D = 18 1e | 58 11 | 18 1e | 58 11 | 00
    ch1-4 -> $211B-$211E   the Mode 7 matrix, per scanline (NOTES 014)
    ch7 -> $2126 (window)  from $7F:E500

So each half of the split screen is **24 scanlines of BG MODE 0** showing
BG2+BG3+BG4+OBJ (`$1E`), then **88 scanlines of mode 7** showing BG1+OBJ
(`$11`).  The horizon art is ordinary 2bpp tiles on those three layers.
Their bases at race time (`bgbase.lua`): BG1SC `$10`, BG2SC `$15` (64x32
at word `$1400`), BG3SC `$1C`, BG4SC `$7B`, BG12NBA `$00`, BG34NBA `$22`;
BG2's horizontal scroll is written every frame from `$8580AC`.

Open: a VRAM shadow built from the write/DMA stream
(`tools/labs/mame/vshadow.lua`) captures only one 1 KB upload, so the
game's bulk VRAM traffic uses a path the `$420B` tap does not see - find
that first, then the sky tilemap can be read straight out of it.

---

**115** — The horizon ART found; the map is the one piece left.  And how
MAME's Lua taps really behave.

**The art.**  Asset table `gfx_d` (`$81EBEB`) has **eight entries, one per
theme**, 2bpp: rendered, they are unmistakably the horizon scenery -
hills and trees, clouds, mountains, castle battlements, ice shapes, and a
star field.  288-1804 bytes each (18-112 tiles).  That is the layer the
captured frame draws over the sand band.

**The upload path**, from the ROM rather than the emulator: `$81EA39` -
`$81EAE6` fills exactly the race's sky addresses from decompressed WRAM
staging - VMADD `$5000`/`$6000` from `$7F:6C00` (1 KB each), `$5200`/
`$6200` from `$7F:C800`, and the **BG2 tilemap at VMADD `$6400` from
`$7F:CC80`, `$280` = 640 bytes** (320 entries).  The race's layer bases
come from `$84FF44`: BG2 map word `$6400`, BG2 chars `$6000`, BG3 map
`$6C00`, BG3 chars `$7000`.  What is NOT yet pinned is which asset feeds
`$7F:CC80` for a RACE (the routine above may be the menu's - its sources
are fixed, not theme-indexed) and which CGRAM block the 2bpp tiles use.
That is the whole remaining gap: art yes, arrangement no.

**MAME Lua taps - the rules, learned by measurement** (they cost most of
this session, so they are written down):

* **Install taps on ONE bank only.**  With taps on both `$00` and `$80`
  for the same register the capture collapsed from 11777 VRAM word
  writes to 512; with taps on all 128 mirror banks it collapsed to
  nothing.  The mirrors are the same physical registers, and MAME does
  not merge them sanely.
* **Bank `$00` is where SMK writes the PPU ports** - `$2118`/`$2119`
  saw 14976 writes each there and none in `$80`.
* A tap callback must **return the data** it was handed.
* Even then the shadow is incomplete (11777 of the ~16384 words the
  track needs), so a VRAM reconstruction is not trustworthy; the
  debugger (`wpset` + `printf`) remains the reliable observer, and the
  ROM-side reading above is better still.

Shipping meanwhile: the flat backdrop colour and the character-0 ground
fill from NOTES 114, which is what removed the black void.

---

**116** — The background's shape, from the user's reference shots, and the
first byte-exact link: BG3's characters ARE the theme's `gfx_d`.

The reference frames (Ghost Valley, Bowser Castle) show what the sky band
really contains, and it is three things, not one:

* a **gradient sky** - navy shading on Ghost Valley, orange on Bowser
  Castle;
* a **far plane** of black silhouettes - hills/trees, pyramids;
* a **near plane** that scrolls faster - the ghosts, the castle arches.

That matches the mode-0 split of NOTES 114: the 24 scanlines above the
horizon show BG2 + BG3 + BG4, i.e. two scenery layers (parallax by their
own scroll registers) plus the HUD, over a gradient.

**Proved this session**: the oracle's VRAM (tools/labs, the Python CPU)
is complete where MAME's tap-based shadow was not, and rendering it with
the RACE bases from `$84FF44` (BG2 map word `$6400`, chars `$6000`; BG3
map `$6C00`, chars `$7000`) shows the HUD on BG2 - and the bytes at BG3's
character base `$7000` are **byte-for-byte `gfx_d[theme]`** (matched
against every decompressed asset; theme 1 -> `gfx_d[1]`, offset 0).  So
the far plane's art is settled: per-theme `gfx_d`, 2bpp, at BG3's chars.

**Still open**: the tilemap that arranges them.  The bytes at BG3's map
base look like HUD text rather than scenery indices, so either the base
captured is stale or the horizon map is uploaded elsewhere; and the
gradient's source is unidentified (it is NOT an HDMA CGRAM write - the
race enables channels 1-7 only: M7A-D, BGMODE, TM, window).

Next concrete step, in the oracle rather than MAME: dump VRAM at a race
on Ghost Valley (the demo on track 19 is a Time Trial there), scan every
1 KB-aligned map base for entries that index into `gfx_d`'s tile range,
and read the scroll registers per frame to get the two planes' speeds.

---

**117** — The horizon layer, decoded and drawn: `gfx_d` are the tiles,
`gfx_e` is the MAP.

The scan NOTES 116 called for, run against the oracle's VRAM (which is
complete where MAME's tap shadow was not): of every 1 KB-aligned map base,
word **`$7800`** has 100% of its non-zero entries indexing inside the
theme's `gfx_d` tile range.  Rendered with `gfx_d[1]` as characters it is
the Mario Circuit horizon - the row of trees in the captured frame.

Then the map itself, matched byte-for-byte: a distinctive 64-byte slice of
that VRAM map is **`gfx_e[theme]` at the same offset**.  So the pair is

    gfx_d[theme]  ($81EBEB)  the tiles, 2bpp        -> VRAM word $7000
    gfx_e[theme]  ($81EC03)  the map, 32 x 24       -> VRAM word $7800

which is why `gfx_e` had exactly eight entries of exactly 1536 bytes: 768
map entries, one theme each.  Rendering it as TILES is what hid it - it is
a tilemap, and as tiles it looks like noise.

Ported as `src/horizon.c` + `smk_render_set_horizon`: the sky band is
filled with the backdrop colour and the layer is drawn over it, colour 0
transparent, palette block 64 (mode 0's BG3), scrolled horizontally with
the camera.  Mario Circuit now shows its trees, Choco Island its rock
spires.

**Labelled, not measured:** the horizontal scroll law (we turn the
panorama once per full camera turn - the natural reading of a 32-tile map
on a 256 px screen) and which rows of the 24 the band shows (we take the
top rows, where every theme's scenery sits; the game picks them with the
layer's vertical scroll).  **Still missing:** the NEAR plane - the ghosts
and castle arches of the user's reference shots, a second layer with its
own faster scroll - and the sky GRADIENT (navy on Ghost Valley, orange on
Bowser Castle); ours is the flat backdrop colour.

---

**118** — What the horizon layer really is: the NEAR plane.  Ghosts on
Ghost Valley, arches on Bowser Castle.

Rendering `gfx_e[theme]`'s full 12 used rows with `gfx_d[theme]` as
characters, in each theme's own CGRAM, settles what that pair is: on
Ghost Valley the map is **twelve rows of ghosts**, on Bowser Castle
**three stacked copies of an arcade of arches**, on Mario Circuit trees,
on Choco Island rock spires.  That is the user's NEAR plane - the layer
that scrolls faster - not the silhouettes behind it.

Verified against real races on those tracks, reached in the oracle by
hooking the reads of `$0150`/`$0152` so mode entry computes `$0124` and
the theme itself (`$81EC1B[cup*5 + course]`, `$81EC2F[track]`; forcing
`$0124` alone is the NOTES 059 trap): on both tracks `gfx_d` lands at
VRAM word `$7000` and `gfx_e` at word `$7800`, exactly as on Mario
Circuit, and the map is uploaded twice so the panorama repeats.  The
port now draws it for every theme (`--shot` too).

**Still missing, and where it is NOT**: the far silhouettes (black hills,
pyramids) and the sky gradient are not in `gfx_d`/`gfx_e`, and a 2bpp
sweep of the whole Ghost Valley race VRAM does not show them either.
The pre-race registers have BG2, BG3 and BG4 all pointing at the same
character base (`$7000`) and map base (`$7800`) with different scroll
registers, so the next measurement is the race-time BG registers
sampled per frame in a forced GP race: whatever separates the three
layers is in those, and the gradient with it.

---

**119** — Going off track: the void is a FALL, not a wall.  The port had an
invisible barrier where the game drops you.

Measured with the NOTES 066 technique - swap the live class table
(`$0B00`) under a kart that is already lapping, so it meets the class at
speed instead of being placed inside it:

    class $20  ->  speed to 0, $A0 walks $0A -> $0C -> $0E, z climbs to
                   12288 and is lowered 512 a frame: the FALL and Lakitu's
                   rescue.  This is Ghost Valley's and Rainbow Road's
                   surround (track 1: 10371 cells, track 5: 10765).
    class $26  ->  the same chain, z 1792, $AC = $10, $D4 bit 5.  This is
                   the water on the ice tracks (track 12: 3738 cells) and
                   the drop beyond Mario Circuit's grass.

So `$20-$3E` are hazards, not barriers.  Our `smk_surface_solid` tested
bit 5 as well as bit 7, which put a wall around every void and lake in the
game - and it meant the hazard states decoded in NOTES 113 could never
fire for the player, because the kart bounced off the water before
entering it.  Fixed: **only bit 7 blocks** (the barrier classes NOTES
044/088 measured head-on), and class `$20` joins `$24`/`$26` in the fall
handler.  All gates stay green, including the AI's 20/20 laps, so nothing
depended on the old reading.

With that, three of the four behaviours the user asked for are in and
measured: Rainbow Road and Ghost Valley drop you and Lakitu returns you
to your waypoint; the beach and ice water is the `$22` wade - speed
capped at 123/124, one unit of acceleration a frame - which after its
`$102`-frame timer hands you to Lakitu if you have not driven out
(NOTES 113).

**Not found yet: the breakable blocks** (Ghost Valley's and Vanilla
Lake's, one hit and gone).  They are not a plain surface class: painting
`$80`, `$82` and `$84` ahead of a driving kart leaves the tilemap
untouched and produces no state change.  The likely path is the stamped-
object collector - the same queue at `$7F:DF81` that item boxes use
(`$81BEE0` drains it, writing a 2x2 tile block into both the tilemap and
VRAM, and `$81B762` is the only producer found so far) - or the sprite-
object collision at `$80F897`.  Next: reach a Ghost Valley race and drive
into a block with the kart under our own control, watching `$1EB4` and
the cells around it.

---

**120** — The fall, measured properly: 60 frames down, then Lakitu's carry
to the kart's own waypoint, facing the way the track goes.  And Rainbow
Road's edge is a fall too.

Class-swap captures (the NOTES 066 rig) on classes `$20`, `$26` and `$28`:

    $20, $28   $A0 = $AC = 4 and $CA = 60: sixty frames with the kart
               frozen and the speed at zero - the fall itself - then
               $1F = $3000 and $A0 = $0C.
    $0C        Lakitu carries: 2 px a frame toward $CC/$CE.
    $0E        $1F down by $80 a frame (12288 -> 0, 96 frames), control back.

**`$28` is a fall**, which answers the playtest: Rainbow Road's edge class
was doing nothing in the port, so a kart could sit completely off the road
without dropping.  It now falls like `$20`.

**Where Lakitu puts you** (`$80B373`, the piece that was wrong): the target
is the kart's OWN waypoint - `$0900[$C0]`, `$0A00[$C0]`, the last sector it
legitimately reached - not the cell it fell on, and the heading is the
**flow-field direction at that waypoint** (`$7F:3FFF` indexed by the
waypoint's cell), which `$80B346` turns the kart toward at `$140` a frame
during the carry.  Ported: the caller passes the tracked sector's waypoint
and `course->flow` at that cell.

The drop is now visible - z is lowered during the 60 frames - LABELLED,
because the ROM draws the fall from the sprite state and leaves `$1F` at 1.

**Blocks, still open, but with a lead.**  They are not tilemap cells: a
900-frame Ghost Valley race changes exactly one cell, a scratch byte far
from any kart.  But Ghost Valley's object list has 35 entries including
kinds `$EC` and `$F0`, and Vanilla Lake has **8 entities of kind `$00`**
plus a `$DC` - and those are the counts and places the blocks occupy.  So
the breakable blocks are sprite OBJECTS, and the next measurement is to
drive into one in the oracle and watch its object block at `$1800+` for
the despawn, rather than watching the tilemap.

---

**121** — Breakable blocks: four negative results, and what they rule out.

Chasing Ghost Valley's and Vanilla Lake's one-hit blocks, with a rig that
finally drives a REAL kart into a chosen surface class on a chosen track:
force `$0150`/`$0152` for the course, run 600 frames so the countdown
finishes and the field is moving, pick a cell of the wanted class that has
four cells of road south of it, drop the kart 28 px short of it at speed
`$300` heading north, hold B, and watch every byte of the tilemap.

    Ghost Valley $82 (602 cells, the rails)   contact, $10 = $C000, speed
                                              falls 774 -> 600.  Tilemap
                                              unchanged.
    Ghost Valley $1E (32 cells)               a BUMP: $A0 = 2, $E2 bit 15,
                                              the kart hops ($80B69D).
                                              Tilemap unchanged.
    Vanilla Lake $80 (1168 cells)             contact, $10 = $C000.
                                              Tilemap unchanged.
    Vanilla Lake $84 (30 cells)               no cell has a run-up; not
                                              reachable head-on.

And the objects are not blocks either: rendering what each object kind
stamps on Ghost Valley gives kind `$03` -> tiles `$CC-$CF` class `$14`
(item boxes), `$EC`/`$F0` -> tile `$FE` class `$1A` (coins), `$54` ->
`$F4-$F6` class `$10` (a ramp).  No obstacle kinds at all.

So the blocks are static tilemap features of class `$82`/`$80`, and a
head-on hit at speed does not remove them in any state I can reach.  The
removal must be driven by the tile-change queue at `$7F:DF80` - three-byte
records `[kind][cell]` drained by `$81BEE0`, which writes the 2x2 block
into both the tilemap and VRAM - but that queue has no producer anywhere
in banks `$80-$85` under any absolute or long store form, so it is written
through a pointer.

Next, and it is a bounded job: hook the oracle's bus to log every WRITE to
`$7F:0000-$7F:1FFF` and `$7F:DF80+` with its PC while a player kart rams
`$82` blocks at a range of speeds and angles.  The first write identifies
the routine; everything else follows from it.

---

**122** — The write watch, and what it proves: ramming a wall writes
nothing.  The blocks do not break through the tilemap.

`tools/labs/blockpc.py` wraps the oracle's bus write path and logs every
write to the live tilemap (`$7F:0000-$7F:3FFF`), the class table
(`$0B00`) and the tile-change queue (`$7F:DF80+`) **with the PC that made
it**, while a real kart - race running, countdown done - is placed 30 px
short of a chosen cell and driven into it at three speeds.

Ghost Valley 1, 2 and 3 (tracks 1, 8, 16), class `$82`, 6 cells each at
`$200`/`$400`/`$600`, plus Vanilla Lake's `$80`: **not one write to the
tilemap or the class table.**  The only writes in range are `$7F:1821`
from `$80:FC69` (an object-block field) and the sprite staging at
`$7F:E500+` from `$84:EFxx` - the HUD rank code from NOTES 112.

The live entity blocks were rammed too: Ghost Valley has four of them at
runtime - (148,164) twice and (180,148) twice - which our course decode
does not produce (it reports no entities for that track), so there IS a
spawn system we have not decoded.  Ramming them changes only two
animation-looking bytes (+6/+7 and +44/+45); no block despawns.

So the one-hit blocks are not: a surface class that rewrites the tilemap
on contact, an entry in the per-track object list (Ghost Valley's are
item boxes, coins and one ramp - NOTES 121), or one of those live entity
blocks.  Whatever removes them is reached by a path the player's own
contact does not take in any state we can force.

Left for next time, in order of promise: the undecoded spawn system
behind those `$1800` blocks; battle mode (`$2C = 6`), whose arenas are
the game's other breakable-block setting; and the `$7F:DF80` queue's
producer, still unfound.

---

**123** — Breakable blocks, decoded from a session the user played.  They
turn into the VOID, which is why you fall through them.

The user recorded a Ghost Valley run with `tools/labs/mame/play.sh` and
parked save states either side of the hits.  Diffing the states settled in
seconds what days of rigs had not:

    state 1 -> 2   cell  505 (968,24)  tile $1F -> $00   class $82 -> $20
    state 3 -> 4   six cells at y=24/32, all $1F -> $00/$26/$27, $82 -> $20

Replaying the recording with a watchpoint on those cells gives the writer:
**`$80:FC69`**, with the kart touching (`$10 = $C000`, `$AC = $22`), and the
values arrive as a SEQUENCE - `$26`, `$27`, `$28`, then `$00`.  It is an
animation, and the whole mechanism reads out of it:

* `$80FADC -> $80FBBC` is the wall response, class in A: **below `$82` an
  ordinary wall** (`$84D73A` player / `$84D77A` other); `$84` arms and then
  `$84D7BA`; `$82` and above arm and then `$84D7FA`.
* `$80FBF3` arms: take the slot index `$7F:DE30`; if that slot's counter
  `$7F:DE02,x` is still running, do nothing - there are **eight slots**, so
  only eight blocks crumble at once - else counter = **4 for a player**,
  1 for anyone else, cell = `$02`, advance the slot by 4 (mod `$20`).
* `$80FC2C` runs once a frame and services the NEXT slot only, so a block
  takes four times eight frames to go.  It decrements the counter and
  writes the tile that index selects to VRAM and to the tilemap: theme 0
  uses `$80FC70` = `00 28 27 26`, every other theme `$80FC6C` =
  `08 7D 7C 7B` - the Vanilla Lake ice blocks.
* The last tile's class is `$20`.  **A broken block leaves a hole**, which
  is exactly why the user could then fall through.

Ported as `src/blocks.c`, armed from the wall response in `smk_kart_move`
and stepped once a frame; the selftest replays the user's own cell
(`$1F -> $26 $27 $28 $00`, ending in class `$20`).

The lesson for the ledger: two save states either side of an event, from a
human who can simply make the event happen, beat six increasingly clever
rigs.  Ask earlier.

---

**124** — Why Lakitu never put you down, and why he faced the wrong way.
Two bugs, both from data we had INVENTED where the game has its own.

*The chase.*  `$80B2B6` walks the kart 2 px a frame toward `$CC/$CE` - but
it never recomputes them.  `$B373` (the latch) runs when the fall is ARMED
(`$80B5B7`, `$80B626`, `$80B643` all `jsr $B373` first) and every frame of
the WADE, never during the carry.  The port refreshed the target every
frame from the sector under the kart, so as Lakitu carried it over the
track the waypoint moved with it and the two never met: an infinite ride.
Fixed by refreshing only outside `$A0 = 6/$0C/$0E`.

Transcribed the three states properly while there:

    $A0 = 6     fall: $CA frames, position frozen, then $1F = $3000
    $A0 = $0C   $80B2B6: turn ($B346), then walk INTEGER x 2 px toward $CC
                and RETURN - y only starts once x matches.  Not diagonal.
    $A0 = $0E   $80B32E: $B346 again, and only when it returns CARRY SET
                (`beq $80B372`, the rts after a `sec`) does $1F come down
                $80 a frame.  The kart is put down FACING the field, never
                mid-turn.

*The wrong place.*  `smk_course_load` memset the sector map to 0 - and 0
is a VALID sector.  The game prefills `$7F:5000` with `$7F`.  Measured on
the booted game (tools/labs/flowfield.py, track 7): **1412 cells at `$7F`,
78 genuinely at sector 0**.  So every off-course cell in the port read as
sector 0, and a kart that fell anywhere was carried to the START LINE.
With the `$7F` prefill our map now agrees with the game's on **all 2684
painted cells, 100%** - and the flow field, which had to skip sector 0 to
dodge the ambiguity, no longer has holes.

*The direction field.*  `$81FCFC` builds `$7F:4000` from the waypoints,
one byte per 16-px cell, through the boot-time arctangent table at
`$7F:9000` (`$81E4C5` generates it, `$81F638` reads it as octant base +
`table[min*64 + max]`).  Ours is an atan2, and now that it is asked at
every cell it can be checked against the real thing: **2554 of 2684 cells
exact, 130 off by one step of 1/256 turn, worst error 1.**  Rounding
(`+ $80`) beats truncation, measured: 95.2% against 53.3%.  LABELLED at
that number rather than claimed exact.

`$80B393` reads the field as a WORD at `$7F3FFF,x` - high byte the
waypoint's cell, low byte the cell before it.  Ported literally.

*Water, checked and left alone.*  `$80B5DC` sinks the kart outright when
`$60,x` is negative, else skims if `$EA >= $200`, losing `$2C0` above
`$400` and `$A0` below.  From a real shoreline the port already gives
sink-at-once below `$200` and 1/2/3 skips at 512/768/1024 - the stone
skipping.  `$60,x` bit 15 is the SHRUNK kart (`$80B77B` hops it $70
instead of $E0, `$80A48F` clears it and zeroes $DA/$FE): a small kart
always drowns.  Not ported - we have no lightning.  LABELLED.

**123a** — correction to 123.  "A broken block leaves a hole" is true of
GHOST VALLEY only.  Verified in the selftest across both themes:

    Ghost Valley  theme 0  tile $1F class $82 -> $26 $27 $28 $00  class $20 (void)
    Vanilla Lake  theme 4  tile $7A class $84 -> $7B $7C $7D $08  class $4E (ice)

So the ice blocks crumble away and leave ordinary ice - you do not fall
through where one stood; Vanilla Lake's holes are map features that were
always there.  The two classes also take different branches at $80FBBC
($84 -> $84D7BA, $82 and above -> $84D7FA), which is the tell.

Both themes confirmed in play by the user before this was pinned.

---

**125** — S6 closed: the wall response, measured frame by frame instead of
inferred from displacement.

The rig (`tools/labs/wall.py`) is the one that finally worked: pace the
player up on the game's own flow field, then PAINT a solid tile into the
cell it is about to enter and log the kart block while the game reacts.
Four captures at different approach angles.  One of them:

    f-1  $22=FCBF (-833)  $24=0030   $EA=0343 (835)  $10=8000
    f0   $22=FCBF          $24=002D   $EA=0343        $10=8000
    f1   $22=0341 (+833)   $24=002D   $EA=0343        $10=C000
         $52=C007  $56=0000  $5A=8001  $5C=0008   <- the impact
    f2   $22=01A0 ( 416)   $24=002A   $EA=01A2 (418)  $52=0007 $5C=0007
    f3..f9  velocity FROZEN, $5C counting down
    f10  $10=8000, control returns

**The impact frame does not touch the speed.**  835 in, 835 out.  The
component simply MIRRORS - `$80FB7D` negates `$22`, `$80FB9A` negates
`$24`, `$80FB90` both - and which one is picked comes from the tilemap
CELL DELTA (`$80FADC`: `$02` minus the previous cell `$58,x`, mapped
through `$80FB11`), with a diagonal step probing both orthogonal
neighbours for solidity (`$80FB49`, offsets `$80FB29`/`$80FB39`).  `$56`
records the push direction, `$52` = `$C007`, `$5C` = 8.

**The halving arrives a frame later, and it is per axis.**  `$80F9DF`
scales each component by the pair `$56` selects - `$80FA4A` = `80 80 F0
F0` for `$22`, `$80FA52` = `F0 F0 80 80` for `$24` - through `$80FC74`,
an arithmetic `(v * f) >> 8`.  So the REFLECTED axis keeps half and the
other keeps `$F0`/256 = 0.9375.

**And `$EA` is RE-DERIVED from the vector, not damped.**  This is the bit
two hypotheses agreed on in the first capture and only a diagonal hit
could separate:

    impact (573,-633) spd 855 -> (286,-594) spd 659   |v| = 659.3   0.5*855 = 427
    impact (879, -80) spd 883 -> (439, -75) spd 445   |v| = 445.4   0.5*883 = 441
    impact (662,-276) spd 718 -> (331,-259) spd 420   |v| = 420.3   0.5*718 = 359

All four rows are now a selftest.  Superseding NOTES 092's "the speed
exactly halves": it halves only when the hit is square, because then the
vector is the reflected axis.

Two more paths, ported and labelled:
* `$EA >= $500` at the hit: both axes by `$40`/256 = 0.25 (`$80FA33`).
* `$12,x` negative takes `$80FA06` instead - each component over `$200`
  by `$E0`/256.  Every capture had `$12` = 0, so this is the ROM's text,
  not a measurement; what selects it is unproven (it also picks
  `$84D73A` over `$84D77A`, the two hit sounds).

Not ported, logged for later: the STUCK handler.  `$5A,x` counts frames
spent inside a wall and at 8 (`$80F95F`) the game shoves the kart out
along its pose quadrant at +-`$100` (`$80F98A`/`$80F992`), and below
`$C0` on both axes it snaps to +-`$100` to unstick.  Our port prevents
entry instead, so nothing has been observed to need it - but it is the
ROM's answer to "embedded in a barrier" if that ever returns.

Confirmed in passing: `$1F`/`$26` stay 0 on a plain wall (no launch - the
hop belongs to the bit-7 bars), `$42` stays 0 (the HUD rank timer, NOTES
112), and `$80FA5A` builds its cell from **y - 1** like everything else.
