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

---

**126** — SUPERSEDED by 127.  It read `$C0,x` in `$84DCA3` as a per-object
animation clock and went looking for what decrements it.  X there is the
KART base: `$C0` is the player's WAYPOINT, the same field `$80B373` uses
for Lakitu.  Nothing animates it - the player drives it.

---

**127** — S12: the obstacles are RESPAWNED as you drive.  Decoded and
then confirmed frame by frame in the running game.

Our entity list (`$85:C800 + track*64`, NOTES 078) is real but it is not
a set of things all on the track at once.  The game keeps **two object
slots** in a one-player race and moves them around the course as the
player progresses:

    $819136   slots = 4 if $B6 (two players) else 2, addresses copied
              from $81:9194 = $1800 $1880 $1900 $1980, stride $80
    $818E7E   $0D28 = ROM[$81:8B73 + track]   picks a threshold TABLE
              $0D2C = ROM[$81:8B8C + track]   picks the list inside it
    $84DBD5   $84DB83[$0D28] -> a table of list pointers; [$0D2C] -> the
              waypoint threshold list ($84DACF and on, $FF-terminated)
    $84DBFF   y walks the list while the waypoint is still >= the entry,
              so y is the first threshold it falls short of: the SEGMENT
    $84DC17   if the segment changed, respawn: positions from the track's
              list at $84DAC5[segment] = segment * 8 bytes, one word per
              live slot, low byte the x cell, high the y cell, * 8 + 4

Measured on track 7 (`$0D28` = 2, `$0D2C` = 0, thresholds `0C 17 FF`) by
driving the game's own flow field and logging the waypoint, `$0D34` and
every object block together:

    waypoint  0..9   segment 0   (268,92) (164,132)
    waypoint 12      segment 1   (508,636) (148,676)   both JUMP
    waypoint 27      segment 2   (268,92) (164,132)    slice empty ->
                                 $84DC35 falls back to offset 0

The port reproduces all three, and `$1DA0` in the live game held exactly
`1800 1880 0000` - two slots, as `$819136` says for one player.

This also settles the Ghost Valley puzzle.  Its `$85:C800` row is **all
zeroes** and `$81:8B73` gives it `$0D28` = 0, whose `$84DB83` entry is a
null pointer - so Ghost Valley has no static obstacles at all, exactly as
our decode always said.  The four things visible there come from the
OTHER system.

*Correction, same day.*  The paragraph that stood here claimed Ghost
Valley's "four objects at runtime" come from a second, moving system.
That was wrong twice over, and the user said so: **Ghost Valley has no
moving track objects.**  The ghosts there are BACKGROUND - the near
parallax plane (S5), which the user had already described: "ghost valley
had some ghosts in the front plane and some sort of hills or
constructions in black".  Nothing on the driving surface moves.

The "shows four at runtime" line came from an earlier session's note and
was never verified; it should not have been built on.  What IS verified
is the opposite and simpler: Ghost Valley's `$85:C800` row is all zeroes
and `$81:8B73` gives it `$0D28` = 0, whose `$84DB83` entry is a null
pointer.  It has no track obstacles, static or otherwise.

*What is actually still open.*  `$84DC80`/`$84DC98` reposition the object
slots from paths in ROM - `$84DD15[$0D2C]` -> a block whose first word
points at a waypoint->keyframe index list, then the path as position
words (`$84DD1B`/`$84DDB2`/`$84DE5E`).  Block 0 drifts (924,172) (900,148)
(868,140) 4 px a keyframe.  But **`$84DC80` has no JSR or JSL anywhere in
banks $80-$87** - it is reached only by indirect dispatch, and I have not
found the dispatcher.  So what it drives, and on which tracks, is
unknown.  On track 7 it demonstrably did NOT run: positions held constant
across waypoints 0-9 and only jumped at the segment change.

Real movers to look for when this is picked up again: Bowser Castle's
Thwomps and Donut Plains' moles.  Both are more likely per-object type
handlers (a Thwomp rises and slams in place - a Z animation, not an x/y
path) than this repositioner.  Do not assume they are the same system.

---

**128** — Z order, and falling behind the track.  Three reports, two
causes, and the second one turned out to be the hardware's own trick.

*Obstacles never sorted against karts.*  `draw_scene` drew every entity,
then every kart, then the player.  The kart loop sorted itself by depth,
so "a far kart in front of a near one" could only come from the OTHER
lists: a pipe beside you drew under a kart on the far side of the track,
and obstacles drew among themselves in LIST order.  Fixed by building one
list of everything on the plane and sorting it once, farthest first - the
SNES sorts its OAM by distance for the same reason.  `draw_entity` and
`draw_ai_kart` are the old bodies, split out so the one pass can
interleave them.

*Falling.*  The kart went under the plane and kept drawing on top of it.
The SNES answer is sprite priority: a kart below the plane gets a
priority under BG1, so the track hides it - and the HOLE does not,
because Mode 7 draws colour 0 as transparent.  That only works if the
void really is colour 0, so it was worth measuring rather than assuming:

    Ghost Valley  void: 0 of 672704 pixels opaque   road: 258688 of 258688
    Rainbow Road  void: 0 of 688960 pixels opaque   road: 284288 of 284288

Exactly the split the trick needs.  The ground renderer now records one
byte a pixel - is the plane opaque here - and a kart with `z < 0` is
drawn through that mask (`smk_draw_set_clip_mask`), so it sinks into the
hole it fell through and disappears behind the track edge.  Both the
player and the AI karts use it.

The mask costs one byte per pixel and one extra store in the ground loop;
the texel lookup was split into `smk_track_texel_index` so there is no
second map read.

---

**129** — S10: the object size law, measured and then found in the ROM.
The constant was right all along; the DENOMINATOR was wrong.

NOTES 105 said an object's `+$06` is `$4200 / distance-from-the-kart` and
the port used the EUCLIDEAN distance.  A write watch on `$7E:1806` in the
oracle names the writer in one run - **`$80:C879`**, which reads the
DSP-1 projection's third output and stores it:

    $80C85A  wait DSP-1, read $6000 -> screen X ($1C, $2C,x)
    $80C86E  #$FF00 + $98 + $6000   -> screen Y ($1E, $38,x, $2E,x)
    $80C879  read $6000             -> the SCALE ($18, $06,x)
    $80C883  cmp #$0300 / bcs $80C8AB -> $30,x = $0140, parked off-screen

So `+$06` is the projection's own scale.  Fitted over 975 samples of a
driven lap on track 7:

    denominator                      constant     mean rel err
    euclid from the kart             $4E20            19.0%
    ALONG-AXIS depth from the kart   $422F             2.1%
    euclid + camera trail            $59E8            19.4%
    along-axis + camera trail        $4A9E            10.2%

`$4200 / (depth along the view axis ahead of the kart)`, and that 2.1% is
the DSP-1's own rounding.  **That error is the bug the user reported**: a
pipe BESIDE you has a small axis depth and must draw large, but its
euclidean distance stays big, so it drew small.  An earlier session tried
axis depth, watched the pipe fill the screen and reverted - the missing
half is `$80C883`: closer than `$4200/$300` = 22 px along the axis and the
sprite is parked off-screen.

Then `$84DA18` picks the DRAWING by walking `$84DA3C` = `C0 60 30 00`
against `+$06`, so the ladder in world terms is

    axis depth <  88 px   the big drawing
    88 .. 176             the middle one
    176 .. 352            the small one
    beyond 352            the loop hits the terminator

The hardware cannot scale a sprite, so each band draws at its own fixed
art size and the size POPS - which is why distant pipes had been
dwindling to nothing here instead of settling and then going.

**LABELLED, and the thing to revisit first if this looks wrong in play:**
that the terminator means NOT DRAWN is a reading of `$84DA38`
(`pla/plx/ply/rtl`, the outer exit, skipping what the caller does with the
band), not a measurement.  An attempt to confirm it through `+$30` was
inconclusive - every sample on track 7 read `$0140` whatever the scale,
so that field did not discriminate.  If far objects pop out of existence
too abruptly, clamp to the smallest drawing instead of returning.

The KART path is deliberately untouched: kart blocks carry the same
`+$06` from the same routine, but which drawing each scale picks is a
different table nobody has measured, so it keeps its own constant and
stays in the ledger.

---

**130** — Two playtest reports, both real, both with the answer in the ROM.

*"Pipes disappear when you get close - I cannot even stop near one."*
NOTES 129 read `$80C883` (`cmp #$0300 / bcs $80C8AB`, which writes `$0140`
into `$30,x`) as parking the sprite off-screen, and the port dropped any
object nearer than `$4200/$300` = 22 px along the view axis.  22 px is
about where a kart comes to rest against one, so they blinked out exactly
when you arrived.

That reading cannot be what the branch is FOR: the drawing is quantised
into bands, and band 0 is a fixed-size drawing however close you get, so
nothing needs protecting from a runaway scale.  The port now clamps the
scale into band 0 instead, and the only thing that culls a near object is
being behind the EYE - which `smk_project` already handles.  LABELLED:
what `$30,x = $0140` really means is still unsettled; a lab that logged it
on track 7 found it reading `$0140` at every scale, so it did not
discriminate and cannot be the whole story.

*"Acceleration after bouncing is too fast, so bouncing is too aggressive -
hitting a barrier had a cost."*  Right, and the capture in NOTES 125
already showed it: after the hit `$EA` sat at **418 for all eight frames**
of the window and only moved when control came back.  The port held the
VELOCITY but kept calling the accelerator, so it came off the wall already
back up to speed - bouncing was free.  Speed is now frozen for the window
too.

The rest of the cost is `$80B3DD`: while `$10` bit 12 is up, `$80A0C7`
runs before anything else and

    $80A0D4  takes the ANGLE of the bounce velocity ($81F638)
    $80A0E7  slip = that angle - the heading
    $80A0F0  $A8 (the slide's velocity lag) = slip
    $80A0F6  within 45 degrees and throttle held: $A2 = the bounce angle,
             $A6 = $1C
    $80A108  more than 45 degrees off: $AC = $A6 = $16, a drive state of
             its own
    $80A10F  $C2 >>= 1, floored at $0100

So you come out of a bounce pointing the wrong way, with the slide
machine holding a large lag to unwind.  Ported except `$C2`, which the
port has no field for - LABELLED.

Port trace of a head-on hit at 835, next to the game's own shape:

    f7   835 -> vx reflects, speed UNTOUCHED, drive $16
    f8   357 (714 * $80/256), speed re-derived from the vector
    f9-15 357 held to the end of the window

---

**131** — Reverting most of NOTES 130's bounce cost.  Correct ROM reading,
wrong thing to port, and the playtest was unambiguous:

> "totally broken bouncing dynamics.  If you bounce straight, your car
> stops after bouncing and does not accelerate any longer.  But if you
> bounce while turning or sliding, then you bounce back very aggressively
> and it doesn't stop... speeds faster than anything in game (1500+)."

Both symptoms come from the same two lines.  `$80A0E4` writes the slip
angle straight into `$A8` and, past 45 degrees, `$AC`/`$A6` = `$16`.  In
the ROM those land in a slide machine that owns `$A8` and clamps it
through the drift rows; dropped into ours from outside, `$A8` sat far past
any clamp (a head-on hit gives slip = $8000, a full 180) and drive `$16`
is a state our player has no handler for.  So a square hit left the kart
in a state that never accelerates, and an angled one fed the velocity
angle back through the reflection every frame and wound up.

What stays is the part that was MEASURED: the window holds the SPEED, not
just the velocity.  On its own that is a real cost - a head-on hit at 715
comes off the wall at 357 and cannot touch the throttle for eight frames:

    f7   715 -> vx reflects, speed untouched
    f8   357, re-derived from the damped vector
    f9-15 357 held
    f36  254 ... 322, climbing again, then another hit and another halving

The lesson for the ledger, and it is not a new one: **a decoded routine is
not a portable routine.**  `$80A0C7` is correct 65816 and reads cleanly,
but it writes into a state machine we have only partly ported, and the
gate could not catch it - the demo race never touches a wall.  When the
only proof available is a playtest, port the smallest measured piece and
leave the rest decoded in the log.

---

**132** — The crash cost, found where I should have looked first: the user
said the speed came back "magically" and that you have to EARN it.  Right,
and my own capture had the answer three lines further down than I read.

Watching `$A8` with the PC through a wall hit names every step:

    f2   $80:A108 writes $A8 = $85C1, $AC = $16   the slip at impact
    f2-9 $EE = 0, $EA = 418 held                  the window
    f10  $80:AA0D walks $A8 by +$40, $EE = FFAB   -85 A FRAME
    f11  $EA 419 -> 334
    f12  $EA -> 250
    f13  $80:AA10 clears $A8: $A6 = $1C, $AC = 0, $EE = +4
    f14+ 169, 173, 177 ... climbing back the slow way

So a crash costs speed in TWO stages and the port only had the first.
`$80A55B` is drive state `$16`:

    $80A562  $A8 zero?  -> $80A588: $A6 = $1C, $AC = 0, done
    $80A566  $EE = -1 by default
    $80A573  A = |$A8|, capped $4000 -> $3F00
    $80A57B  xba / lsr / lsr / and #$001E      the table index
    $80A582  $EE = $80A590[y]

and `$80A590` is `-4 -8 -16 -24 -36 -56 -64 -85`.  **The deceleration is
keyed to the LAG** - how far your travel is from where you point.  A square
hit takes the last entry and is punished hardest; a graze barely registers.
That is the mechanism behind "hitting a barrier had a cost", and it is a
table, not a feeling.

Ported as a contained crash state on the kart: the slip at impact, the
same table, the same `$40`-a-frame walk toward zero (`$80A9FD`/`$80AA05`).
Deliberately NOT written into `p->vlag`, `p->vel_angle`, `p->drive` or
`p->state` - that is what wrecked the dynamics in NOTES 131.

Port trace beside the game's shape:

    f7    715 -> reflects, speed untouched
    f8    357   damped once
    f8-14 357   held, throttle dead
    f16   272   the crash deceleration bites
    f20   114
    f24+  136 152 168 184 200 ... earned back at +4 a frame

**LABELLED**: the ROM runs the deceleration while `$A8 != 0` and walks it
$40 a frame, which from $85C1 would take hundreds of frames - yet the
capture exits after three.  Something recomputes `$A8` from the live
velocity in between and the watch did not catch it (no writes appear
between `$80:AA0D` and `$80:AA10`).  Until that is found the port runs the
deceleration for the three frames the capture shows.  That count is the
one fitted number here; the table, the index rule and the walk are the
ROM's.

---

**133** — The user played the crash themselves, and the recording settled
three things I could not.  Their words first:

> "the bounce is constant no matter the speed.  So if I crash at full
> speed, I get the same bounce as hitting the barrier very slowly.  It
> feels more like a push back than a real bounce."

**They are exactly right, and it is a branch I had read and skipped.**
`$80F9A7`: if BOTH velocity components are under `$C0` after the
reflection, `$80F9C1` does not damp anything - it FORCES each to `+-$100`,
sign kept.  A diagonal comes out at `|(256,256)|` = **362 whatever you
arrived at**.  In their run, frame 997:

    f996  vx  -89  vy   96   EA 130      approaching
    f997  vx  +89  vy   96   EA 130      reflected, speed untouched
    f998  vx +256  vy +256   EA 362      PUSHED OUT, harder than it hit

That is the push-back, and it is why a slow hit and a fast one feel the
same.  Ported; on the recording it moves heading errors 53 -> 37 frames
and speed errors 701 -> 662.

**Second: `$12,x` is not "is the player".**  It reads 0 for a human
player all through their run, so the human takes the same table-damping
path as everyone else and `$80FA06` belongs to something else.  NOTES 125
had that labelled as unproven; it is now disproven.

**Third: the fitted frame count is no longer fitted.**  Sweeping the crash
deceleration against their run:

    frames  0     63.4% within 1 px, mean 1.06
    frames  1     61.7%              mean 1.13
    frames  2     61.4%              mean 1.00
    frames  3     82.0%              mean 0.58     <- chosen
    frames  4     75.9%              mean 0.74

Three, which is what my own staged rig had shown.  Now it is chosen by a
human crash rather than by me.

**A negative result worth keeping.**  `$80A0EB` says a slip under 45
degrees is a graze: `$A6 = $1C`, `$AC` stays 0, no deceleration - and the
recording shows exactly that at frame 1045 (slip -1103, `$EE` = +12 all
through).  But exempting grazes in the port made things much WORSE
(heading errors 37 -> 1718).  So our slip does not match the game's on
most hits, and the reason is not yet known.  Left out, and logged.

The run is now a fourth gate (`tools/labs/mame/crash_run.csv`, 82.0%
within 1 px, 240 resyncs).  A human race is not exact - it has AI karts we
do not simulate - so its bar is its own number, which is enough to catch a
regression: the version that broke bouncing scored 63%.

---

**134** — The slip was taken a frame early, and `$81F638` quantises.

Comparing our slip against the game's `$A8` at all 15 hits in the user's
run showed the game's velocity angle `$A2` landing on values like `$2B00`
and `$2000` - **low byte zero**, which is `$81F638`'s `and #$FF00`.  But
the mismatch was bigger than quantisation: at frame 783 we had 73 degrees
and the game 60.5.

60.5 degrees is the angle of the DAMPED velocity `(249,-140)`, not the
reflected `(498,-149)`.  `$80A0C7` runs on the frame AFTER the impact, so
it sees what `$80F99A` left behind.  Corrected, and masked to the high
byte as the ROM does.

It does not move the score - the deceleration table saturates at its last
entry for any big slip, so a 13-degree error in the index changed
nothing - but the slip is now the game's number rather than one that
happened to be close.

**Still unresolved, and now with the slip ruled out.**  `$80A0EB` exempts
a slip under 45 degrees from the deceleration, and the run shows the game
doing exactly that (frame 1044: slip -1103, `$EE` = +12 throughout).
Applying the same exemption in the port costs 82.0% -> 73.4% within 1 px
and heading errors 37 -> 1718, with the slip computed either way.  So the
port is leaning on the deceleration to cover an error that is really
somewhere else, and finding that is the next thing worth doing on
bouncing.  Left out deliberately; the ROM text is in the log.

---

**135** — Lakitu's rescue, confirmed by a human falling off Ghost Valley.
NOTES 124 was decoded from the ROM and gated only by a synthetic test I
wrote myself.  The user's run proves every element of it:

    f718-719  $A0 = 04, $CA counting 2, 1          the fall
    f720      $A0 = $0C, $1F = 12288 = $3000       exactly as ported
    f720-807  x walks 959 -> 784, 2 px a frame, y UNCHANGED at 31
    f808-843  then y walks 31 -> 104, x fixed at 784
    f844      $A0 = $0E
    f844-940  $1F down by 128 = $80 a frame, 12288 -> 0
    f941      $A0 = 0, control returns

The L-shaped walk is real: **88 frames of x, then 36 of y**, never
diagonal - which is what `$80B2B6` says and what the port does.  `$A4`
turns $140 a frame toward the target and snaps at f776, and the descent is
`$80B334`'s `sbc #$0080` to the unit.  221 frames from fall to control,
and the drop point (784,104) is the waypoint.

This is the piece I most wanted an outside witness for: it was ported
from ROM text, checked against a test of my own construction, and could
have been confidently wrong.  It is not.

The run is a fifth gate (`tools/labs/mame/gv1_run.csv`): 92.0% within
1 px, 64 resyncs, mean error 0.30 px over 5661 frames - the cleanest
human run we have, and it covers eight block contacts, a fall, the
rescue, and a lap of sliding into rails afterwards.

**135a** — what the fall frames actually show, and one thing we invent.

The 45 "crash-state" divergences in the Ghost Valley gate are not crashes:
`$AC = 04` is the FALL.  Reading the frames either side:

    f659  $AE = 42 (road)  $EA = 270  pos (959,33)
    f660  $AE = 20 (void)  $EA = 0    pos (959,32)  $A0 = $04, $CA = 60
    f660-719   $1F stays 1, position FROZEN, $CA counts down
    f720  $A0 = $0C, $1F = $3000, the carry starts

Two things follow.  First, the fall arms the instant the cell under the
kart turns void and the speed goes to zero in that same frame - no
tolerance, no delay.  Second, **the kart does not move in Z while it
falls**: `$1F` sits at 1 for all 60 frames.  The drop you see is the
SPRITE, drawn by the object code; the physics just stops and waits.

Our port lowers z by `$180` a frame through the countdown so something is
seen to fall.  NOTES 120 labelled that as invented and it is now
confirmed invented - the game does not do it.  Left in (it reads better
than a kart frozen in mid-air) but the label is now a measurement, and it
matters more since sprites below the plane are clipped: our falling kart
sinks behind the track where the game's does not.

The remaining error in that gate is drift, not a rule: our kart reaches
the edge about 6 px from where theirs did, so the carry starts 6 px out
and stays there - the walk itself is exact.  Nothing to fix in the rescue.

---

**136** — Two playtest reports on the Ghost Valley rails.  One was a real
bug; the other the recording contradicts.

*"I hit some blocks twice and it completely stopped me."*  Real, and it is
the corner.  Our port refuses to ENTER a solid cell, so when both axes are
blocked it moves on neither - and a kart wedged in a corner of the rails
sits there with its velocity cycling and nowhere to put it.  The ROM has
the escape and NOTES 125 had skipped it as unnecessary:

    $80F933  bit $5A,x / bvs $80F95F        already flagged stuck
    $80F93C  $5A + 1; at EIGHT set the flag
    $80F964  eject: quadrant = $2A >> 14, then a flat +-$100 on BOTH axes
             from $80F98A = 0100 0100 FF00 FF00
                  $80F992 = FF00 0100 0100 FF00
             i.e. diagonally, in the quadrant the kart FACES

Ported with "inside a wall" read as "blocked on both axes", which is our
geometry's equivalent.  A kart that cannot move for eight frames is now
thrown out along its facing quadrant at 362, and the selftest wedges one
into a real Ghost Valley corner to prove it.

*"The blocks trigger the hit in the centre of them; in the real game it is
the side."*  The recording says otherwise.  At all EIGHT block contacts in
the user's run the kart's centre `(x, y-1)` was already INSIDE the `$82`
cell when the game registered the hit:

    f603  (960,32)   f640  (959,31)   f1405 (128,658)  f1715 (1008,781)
    f1952 (867,32)   f2359 (128,669)  f2598 (806,1010) f2677 (1008,894)

all with `$AE` = `$82`.  `$80FA5A` builds ONE cell from `(x, y-1)`: no box,
no corners, no kart extent.  If anything ours triggers EARLIER, because we
test the destination and refuse to enter while the game lets the centre
get into the cell and reflects from there.  So the difference being felt
is real but it is not centre-versus-side - the likelier candidate is that
penetration: the game's kart gets half a cell deeper before it bounces.
Not changed on a guess; wants its own test.

---

**137** — Why the Ghost Valley rails could be jumped, and the ROM's answer.

Playtest: "in ghost valley I can jump those blocks and in the real game
that was not possible."  Right, and the port was filtering the wrong
thing.  `smk_kart_move_ex` let an airborne kart through anything whose
surface TYPE was not 0 - an invention.  `$80FA5A` opens with

    lda $20,x / cmp #$0004 / bcs (skip the collision entirely)

**Height, not type.**  Above four the collision test is not run at all;
below it an airborne kart collides exactly like one on the ground.

And the threshold is placed exactly where it has to be:

    hop  ($80B77B, $E0)   peaks at 3    cannot clear a wall
    ramp (class $10)      peaks at 4    clears

so hopping over a rail is impossible and a ramp launch flies, with one
unit between them.  Both pinned in the selftest.

It also pays on the gates - and on the right track.  Ghost Valley, which
is where the rails are, goes **92.0% -> 93.0% within 1 px** with resyncs
64 -> 56; the Mario Circuit run is unchanged inside its noise (82.0% ->
81.5%, one more resync, and that run's divergences are AI karts we do not
simulate).

---

**138** — Three visual reports.  Two fixed at the root; the third is a
measurement that came up short and is logged as such.

*Ghosts of what is behind you, off the side of the track.*  `smk_project`
wrapped the camera-relative delta at half the world, and `draw_entity`
did the same for its depth.  **The world does not wrap** (NOTES 063), so a
kart 900 px behind became one 124 px in front.  The Mode 7 plane repeating
character 0 outside its 1024 px is the PPU filling the floor, not the
world being tiled - a distinction the projection had lost.  Both wraps
removed.

*Far karts garbling at one size and no other.*  The far tier draws through
`smk_draw_sprite_mini`, which samples all 32 columns 2:1 - but a MIRRORED
pose is frame 0's LEFT HALF folded (NOTES 080), so the junk right half
came with it.  It only ever showed at that tier because every other tier
goes through `smk_draw_sprite_mirror2`, which folds.  The mini path now
uses `mirror2` with its `mini` flag when the pose is mirrored.

*Pipes too small, and not growing as you approach.*  Half true, and I
could not finish the measurement.  What IS measured:

* `OBSEL` is written `$02` at every site (`$808ABF`, `$84F484`, `$84FF62`
  and friends), so this game's sprite sizes are **8x8 and 16x16** - there
  is no 32x32 object sprite.
* The theme's object sheet holds nothing bigger.  Bounding boxes of every
  2x2 base, theme 1: two descending ladders, `12x15 11x16 10x14 9x13` at
  bases 0-6 and `12x16 11x14 10x12` at 32-36, plus squat lid-shaped
  drawings at 8-14.  **The largest object drawing in the sheet is 12x16.**

So a pipe really is about half a kart's 32 px, and the "not growing" part
is the hardware: inside a band the drawing does not change size at all.

What I could NOT get: how many sprites the game puts on screen for one
pipe.  If it stacks a lid over a body the object is ~26 px, not 16.  The
rig to settle it (`tools/labs/objoam.py`) finds the object's own screen
position in `+$2C`/`+$30` and counts the OAM entries there - but on the
demo's track every live object reads `+$30` = `$0140`, parked, for the
whole lap.  Either those two objects are never drawn there, or `$30` is
not the field `$80C8AE` makes it look like.  Unresolved, and NOT doubled
on a guess.

---

**139** — The pipe is twice the size we draw it, measured against a frame
of the real game.

The user put our render beside the original, same pipe, same place.  Using
the kart's known 32 px as the ruler in the ORIGINAL frame (its red parts,
helmet to bumper, span 138 px there):

    real pipe       104 x 142 screen px   =  23 x 31 SNES px
    sheet's drawing                          12 x 16
    ratio                                    1.9 x  1.9

A clean 2x in both directions.  The old code had an `SMK_OBJ_MAG_MAX` of 2
taken from a reference screenshot; NOTES 129's band rewrite dropped it,
which is exactly when "pipes are not growing as I get closer" appeared.
Restored, and now it is a measurement with a number rather than a
recollection.

**Where the bigger art comes from is NOT known.**  Rendered whole, the
theme's object sheet is 16 tiles wide and 57 tiles long, and every drawing
in it fits a 2x2 block - 16 x 16 px at most.  The bottom rows hold
complete pipes (lid over body, 12 x 16 at base 32, then 11 x 14 and
10 x 12), the top rows hold separate lid and body pieces.  Nothing in it
is 24 x 32.  So either the near band's art lives somewhere we have not
looked, or the game composes it - the same open question as the kart
minifier in NOTES 076, pointing the other way.

Labelled accordingly: the SIZE is measured, the MECHANISM is not, and the
port magnifies the drawing it has to reach it.  The crop of the original
makes the construction plain - a wide lid overhanging a narrower body,
split by a hard black line - so whatever produces it keeps that shape.

---

**140** — S13: all eight characters, and a finding that says NOT to give
the AI per-character stats.

*The player side already works, and now it is gated.*  The five tables are
read per character at setup, and driving each of them 180 frames from a
standstill on the same straight puts them in exactly the ROM's order:

    Peach / Yoshi   382 px   600 speed by frame 67   quickest off the line
    Koopa / Toad    323 px   frame 91                most agile steering
    Mario / Luigi   276 px   frame 130
    Bowser / DK Jr  135 px   slowest accel, highest top (944)

Tops at 100cc: 944 / 912 / 880 / 864 across the four pairs.  The selftest
pins both the tops and the ordering, so a regression that made everyone
drive like Mario would show.

*The AI does NOT have per-character stats, and giving it any would be
wrong.*  This was about to be the obvious next step - opponents all drive
the same in our port - so it was worth checking first:

    kart   $B4 (its per-player block)   top speed seen over 1500 frames
    0      $0390                        785      the demo's P1
    1      $0370                          0      P2
    2-7    $0000                        736 942 1049 1054 1059 1066

**AI karts carry no block at all**, and their tops run past 1049 - above
the 944 that is the FASTEST character's cap at this class.  So they are
not reading the character tables, and they are not bound by them.  Our
shared-physics AI is structurally faithful; wiring per-character stats
into it would have made the port less like the game, not more.

What is left on S13 is verification, not code: Mario and Toad are replay
exact, Luigi rides along in both of the user's human runs (93.0% / 82.0%),
and the other five are read from the same tables by the same code.  A
recorded run per character would close it; the risk in the meantime is low
because the remaining difference is table data, not logic.

---

**141** — The rubber band, found and located.  Not ported yet; this is the
map for whoever picks it up.

The user, from the start: "AI uses rubber-band technique and their order
is pre-determined... top speed is used to catch up to get to their
supposed order."  Both halves are one field.

`$80B074` picks an AI kart's target-speed row from the waypoint attribute
**offset by `$C8,x`**.  Watching `$C8` for every kart through a race, the
AI karts step `$0010 -> $0008 -> $0000` while the two humans sit at
`$0000` throughout, and exactly ONE instruction writes it: `$80:AD93`,
fed by `$80AD96`:

    $80AD96  $0E50 set        -> $C8 = 0        the band switched off
    $80ADA5  $84,x != 0       -> $C8 = $18      the strongest row
    $80ADA9  $10 bit 5 set    -> $C8 = $18
    $80ADB4  y = $00E6,x                        the kart's slot in the ORDER
    $80ADC5  x = $010C,y      the kart AHEAD of it in that order
             its $10 negative -> $C8 = $08
    $80ADD1  x = $0110,y      the kart BEHIND it
             its $10 positive -> $C8 = $08
             otherwise        -> $C8 = $10

So the row is chosen by looking at the neighbours in a **pre-determined
running order** - `$00E6,x` is the kart's slot, `$010C`/`$0110` the tables
of who is ahead and behind - and a kart out of station gets a faster row
until it is back.  Four rows: `$00`, `$08`, `$10`, `$18`.

That also explains NOTES 140's measurement, where AI tops ran to 1066
against the fastest character's 944: the band, not the character.

Our AI has none of this - one row (+0) for everyone, plus a softened
off-road cap that is our own invention and labelled as such.  Porting it
needs: what fills `$00E6`/`$010C`/`$0110` and when, what `$84,x` is, and
what `$0E50` gates.  All four are named now.

---

**142** — S16 closed, and S2/S11 turn out not to be the small jobs the
roadmap called them.  Reporting all three honestly.

*S16 - the falling kart's z.  DONE.*  `$80B5CD` sets `$1F` = 1 and the
game leaves it there for the whole 60-frame countdown; the drop you see is
the sprite (NOTES 135a).  Our physics lowered z instead, which put the
kart under the plane and - once sprites below the plane were clipped -
hid it behind the track.  Physics now holds `$1F` = 1 like the game, and
the visible drop is a RENDERING effect in `draw_scene` and nowhere else,
not clipped against the plane.  Selftest pins the z.

*S2 - the start grid.  Bigger than billed, and worth knowing how wrong we
are.*  Measured against the game's own grid (frame 0 of three logs):

    track  7   ours (897,604)   game (952,756)   off by (-55,-152)
    track 16   ours (948,601)   game (960,592)   off by (-12,  +9)
    track 19   ours (106,536)   game (136,524)   off by (-30, +12)

So the SHAPE is right - P1 and P2 are 32 px across and 24 px back in both
ours and the game - but the ORIGIN is out, badly on track 7.

The decode did not close.  `$819207` unpacks a record word as
`x = (w & $7F) * 8 + $12`, `y = ((w & $3F80) >> 4) + $14` with `$12`/`$14`
either 4 (`$8191DE`) or 10 (`$8191F4`) - but **no offset makes the
measured positions cell-aligned**: 952 needs an offset of 0 or 8 mod 8,
and neither 4 nor 10 qualifies.  So either a third entry point sets other
offsets, or the position is adjusted after unpacking.  `$0C` is loaded at
`$819053` with `#$0018`, which is not a ROM pointer, so that is a
different use of the same direct page.  Next step: watch `[$0C]` and the
kart positions at the moment the grid is built, rather than reading it.

*S11 - the countdown.  Not measurable from what we log.*  None of the
globals in `demolog.lua` is the countdown timer, and the frame the kart
first moves is the player's REACTION, not the light: 339 in both human
runs, 345 in the time trial, 539 in the demo.  Needs the countdown's own
address, which means finding it first.

**142a** — the start boost, from the user, before any decode.  Recording
their words because they are the specification:

> "while the count down is on, you can accelerate, but the cart doesn't
> move.  And the interesting thing is that you are launched at higher rev,
> but normal speed.  And if you accelerate in exactly one particular
> point, you get a turbo launch."

Three facts to find, and they constrain each other:

* the kart is HELD but the throttle is not ignored - something
  accumulates while it is held;
* at release, "higher rev but normal speed" - so that something is NOT
  `$EA`, and it survives the release;
* one exact moment gives a boost - so a window tests it.

*Refined by the user before recording:* holding the throttle from the very
beginning is not neutral, it is a PENALTY - "the cart slides on itself and
doesn't start until revs are back to zero."  So the rev accumulator
overshoots, and over-revving costs you the launch entirely until it
decays.  Three outcomes by WHEN you press:

    too early, held through   wheelspin; no motion until revs decay to 0
    somewhere in between      normal launch, "higher rev but normal speed"
    one exact point           turbo launch

That is a much better experiment than a neutral case: the penalty state is
directly observable - throttle held, kart not moving, something counting
down - so the accumulator and its decay show themselves without needing to
be guessed at.

Our port has none of it: the countdown holds the kart for an invented 60
frames a step and then simply lets go.  S17.

The user has also pointed out that their earlier recordings differ at the
start - one waited before moving, the others went immediately - so the
existing logs already hold two of the three cases.  What is missing is a
successful turbo launch, and a `starts` recording with all three in one
file (baseline / held / rocket) makes them diffable against each other
with everything else identical.

**142b** — S2 again: the grid is NOT a stored table, and NOTES 111's lead
was probably the wrong routine.

Three things ruled out, cheaply:

* **No offset makes the measured positions cell-aligned.**  952 is 0 mod
  8, so `$819207`'s `x = (w & $7F) * 8 + $12` needs `$12` in {0, 8} - and
  its two call sites set 4 (`$8191DE`) and 10 (`$8191F4`).
* **The coordinates are not in the ROM at all.**  Searching for (952,756),
  (960,592) and (136,524) as word pairs in either order: zero hits.
* **`$819207`'s packing is the generic cell-word unpack**, the same shape
  as `$84DCC4` in the OBJECT spawner (`and`, three `asl`, `adc #$0004`).
  So it is likely a shared helper and NOTES 111 read it as the grid on
  circumstantial evidence.

So the grid is COMPUTED - most plausibly from the finish-line record plus
per-slot offsets - and finding it means watching it happen, not reading.

Also learned, and it cost two runs: **memory taps cannot see these
writes.**  A Lua tap on `$00:1018` catches six writes in 2171 frames and
none is the grid, exactly as this repo's own MAME README warns about bank
`$7E`.  The debugger's watchpoints do work (`wpset 7e1018,2,w` with
`-debugscript`), and that is the tool for the next attempt - ideally on
the `starts` recording, where the grid, the countdown and the launch all
happen in the same few hundred frames.

---

**143** — The start: the rev is `$C2`, and the turbo launch is the
mushroom boost.  Decoded from the user's four-start recording.

They recorded four starts in one file - late, over-revved-and-penalised,
revved-but-not-penalised, and a clean turbo - which made every question
answerable by diffing one against another.

*The launch itself, from the log (frames 338 on):*

    run 2 (penalised)  $EE = 0   speed 0,1,2,3...     nothing happens
    run 3 (normal)     $EE = 2   speed 2,4,6,8...     ordinary launch
    run 4 (TURBO)      $EE = 50  speed 0,50,100..1200  and $AC = $10

**`$AC` = `$10` is the boost drive state - the same one the mushroom
uses**, which the port already has as `smk_player_boost`.  The turbo start
is not a new mechanism; it is the mushroom, awarded at the line.  It ran
23 frames, took the kart to 1200 (against a 912 top), then `$AC` returned
to 0 and the speed decayed -32, -24, -16, -8 back to normal.

*The rev, found by dumping the whole kart block and diffing the four:*
**`$C2`**, which we had already met without knowing it - `$80A10F` halves
it on a crash with a floor of `$0100`, and `$0100` is exactly what the
late start idles at.

    just before release   idle 256   penalised 19264   normal 11008   turbo 11776

*The machine (`$80B0EE`..`$80B180`):*

    $80B0EE  if $E2 bit 0 (the spin flag) is set:
               while $C2 >= $2000: $C2 -= $70 and $E2 |= $20   <- WHEELSPIN
               below $2000: clear $E2 bits 0 and 5             <- it lets go
    $80B112  $70,x set -> $C2 = 0
    $80B119  already spinning -> do not build
    $80B121  the pad decides which delta, then
    $80B169  $C2 += delta, floored at $0100, CEILINGED at $0E20

and the deltas are per-class globals, live values at 100cc:

    $0E20 ceiling          24575     $0E26 throttle off      -640
    $0E22 below $2000        512     $0E28 $C2 >= $1000      -896
    $0E24 at/above $2000      64     $0E2A other             -384

So holding from the start runs `$C2` past `$2000` and earns the spin,
which then bleeds `$70` a frame - the user's "doesn't start until revs are
back to zero", and it is `$2000`, not zero.

Still to find: the test at the line that turns 11776 into a boost and
11008 into nothing.  Everything else is in hand, including that the reward
is a mechanism we already have.

---

**144** — S17 ported: the rev, the wheelspin and the turbo launch.

The comparison NOTES 143 was missing is `$80956A`, and it is a BAND:

    lda $C2,x
    cmp #$3000   ; 12288 and over -> $809591: $E2 |= 1, the WHEELSPIN
    cmp #$2E80   ; 11904..12287   -> $E0 |= 1, the TURBO window
                 ; under          -> nothing

(Two-player uses `$30C0`/`$2DC0` at `$809555`; this port is one-player and
that is labelled.)

The user's own numbers land on it exactly.  Their turbo run read 11776 two
frames before the line and the rev climbs `$40` a frame above `$2000`:
11776 + 128 = **11904 = `$2E80`**, the first value in the band.  Their
normal run read 11008 and would reach 11136 - short.  Their penalised run
sat at 19264, far past `$3000`.

Ported whole:

* the parameters are read from the ROM row at `$81:EFF3` - ceiling 24575,
  `+$0200` under `$2000`, `+$0040` over it, `-$0280` off-throttle;
* `smk_player_rev` builds and clamps to `[$0100, ceiling]`, and once
  over-revved bleeds `$70` a frame until it drops under `$2000`;
* while the wheels spin the throttle does nothing at all - measured, `$EE`
  = 0 for the whole penalty;
* `smk_player_launch` pays the window out through `smk_player_boost` -
  **the mushroom's own boost**, which the port already had.

The window in the port is **six frames wide** (press at f105..f110 of a
180-frame countdown; f104 over-revs, f111 misses), which is the user's
"one particular point" with a number on it.

Labelled and outstanding: our countdown is still 180 frames of invention
(S11) where the game's is about 338, so the window sits at the right
DEPTH in the rev curve but not at the right wall-clock moment.  Closing
S11 moves it without touching any of this.

---

**145** — S11 closed, and NOTES 144's rev curve corrected by measurement.

*The countdown is 336 frames.*  `$809FE1` loads `$0146` with `$FEB0` =
-336 and `$80A1F8` does `inc $0146 / bne` - the karts are released on the
frame it reaches zero.  Found by asking the user's four starts which
address moves identically in all four and changes exactly at the release:
`$0146`, `$FFFF` -> 0, in every run.  The race clock `$0100` starts
ticking immediately after.  The port had 180 invented frames.

LABELLED: the 3-2-1 digits are still an even split of the 336.  What the
game shows is Lakitu with a traffic light on a timer of its own (`$0142`,
207 down by one every second frame) and we have neither his art nor that
decode.

*And the rev builds at a flat 96 a frame.*  NOTES 144 read `$80B169`'s
deltas out of the row at `$81:EFF3` - `$0200` under `$2000`, `$0040` over
- and built a two-rate curve from them.  The recording says otherwise, in
all three throttled runs and across the supposed knee:

    run 2  f200=17856  +96/f ... then bleeds -320/f and climbs again
    run 3  f232=1024   +96/f steadily to 11008 at the line
    run 4  f224=1024   +96/f steadily to 11776

**A flat 96, no knee.**  So that row is either not the one in play or is
scaled somewhere we have not found; the measurement wins and the port uses
it.  The check that settles it: run 4 began revving eight frames before
run 3, and 8 * 96 = 768 is exactly the gap between their readings at the
line, 11776 against 11008 - and the port now reproduces both to the unit.

With the real countdown and the real rate the window is **four frames
wide** (press at f211..f214 of 336; f210 over-revs, f215 misses), which is
the user's "one particular point" at last standing on two measurements
rather than one reading.

Still open, and honestly not modelled: over-revving in the GAME oscillates
- run 2 climbs at +96, bleeds at -320, and climbs again, wobbling around
19-20k - where our port simply latches the spin flag and holds at the
ceiling.  Both end at the line over-revved, which is what the player
feels, but the wobble is not ours.

**145a** — Lakitu's semaphore: what has been ruled out, and where to look.

The user wants the light because it, with the sound, is how you time the
launch.  The TIMING half is already right - the countdown is the measured
336 frames (NOTES 145) and the port shows 3-2-1 digits across it.  What is
missing is the game's own Lakitu and his light.

Ruled out this session:

* **Not in the asset tables we decode.**  `gfx_b` and `gfx_f` - the small
  and medium graphics - identify as mode7 track tiles under
  `smk gfx --identify`, every entry.  Rendering `gfx_f` as 4bpp gives
  noise.  So his art is not there.
* **Not a discrete light state in low WRAM.**  Asking the user's four
  starts for an address that takes 3-5 small values identically in all
  four turns up only race-phase flags (`$003A` goes 2 -> 4 at frame 4 and
  4 -> 6 at the release).  The light is an ANIMATION, not a counter.
* **MAME exposes no VRAM or OAM share** to Lua (`:aram` and `:wram`
  only), so the tiles cannot be lifted that way; a debugger script or the
  Python oracle's own `vram` would be needed.

Next step, for whoever picks it up: boot the Python oracle to a race and
render the OBJ half of its VRAM - Lakitu's tiles are uploaded for the
countdown and will still be resident.  Match those against the ROM to find
the source asset, the way the kart sheets were found.  `$0142` (207 down
by one every second frame) is the likeliest driver of his animation.

**145b** — Ghost Valley, for the third and last time: NO moving objects.

The user has now said this three times and I have re-derived the same
wrong turn each time, so it goes in the log with the evidence.

The handler table at `$84DAA9` is indexed by `$0D28` and its entry 0 is
`$84DC80`, the path repositioner - and tracks 1, 8 and 16 (all three
Ghost Valleys) select it.  That looks like "Ghost Valley has movers".  It
is not.  Replaying the user's own Ghost Valley run and logging all four
object slots for 1800 frames:

    f30..f540   (924,172) (900,148) (868,140) (836,140)   unchanged
    f570        (900,148) (868,140) (836,140) (804,140)   waypoint 3 -> 4
    f600        (836,140) (804,140) (772,140) (740,140)   waypoint 4 -> 5

The slots only ever shift when the WAYPOINT advances: the repositioner
re-places them along a path ahead of the player, which is spawn
behaviour, not an obstacle in motion.  Nothing on that track animates.

And the same table kills the theory outright: Bowser Castle (3, 9, 17),
Rainbow Road (5) and Donut Plains (2, 11, 19) - the tracks that DO have
Thwomps and moles - all select `$84DBD5`, the STATIC spawner.  So motion
is not chosen by `$0D28` and this whole line is a dead end.  Look for a
per-object type handler, as NOTES 127 said, and look on the tracks the
user named rather than the one the table points at.

---

**146** — The movers found: Thwomps and moles run a per-object SCRIPT, and
they only ever move in Z.

The user, cutting through three of my wrong turns: "thwomps and moles go
only up and down."  Forcing Bowser Castle and logging every object slot
proves it - x and y hold still while z swings, on every frame, with the
player's waypoint unchanged:

    f599   wp 21   (143,484, 227)  (157,738, 487)
    f899   wp 21   (160,477,1820)  (154,743, 904)
    f1199  wp 21   (154,475,  18)  (154,747, 983)

So it is a HEIGHT animation, not a path - which is why nothing in the
position machinery ($84DC80 and friends) accounted for it, and why the
handler table at $84DAA9 sends every Thwomp track to the STATIC spawner.

**The mechanism is a bytecode interpreter, one script per object.**

    +$04   the object's script pointer
    +$08   its state

    $85E0B9   ldy $04,x / bne / tyx / jsr ($0000,x)

The record's FIRST WORD is the handler address; the interpreter simply
calls it.  Each handler reads its arguments from `$0002,y` on and advances
`+$04` past itself.  The one that matters here is `$85DDA0`:

    $0002,y -> $1F,x   the HEIGHT, with $1E,x cleared
    $0003,y -> $21,x   ($20,x cleared)
    $0004,y -> $15,x
    $0005,y -> $30,x
    then $04,x += 6

Command handlers sit in a table around `$85DD26`: `$DD2E $DDA0 $F871
$0E20 $DEEF $DEDF $DD74 $DDC7 ... $DDEF $DE68 $DD82 $DF4A $DDA0 $F816`.

This is the same shape as the tyre-smoke interpreter we already ported
(`$80D530`, src/effects.c): a pointer, a record, a handler per command.
Porting it means the interpreter, the handful of commands a Thwomp and a
mole actually use, and where their scripts are attached at spawn - not a
new subsystem.

NOT ported in this session, deliberately: a half-done bytecode interpreter
is exactly the kind of change that broke bouncing twice today, and the map
above is the expensive part.  It is one focused session's work from here.

---

**147** — The menus' font and palette, found by asking the running game.

Building the shell needed text, and the project's rule is that text art is
ROM data like any other.  Searching the asset tables for a font found
nothing, so the question went to the oracle instead: boot to the title
screen (`$0036/2 == 2`) and read VRAM.  The alphabet is there at 4bpp tile
`$400` — digits, `A-Z`, punctuation, a `cc` ligature and a set of whole
words baked as tiles (`LAP`, `TIME`, `COURSE SELECT`).

Why the static search had failed: **only bitplanes 0 and 1 are ever set.**
The glyphs are 2bpp art uploaded into a 4bpp region, so the 32-byte form
that VRAM holds does not exist anywhere in the ROM.  Searching for the
16-byte form instead still missed, because the load happens during boot,
before the point the labs normally attach.

Tracing it properly (write hook on `$7F:4400` installed from RESET, which
caught `$84:E12D` — the decompressor's inner store) and then matching
decompressed streams against live WRAM:

* `$C7:0000` → **4096 bytes = 256 tiles of 2bpp**, the font.  The game
  decompresses it to `$7F:4400`, expands 2bpp → 4bpp into `$7F:A000` and
  DMAs 8192 bytes to VRAM word `$4000`.  All 4096 bytes match the running
  game's WRAM exactly.
* `$C7:1996` → 17408 bytes to `$7F:0000` (17408/17408 exact), the menu
  screen's tiles — and its **last 256 bytes, at offset `$4000`, are the
  eight background palettes**, matching the oracle's CGRAM entry for entry.

DMA destinations, captured at `$420B`: `$7F:0000`→tile 0, `$7F:2000`→256,
`$7F:2800`→320, `$7F:A000`→1024 (the font).

Glyph order, off the sheet: `0-9` at 0, `A-Z` at 10, then `? . , ! ' "`,
the `cc` ligature at 42, two box corners, a solid block, `:` at 46, and
words from 48.  Ported in `src/font.c`.

Still not decoded, and so not drawn: the title logo and the menu
backdrops.  Their tiles are in the `$C7:1996` stream above; what is
missing is the tilemap that arranges them and the BG/scroll setup.  The
shell composes its own layout from the ROM's font and palettes instead,
which is stated in the ledger.

---

**148** — Course names without a name table, and the race length.

**Names.** There is no course-name string table to find: the ROM draws
names from the word-tiles at font index 48+.  But the *ordering* is fully
determined by two tables the project already had (NOTES 009), and that is
enough to derive every name:

* `$81EC1B[cup*5 + course]` → track index (the game's own indirection —
  `$81EC47` computes `$0124` from `$0150`/`$0152` through it).
* `$81EC2F[track]` → theme*2.

Walk the cup order and group by theme: each theme is exactly one course
FAMILY, and a family's courses are numbered in the order the cups present
them.  That reproduces the printed line-up exactly:

    Mushroom  7 MC1  19 DP1  16 GV1  17 BC1  15 MC2
    Flower   18 CI1   1 GV2   2 DP2   3 BC2   0 MC3
    Star     13 KB1  10 CI2  12 VL1   9 BC3  14 MC4
    Special  11 DP3   6 KB2   8 GV3   4 VL2   5 RR

and the per-theme counts fall out right (Mario Circuit 4; Ghost Valley,
Donut Plains, Bowser Castle 3; Choco Island, Koopa Beach, Vanilla Lake 2;
Rainbow Road 1).  Two independent cross-checks: theme 0 = Ghost Valley is
already asserted by the breakable-block tile sequence in `src/blocks.c`
(`$80FC70` is theme-0 only), and the repo's own time-trial log is track 19
with `$0126 = 4` → theme 2 = Donut Plains 1.

The eight family WORDS are English text and are labelled as ours; their
assignment to themes is forced by the table above, not chosen.

**Correction to NOTES 009.** It listed a fifth cup row
`[2,0,4,12,8]` as "Special Cup reusing earlier courses".  There is no
fifth row: `$81EC1B` is 20 bytes and ends at `$81EC2F`, so those five
bytes are the *start of the theme table*.  Four cups, twenty courses.

**Race length.** `$014C` is the finish threshold on the progress word
(`lap << 8 | sector`, NOTES 052).  Measured live in a running race:
`$014C = $8500`.  The grid sits behind the line — P1's `$C0` starts at
`$7F1B`, lap byte `$7F`, sector 27 — so the FIRST crossing only reaches
`$8000`, which `$8089C9` special-cases out of the finish test.  Five more
crossings reach `$8500`.

    five laps = SIX crossings, and the lap shown is the crossing count.

`$8089D4`'s `cmp #$FF00` against the same threshold is what lights the
FINAL LAP tile the HUD sheet carries.

The port agreed once measured: `tools/laptest.c` drives the shipped AI
round every GP course and finds the first crossing at 0-96 frames against
laps of 1254-4344, i.e. the grid is behind the line on **20/20**.  This
also fixed a standing HUD bug — the lap readout was `me->lap + 1` and so
showed LAP 2 from the first time you passed the flag.

**Time trial specifics**, from the attract loop's own `$2C = 4` demo
(NOTES 113) and its log in this repo: no coins and no item boxes on the
tilemap, the kart is alone on the track, and P1's `$0E00` (coins) is **0**
on frame 0 and stays there.  All three are now what the port does.

---

**149** — A driver that obeys the rules, and the bug it found in its first
lap: Mario Circuit 2's jump was impossible.

`--autodrive` steered by the direction field with bang-bang left/right,
which is roughly what `src/ai.c` does - and the AI is not playing the game.
It writes its own heading and speed, ignores surfaces, and teleports itself
back onto the road when it wedges. A driver built that way cannot exercise
the player's rules, because it does not obey them.

`src/autopilot.c` only ever presses BUTTONS. It hands `smk_player_step` the
same pad word a person would, so the acceleration curve, off-road caps, the
slide machine, spin-out, wall bounce and its cost, hop and Lakitu all apply
to it exactly as they apply to the player.

**What it steers by, and the trap on the way there.** The obvious design -
aim at the ROM's route points with a speed-scaled lookahead - parks the kart
against a wall on Mario Circuit 2. The waypoints are sector CENTROIDS, not a
drivable polyline: the straight line from sector 29's point `(464,696)` to
sector 30's `(720,688)` crosses a solid barrier at `x = 656`. `src/ai.c`
already records this ("atan2 to a waypoint is only the OFF-COURSE recovery
path in the ROM, and treating it as the main rule was why our karts clipped
corners into walls") and I walked into it anyway. The split that works:

* the per-cell direction field steers - it is built FROM those waypoints,
  in the form that knows where the road is;
* the route points decide how fast to ARRIVE, via the bend ahead;
* nine surface probes bend the aim away from what cannot be driven on;
* stagnation is measured over a WINDOW, not frame to frame.

That last one matters more than it sounds. Pinned against the rail on
Rainbow Road, the kart jittered between `x = 297` and `298` for sixty
thousand frames with the throttle wide open, and an exact-equality
"did it move since last frame?" test reset on every one of them. (`ai.c`
has the same hole.)

**Tuning, measured rather than guessed.** Five laps of Mario Circuit 1,
50cc, with each piece switched off:

    everything on  2'34"   no probe  2'25"   no brake  2'23"
    no slide       2'01"   no brake + no slide  1'58"

The slide was costing **33 seconds** - it was held for 90 frames on
anything that merely bent. Short and rare now. The switches
(`SMK_AP_NOBRAKE` / `NOSLIDE` / `NOPROBE`) stay in the file so this takes a
minute to redo rather than an afternoon to rediscover.

**The bug.** Mario Circuit 2 refused to complete in every configuration.
Tracing it frame by frame over the jump:

    f1449  pos 513,702  spd  586  surf 16      <- onto the boost pad
    f1458  pos 540,694  spd 1036  surf 16
    f1463  pos 561,688  spd 1286  surf 10      <- onto the RAMP
    f1465  pos 566,687  spd 1336  surf 10  z 0 <- still on the ground
    f1466  pos 565,686  spd  334  surf 10      <- into the barrier

`z` never leaves 0. The kart drives over the ramp at 1336 and hits the wall
it is supposed to fly over. The layout is unambiguous - boost pad `$16` at
`x 512-543`, ramp `$10` at `x 552-560`, barrier at `x 568`, and the
direction field pointing EAST straight across all three:

    y 688   40 40 40 40 16 16 16 16 40 10 10 ## 40 40 40 40 ...

Cause: `src/player.c`'s object-class dispatch carried `p->drive != 0x10`,
and `$80B47B` - the boost pad's own handler, twelve pixels earlier - sets
`p->drive = 0x10`. The guard was therefore always false by the time the
ramp arrived. **The ROM has no such guard**: `$80B3F4` reads `$AE,x` and
dispatches on it (`$80B418`: `and #$000F / tax / jmp ($B3A5,x)`) with
nothing tested in between.

Removing it from the object path:

* all five replay gates score **identically**, to the tenth of a percent
  (100.0 / 100.0 / 100.0 / 81.5 / 93.0) - no recorded run in the set ever
  crosses a ramp while boosting, which is exactly why this survived;
* Mario Circuit 2 completes, 4'19"06.

The same guard on the HAZARD path is left in place and labelled: it has not
been shown wrong, and immunity to water during a boost is at least
plausible.

**The point.** Four gates, forty-four selftests and twenty AI laps all
passed while a jump on a Mushroom Cup track could not be taken. It took a
driver bound by the player's own rules to find it - the AI flies over that
barrier because it never asks the surface what is underneath.

**Where it stands, all twenty GP courses, five laps, 50cc** (after the
steering damping of NOTES 149 and the rescue fix of 149a):

    19/20 complete.  1'34" Mario Circuit 1 to 5'21" Rainbow Road.
    (18/20 before those two fixes; 15/20 for the bang-bang driver
    this replaced.)
    The one that does not is Bowser Castle 3, and it fails on S12.
    Thwomps are spawned at the right positions but never move (NOTES
    146), so four permanently-down Thwomps at (428,396) (436,396)
    (444,396) (452,396) are a wall across the road; the kart is pinned
    at (448,401) while DRIVING, with no rescue involved.

    A human player cannot get past them either.  This is the strongest
    argument yet for porting the mover scripts: it is not a cosmetic
    gap, it makes a Star Cup course unfinishable.

The previous `--autodrive` (direction field, bang-bang, no sensing) got
15/20 on the same measurement, and its best Mario Circuit 1 was 1'46"86
against 1'34"23 here.

---

**149a** — Lakitu could never put the kart down, and it was not Lakitu's fault.

Damping the autopilot's steering (NOTES 149) made it quicker everywhere,
and Bowser Castle 1 - which had been finishing - started hanging instead.
The trace says why, and it is a real bug, not a driver problem:

    f1240  pos 495,711  spd 0  sec 19  haz  6  z     1   <- falls
    f1300  pos 465,711  spd 0  sec 19  haz 12  z 12288   <- Lakitu has it
    f8660  pos 457,520  spd 0  sec 20  haz 12  z 12288   <- still has it

Eleven thousand frames in the carry state, position frozen. The rescue's
L-walk ($80B2D2) steps the kart two pixels a frame toward its waypoint -
sector 19's, at `(456,504)` - and `smk_collide_objects` pushes the kart's
POSITION out of any track object within `SMK_OBJ_RADIUS`:

    k->x += nx2 * push * SMK_POS_ONE;

There are Thwomps at `(436,516)`, `(452,516)`, `(468,516)`, straight across
the racing line. Walking north from `(465,711)` to `(456,504)` passes
`y = 516` about four pixels from one of them, so every frame the walk moves
the kart in and the push throws it out. Neither side wins; the state
machine never reaches `$0E` and the kart is never set down.

**The push ignores height entirely.** During the carry the kart is at
`z = $3000` - forty-eight units up, in Lakitu's hands - and a ground object
still shoves it. The wall path has respected height since NOTES 136
(`$80FA5A`); object collision never learned the same rule.

Fixed narrowly, at the call site: no object collision while `p->hazard` is
set. A kart in Lakitu's hands is not on the track, so the track cannot
touch it. All five replay gates score identically (100.0 / 100.0 / 100.0 /
93.0 / 81.5); Bowser Castle 1 completes in 2'48"16 and Bowser Castle 2 in
3'40"60, both of which had failed.

Left alone deliberately: the general question of whether a HOPPING kart
should clear a ground object. That needs measuring, not guessing, and the
narrow rule above does not depend on the answer.

Bowser Castle 3 still fails, and still for the S12 reason - there the kart
is pinned while DRIVING against four permanently-down Thwomps at
`(428..452, 396)`, with no rescue involved. That one only the mover
scripts will fix.

---

**150** — Stuck on a Thwomp: three defects, and one ROM register modelled
twice.

The user hit a Thwomp on Rainbow Road and could not get free. In the real
game a few shoves while holding a direction works you clear; in the port it
was permanent. `tools/objhit.c` is the repro - place the kart a known
distance from a known object, hold the throttle and optionally a direction,
print the state across the impact.

**1. The bounce was erased one frame after it happened.**

    f48   dist  6  speed 150  vy +150   vang 8000   vlag 0  clag 0
    f49   dist  7  speed  81  vy  -77   vang 8000   vlag 0  clag 0   <- hit
    f59   dist 10  ...  bcool reaches 0
    f60   dist 11  speed  87  vy  +87   vang 8000               <- back in

`smk_collide_objects` reflected `vx`/`vy` and nothing else. `player.c`
rebuilds the velocity from `$A2` every frame, and `$A2 = $A4 + $A8` -
neither of which the hit touched. So the reflection survived exactly as
long as the ballistic window, then the kart resumed driving into the
object. For ever.

**2. `$A8` is modelled TWICE.** `k->crash_lag` and `p->vlag` are the same
ROM register, and the comments on both say so (`smk.h` 182 and 281). The
wall crash writes one; `vel_angle` reads the other. So the impact slip
never reached the velocity direction on ANY impact, wall or object.

What the ROM does: contact sets `$10` bit `$1000`, and the next update runs
`$80A0AF` or `$80A0C7` (dispatched at `$80B3DF` on bit `$2000`). Both push
the kart's ACTUAL velocity through the arctangent `$81F638`; `$80A0AF`
writes the result straight to `$A2`, `$80A0C7` takes the slip against `$A4`
and stores it in `$A8`, with drive state `$16` when the slip exceeds
`$2000` (45 degrees) and the `$1C` slide when it does not and the throttle
is held.

Ported narrowly - `vel_angle` takes `crash_lag` while a crash is running -
and **the user's own crash recording judged it**: the human wall-crash run
goes **81.5% -> 86.2%** within 1 px, the other human run is unchanged
(93.0 -> 92.8), and both staged demos stay at 100.0%. The gate floor is
ratcheted 80 -> 85. This is a piece of the `$80A0C7` port that NOTES 131
abandoned; the narrow piece is right, and the recording says so.

**3. Objects had no low-speed floor.** A wall does not scale the bounce
when the kart is barely moving: `$80F9C1` forces each component to
`+-$100`, which is why the wall push-back "is constant no matter the speed,
more like a push back than a real bounce" (the user, NOTES 133). Objects
never got that rule, so a hit at a crawl returned a crawl, the kart
re-touched within a few frames, and it was glued. With the floor applied
along the contact normal, steering one way now takes it x 68 -> 97 and the
other x 65 -> 36; head-on with no steering it is shoved clear to 13-19 px
instead of oscillating at 6-9.

**What it cost, and why that is not a regression.** The autopilot's
completions went 19/20 -> 18/20. Bowser Castle 3 - previously the ONE
course it could not finish - now completes (5'15"18). Bowser Castle 1 and
Donut Plains 3 now fail instead, and Bowser Castle 1 shows why:

    f8600  pos 459,516  spd 256   f8620  pos 461,516  spd 256

pinned at exactly `$100` - the new floor - in the four-pixel slot between
Thwomps at `(452,516)` and `(468,516)`. That slot is a trap only because
the Thwomps never rise (S12). The earlier 19/20 was partly the bot
squeezing through walls that should not be there; a human wedged in four
pixels between two solid objects is stuck too. The fidelity number went
up, the exploit went away, and both point at the same missing feature.

---

**150a** — The same hit, harder off a hop; and how hard a slow one should be.

Two follow-ups from the user driving NOTES 150.

**"When you hit it while jumping, you get pushed back even harder."** True,
and measurable. The knockback countdown lived inside a `!k->airborne`
guard, so it did not run in the air:

    grounded   bcool 10  9  8  7  6 ...
    airborne   bcool 10 10 10 10 10 ...  until it lands

The whole ballistic window was held for the entire flight, so the same
impact carried the kart much further off a hop than off the ground.

Taking the guard off fixed the hop and cost the Ghost Valley human run a
full point (92.8% -> 91.8%) - that run is all hops and rail hits, so it is
exactly the witness for WALLS. Both are right, because the ROM does not
use one mechanism for both: a wall runs the `$5C`/`$42` counter, while the
pipe crash is drive state `$16` with `$10 = $C000` (NOTES 072). The port
had collapsed them into one field. Split (`k->bounce_obj` marks the
object's window), the wall keeps its grounded countdown and the object's
runs in the air: Ghost Valley back to **92.8%**, the crash run **86.2%**,
both their best.

**"At low speeds it feels too aggressive. The bounce is milder."** Also
true. NOTES 150 borrowed the wall's `+-$100` floor, which sends a slow
arrival away at three times its own speed. Measured on the repro - final
distance from a low-speed contact after 240 frames, driving in and holding
each direction:

    kick    0    7.0 / 7.2 / 6.4   glued, whatever you steer
    kick $60   10.0 /16.4 / 8.9   one direction still stuck
    kick $80   12.0 /28.0 /14.2   frees in all three      <- taken
    kick $B0   14.0 /19.7 /36.7
    kick $100  10.0 /30.8 /38.1   frees, but it kicks

`$80` is the mildest shove that still works free the way the game does -
half a wall's, which is what "milder" means here. LABELLED: fitted to
behaviour, not read from the ROM. NOTES 072 measured the object response
as reflect and 308/581 with no floor at all, and no floor leaves the kart
glued, so something is missing from that measurement rather than from this
fit.

Side effect worth recording: at `$100` the autopilot was pinned at exactly
that speed in the four-pixel slot between two Thwomps on Bowser Castle 1;
at `$80` that course completes again (2'48"98).

---

**150b** — The graze exemption, retested and still wrong. (Negative.)

NOTES 134 found that `$80A0EB`'s exemption - a slip under 45 degrees keeps
its throttle instead of taking the crash deceleration - is in the ROM, is
visible in the user's recording, and makes the port WORSE. It was left
out, with "something upstream still differs" as the standing explanation.

NOTES 150 looked like that upstream difference: the impact slip was never
reaching `$A2`, so every graze was being judged on a velocity direction
that had already snapped back to the heading. Retested with the slip now
carried:

    exemption off   crash 86.2%   Ghost Valley 92.8%
    exemption on    crash 81.6%   Ghost Valley 29.6%

Worse, and far worse on the run full of glancing rail contacts - which is
exactly the run the exemption should help. So `vel_angle` was NOT what it
was fighting, and the standing explanation still stands with one more
candidate eliminated.

(Method note: the first attempt at this measurement was meaningless -
`getenv` without `<stdlib.h>` compiles to an implicit int-returning call
and both arms of the experiment produced identical numbers. Identical
results from a toggle are a bug in the experiment, not a finding.)

---

**151** — "You can still hit the invisible one": drawing and collision
disagreed about which objects exist.

The user, on Rainbow Road: a place where two Thwomps should stand, only one
is drawn, and the missing one still hits you.

Two different sets were in use. `draw_scene` drew the LIVE slots
(`crs->live[]`, filled by `smk_course_spawn`), while `smk_collide_objects`
walked the whole decoded entity list `crs->ent[0..nent)`. So every object
on the track collided, and only two were ever visible.

The game has no such split. `$819136` builds the live table with
`lda #$0004` and two `dec A`s unless `$B6` says two-player, so a one-player
race has **two** object blocks; `$81:9194` places them at `$1800`, `$1880`,
`$1900`, `$1980` (each with a sub-block at +`$40`, `$819174`), and
everything downstream works on those blocks. Collision now uses the same
live set. All five gates unchanged.

Also mapped, for the mover work: inside a block, x is at +`$18`, y at
+`$1C`, height at +`$1F`, and the script pointer at +`$04` (`$84DC54`,
`$84DC61`, `$819163`). An earlier scan for the live slots failed because it
looked for x and y in ADJACENT words; they are four bytes apart.

**Which two are live.** `$84DC30` reads a per-segment start offset from
`$84DAC5` - the table is `[0, 8, 16, 24]`, byte offsets into the record
list, so record indices 0/4/8/12, which is what `src/course.c` already
guessed. On Rainbow Road segment 3 that gives records 12 and 13, at
`(68,700)` and `(60,700)` - eight pixels apart, so the two billboards
overlap into the single Thwomp the user saw. Records 14 and 15,
`(52,700)` and `(76,700)`, were the invisible pair. Whether the real game
also draws an overlapping pair there, or picks a different two, is not yet
established: the fix above stops us hitting what we do not draw, but does
not by itself prove the live PAIR is right.

Behaviour the user reports and we do not yet model: Rainbow Road's Thwomps
flash through colours, cannot be destroyed, and give no bounce - just the
ordinary object hit. Moles rise out of a hole, and one you hit sticks to
the kart's face for a while.

---

**152** — Thwomps measured. And why four earlier captures found nothing.

The user: *"thwomps in all the tracks (bowser castle and rainbow road)
start first lap up, then after finishing the first lap, they get
activated."*

That one sentence is the whole reason this took five runs. Every capture
before it recorded lap 1, when the objects are parked, and duly reported
"x, y and height all frozen". The frozen `z = 4096` on Rainbow Road was
not a bad offset or a void run - it was the resting state. **An object
capture that does not first complete a lap is void**, and the lab now
drives the flow field until the progress word `$C0` reaches `$8100`
(NOTES 148) before recording anything.

**The measurement** (Rainbow Road, `$0D2C = 6`, blocks `$1800`/`$1880`,
height = the word at +`$1F`, per frame, 1100 frames):

    parked          z = 4096, until the lap completes
    FALL            15 frames, deltas -64 -96 -128 -160 -192 -224 -256
                    -288 -320 -352 -384 -416 -448 -480, then clamped at 0
                      -> velocity starts at -64 and gains -32 a frame
    hold at bottom  135 frames, every cycle, both objects
    RISE            +64 a frame, linear
    fall again      same gravity, from wherever the rise reached

x and y never change - motion is Z only, exactly as the bytecode implied
(NOTES 146).

**What is NOT pinned: how long the rise lasts.** Block `$1800` rose for
119 frames (peak 7616) then 116 (7360) then 96 (6144); block `$1880` rose
144 (9216), then 199 (12736), then 93 (5952). Periods per object are
fairly steady - about 270 frames for one, 294 for the other - and
135 + 15 + rise accounts for them, so the variation IS the rise. It is
not proximity: the kart was 566, 447, 311 and 566 px away at the four
drops of one object. So the duration comes from the script, and the
script is not decoded.

**Flashing is separate from motion.** +`$06` in the block cycles through
65 distinct values while x, y and z sit still - that is the palette/frame
counter behind the "flashing colours" the user reports on Rainbow Road,
and it runs on lap 1 too.

Corrections to earlier notes, both found on the way:

* **`$0D2C` is not the object TYPE.** NOTES 078 read `$84DD15` as a type
  table indexed by it ("MC tracks get pipes, Bowser tracks Thwomps").
  Grouping the twenty GP tracks by `$0D2C` gives {MC1, VL1, GV1, BC1, CI1,
  DP1}, {GV2, DP2, BC2, VL2, KB2, CI2, KB1, MC2}, {MC3, GV3, BC3, DP3},
  {RR, MC4} - Mario Circuit 1 with Bowser Castle 1, Ghost Valley 2 with
  Bowser Castle 2. That is position within the cup, not pipes-vs-Thwomps.
  It selects a SCRIPT; appearance is per theme, which is how the art is
  already loaded.
* **The object block layout**, needed by any of this: blocks at `$1800`,
  `$1880`, `$1900`, `$1980` (`$81:9194`), two live in a one-player race
  (`$819136`), each with a sub-block at +`$40` running its own script
  (`$819174`). Inside: x +`$18`, y +`$1C`, height +`$1F`, script pointer
  +`$04`. An earlier hunt for the live slots failed because it looked for
  x and y in ADJACENT words; they are four bytes apart.

---

**152a** — The autopilot gets round all twenty. (Measurement.)

Clean sweep, five laps each, 50cc, no binary changes mid-run:

    20/20 GP courses complete.  1'37" Choco Island 1 to 5'14" Rainbow Road.

It was 15/20 this morning.  What moved it: sensing the ground it drives
over and steering damped by the ROM's own turn rate (NOTES 149), the Mario
Circuit 2 ramp guard that made that course's jump impossible (NOTES 149),
Lakitu's rescue no longer fighting object collision (NOTES 149a), and the
object hit carrying its slip with a milder low-speed floor (NOTES 150/150a).

Two of those were found by the autopilot itself and one by the user
playing.  None by the replay gates, which stayed green throughout.

---

**152b** — The movers, ported.

The measured cycle from NOTES 152, in `src/course.c`: parked at 4096 until
the first lap completes, fall (velocity from -64 gaining -32 a frame,
clamped at 0), 135 frames on the floor, rise +64 a frame. What is NOT
ported is the bytecode VM at `$85E0B9` that produces it - the same choice
as the tyre smoke, and for the same reason: a half-understood interpreter
is a worse thing to own than a measured curve.

Checked against the capture: the port peaks at **7552** where the game
measured **7616** (that is 118 rise frames against 119 - the constant is
120 and the difference is where the clamp lands).

Three things here are OURS and labelled at the point of use:

* **the rise duration** (`SMK_MOVER_RISE` = 120). The capture gave
  119/116/96 on one object and 144/199/93 on the other; 120 reproduces the
  270-frame period one of them held. It is script data we have not read.
* **which themes move** - Bowser Castle and Rainbow Road, from the user.
  `$0D2C` is not the type selector (NOTES 152), and the real per-theme
  binding is not decoded. Everything else measured static over 400 frames.
* **the height at which a raised Thwomp stops touching you** - half the
  resting height. The capture gives the MOTION, not the hit box.

Height on screen reuses the kart's own rule: `$1F` feeds a 16.16 z where
one screen pixel is 25029, so one screen pixel is 25029/256 = 97.8 units
of `$1F`. The lift then scales with the sprite's drawn height, or a
distant Thwomp would rise as far on screen as a near one.

Effect on the autopilot, which is the cheap gate for this: Bowser Castle 3
went 4'25"60 -> 3'37"76 and Bowser Castle 1 2'47"08 -> 2'39"65, because a
raised Thwomp is no longer a wall. All five replay gates unchanged - no
recorded run contains a mover.

---

**153** — Bowser Castle's Thwomps were made of lava.

The user: they are grey in the real game, molten here.

The port drew every theme's objects from palette base `$F0`. Rendering the
same art from three candidate rows shows why that survived so long - it is
right twice and wrong once:

    theme 1 Mario Circuit   $F0 green pipe    $C0 white pipe
    theme 7 Rainbow Road    $F0 grey Thwomp   $C0 grey Thwomp
    theme 6 Bowser Castle   $F0 LAVA Thwomp   $C0 grey Thwomp

Bowser Castle's `$F0` is `CE0000 FF6300 FFBD00` - dark red, orange, amber -
with no grey in the row at all, while every theme carries a grey ramp at
`$CA-$CE` (`EFEFEF D6D6D6 BDBDBD A5A5A5 7B7B7B`).

MEASURED, and the reason this is not just picking the row that looks
right: over 600 frames of a Bowser Castle race the game writes palette 7
(`$F0`) into OAM **zero** times. An object-free Ghost Valley run over the
same window writes it 120. So `$F0` is provably not where that theme's
objects come from, and `$C0` is the only row there with the ramp the art
indexes.

Ported as a per-theme base: theme 6 takes `$C0`, everything else keeps
`$F0`. Mario Circuit's pipes stay green, Rainbow Road's Thwomps stay grey,
Bowser Castle's become grey.

LABELLED: the table is ours. The OAM tally could NOT isolate the objects'
own row - karts, the HUD (`$C0`) and tyre smoke (`$D0`) dominate the counts,
and the differential against a track with an empty entity list came back
inverted rather than clean. So where the base really comes from is still
undecoded; this fixes the one theme that is provably wrong and leaves the
others where they were rather than guessing a rule for all eight.

---

**153a** — Shadows, and a negative on the Rainbow Road flash.

**Shadows.** The port drew none - not under an object, not under a hopping
kart. A shadow is what makes a raised Thwomp read as overhead rather than
floating, and what makes a hop read as a hop. Both are now drawn as an
ellipse that darkens the ground at the sprite's GROUND position, so the
sprite lifts away from it.

LABELLED, ours. The game gives each object a SUB-BLOCK at +`$40` running
its own script (`$819174`), which is almost certainly the shadow - we model
neither the sub-block nor any shadow art, and none is obvious in the 57-tile
object sheet.

**The flash is NOT palette animation.** The user reports Rainbow Road's
Thwomps flashing colours to show they cannot be touched. Watching CGRAM
over 240 frames of an active Rainbow Road race:

* `$FA-$FF` - the object palette row - **never changes**;
* the only entries that move at all are `$83 $93 $95 $A3 $A5 $B3 $B5`,
  each alternating between `2525` and `35A9`, and those are KART palette
  rows, not object ones.

So whatever produces the flash, it is not the game rewriting the object's
colours. `+$06` in the block is not it either - sampled over the same run
it reads 37, 38, 40, 41, 43, 45, 103, 510, 0, 0, 52, which jumps around
rather than cycling, so it is more likely a projection value than an
animation frame (superseding the reading in NOTES 152 that called it a
palette/frame counter).

Nothing is implemented for the flash. The remaining candidate is the
sprite's own palette BITS changing in OAM frame to frame, which the tally
in NOTES 153 could not isolate because karts, the HUD and tyre smoke
dominate the counts.

---

**154** — Three sightings on the movers: the height was ours, the anchor is
not what it looks like.

**1. "They do look too high" - fixed.** `smk_mover_px` converted the
object's `$1F` to SCREEN pixels using the kart's own rule (one screen pixel
per 25029 of a 16.16 z), which is only valid at the kart's own depth of 61
px from the eye. Reused at any distance it put a Thwomp a third of the way
up the screen. Height is now returned in WORLD pixels and multiplied by
`smk_project`'s scale, exactly as the ground and every sprite do, so the
lift shrinks with distance.

The unit: the kart's hop peaks at `$1F` = 1173 and reads 12 SNES px at
depth 61, where the scale is 256/61 = 4.20 screen px per world px. So 12 px
is 2.86 world px and **one world pixel is 410 units of `$1F`**.

**3. The anchor is NOT a projection error.** The report - a pipe looks like
it sits in the middle of the road when far off and "corrects" as you
approach - reads exactly like the sprite and ground projections
disagreeing. They do not. Projecting a world point with `smk_project` and
then inverting the Mode 7 renderer's own per-scanline law at that screen
pixel agrees to **0.00 px** at every distance from 40 to 400 world px and
every lateral offset tested:

    fwd  40 lat +60  d 101.0  (280.1,139.2)  ->  (552.0, 572.0)  err 0.00
    fwd 400 lat +60  d 461.0  (161.3, 62.3)  ->  (912.0, 572.0)  err 0.00

So the position is right and the SIZE is what is wrong. An object drawn
too large for its distance reads as a nearer object, and a nearer object
at that screen column would be closer to the road's centre line - which is
what the eye reports as a bad anchor. Same root cause as sighting 2,
"they appear too close".

**2. Still open: the size at distance.** The drawing is quantised into
three tiers, and every tier is magnified 2x (S15, labelled) because the
near size was measured at 23x31 SNES px against a sheet whose largest
drawing is 12x16. If that magnification belongs only to the near band, the
far bands are twice the size they should be, which matches the report. Not
guessed at here: the next step is to measure the object's real on-screen
size from OAM at a known distance, the way NOTES 139 measured the near one.

---

**154a** — The pipe that looked like it stood in the road, and the end of
S15.

The user, with two screenshots of our own render: a pipe that is plainly
off the road when you stop beside it looks like it is standing in the
middle of it from a distance. Also: pipes have no shadow in the game, and
the ellipse added in NOTES 153a is not the shape a Thwomp's would be
either.

**It is not the anchor.** Instrumenting the real draw loop
(`SMK_ENT_TRACE=1`) to project each drawn entity and then invert the Mode 7
renderer's own law at the pixel it drew the base on: the round trip returns
the entity's exact world position at every distance from 3 to 348 px. An
earlier version of this check only tested camera angle 0 and inverted with
`ca=1, sa=0`, so it could not have caught an angle-dependent error; redone
over eight angles the worst disagreement is **0.0001 world px**.

**It is the SIZE.** The same trace, before the fix:

    dist 342  size 24x24
    dist 120  size 28x28
    dist   3  size 32x32

A pipe 342 px away and one 3 px away drew within eight pixels of each
other. On a road only a few pixels wide at that depth, a 24 px sprite
covers it, and the base - correct to the pixel - is buried under a mass
that reads as mid-road. As you approach, the road widens on screen while
the sprite barely grows, so it "corrects itself". Exactly the report.

**The law, and S15 closed.** `$4200 / zf` is the game's own scale for an
object (NOTES 129). Read as 8.8, the drawn size is `art * $4200 / (256 *
zf)`, and it lands on the one size ever measured: NOTES 139 put a real pipe
at 23 x 31 SNES px against a 12 x 16 drawing, and

    12 x 16 * ($4200 / (256 * 34))  =  23.3 x 31.1

So there was never a missing 2x art source. S15 invented one to explain a
number that falls straight out of the scale law we already had; at that
reference distance the correct scale simply IS 1.94. The band thresholds
(`$84DA3C` = C0 60 30) now read as the drawing swapping at 12, 6 and 3 px
tall before the cull at zf = 352, which is what a 1/distance law should do.

After: 48 px at 8 px away, 24 at 44, 13 at 79, 3 at 348, gone past 352.

**The entity DATA is confirmed right.** The user, on the same screenshot:
of the two pipes, "the one on the right is in the track, the one on the
left is not". Our decode says exactly that - entity 0 at `(268,92)` sits on
surface `$40`, road, 29 px from the edge, and entity 1 at `(164,132)` on
`$54`, off-road, 4 px from the edge. So Mario Circuit 1 really does stand
one pipe on the track and one beside it, NOTES 078's coordinate check holds,
and nothing here was a placement bug. The whole report was the size.

**Shadows removed.** Pipes have none, and the ellipse was wrong for
Thwomps too - drawing the wrong thing is worse than drawing nothing. The
real one is presumably the object's sub-block at +`$40`, which runs its own
script (`$819174`) and which we do not model. The kart's hop shadow stays,
since that one was asked for and there is nothing else standing in for it.

---

**154b** — Objects scaled from the wrong origin, and the test track that
showed it.

The user asked for a scaling ruler: a straight Mario Circuit road with a
line of pipes down the middle, so one frame shows how objects scale against
the ground's own perspective. `--scaletest` builds exactly that (road along
+X, pipes every 40 world px from 40 to 400, all drawn rather than the live
pair). It is worth keeping - the bug is obvious in the picture and invisible
in any single screenshot.

Their diagnosis was right: "pipes look very small in far view, they remain
the same until you are relatively close, then scale up faster than any
element on screen, so they look like they are getting bigger by magic, not
approaching."

**The cause.** Objects were sized by `$4200 / zf`, where `zf` is the depth
ahead of the KART. Every other scale in the renderer - the ground plane,
the karts - is `1 / d` where `d = zf + 61`, the depth from the EYE, which
trails the kart (NOTES 083/084). Over 40 to 400 world px:

    ours          26.4 -> 2.6 px    shrinks 10.0x
    perspective   40.6 -> 8.9 px    shrinks  4.6x

So distant objects were about half the size they should be and grew far too
fast on approach. An object is now `SMK_OBJ_PIPE_W x SMK_OBJ_PIPE_H` world
pixels drawn with `smk_project`'s scale - the same law as everything else,
so it cannot drift out of step with the floor it stands on. `$4200/zf` still
picks the BAND, which is what it is for: a depth cue, not a size.

**A correction to NOTES 154a, and the lesson in it.** That entry closed S15
on the grounds that `12 x 16 * ($4200 / (256 * 34))` = 23.3 x 31.1 matches
the 23 x 31 measured in NOTES 139. The arithmetic is right and the
conclusion was wrong: **one measured point cannot determine a law, only
calibrate one.** `1/zf` and `1/(zf+61)` can both be made to pass through a
single sample, and they disagree everywhere else. It took a scene with ten
pipes at known distances to tell them apart - which is why the ruler the
user asked for was the right instrument and a screenshot was not.

---

**155** — Every object, all the time.

The user: "given that we have better resolution than the original game, and
a better road angle, I would like the pipes and objects to be there always,
and avoid having them appear out of nowhere."

Two separate things made them pop:

* **the live pair.** `$819136` keeps two object blocks in a one-player race
  and `$84DC20` respawns them when the lap segment changes, so an object
  materialises as you cross a threshold and the one behind you vanishes.
* **the cull.** `$84DA18` walks `$84DA3C` = C0 60 30 00 and stops drawing
  past the last threshold, zf = 352.

Both are budgets - OAM slots and a 256x224 screen where the third band is
three pixels tall - not facts about the course. So by default every entity
is now drawn, at any distance, keeping its last drawing and simply getting
smaller until it is sub-pixel or above the horizon (which `smk_project`
already rejects). `--rom-spawn` restores the ROM's behaviour. Ledger S23,
the same standing as S7's full-resolution perspective.

**Collision follows drawing.** Whatever is drawn is what you can hit -
NOTES 151 was the bug that came from those two disagreeing, and this change
would have re-created it if collision had been left on the live pair.

**Mover state is now per ENTITY**, not per live slot, since there are no
longer four slots to index. They are also STAGGERED: with a whole row live
at once, every Thwomp slamming in unison looks mechanical, and the two the
oracle caught ran periods of 270 and 294 frames, so they are not in phase
in the game either. LABELLED: the offset is ours - the real per-object
timing is the script data NOTES 152 could not pin.

---

**155a** — Chasing the "pipe in the middle of the road" a second time, and
the instrument for settling it.

Reported again with fresh screenshots: a pipe looks like it is in the road
when far off, and beside it when you pull up next to it.

Checked three ways, all of which say the BASE is placed exactly:

* analytically - `smk_project`'s lateral term is `xr * (Les*w/256) / d`,
  the exact inverse of what the Mode 7 renderer walks across a scanline;
* against the ground renderer's own law at eight camera angles - worst
  disagreement **0.0001 world px** (NOTES 154a; the first version of that
  check tested only angle 0 and was worthless);
* in the live draw loop, projecting each entity and inverting at the pixel
  the base was drawn on - returns the entity's own world position at every
  distance from 3 to 348 px.

And a controlled scene: `--scaletest` now lays TWO rows of pipes, one down
the centre line and one 8 world px inside the left edge. The edge row hugs
the edge at every distance; the centre row sits on screen centre at every
row (measured off the frame: road centre 256-258, centre pipes 256).

**What our data says.** Mario Circuit 1's entities are not all at the
roadside. Entities 0 `(268,92)`, 2 `(188,52)` and 3 `(252,124)` stand on
surface `$40` - road - 29, 28 and 5 px from the nearest edge, while 1, 4,
5, 6 and 7 are off-road 4-5 px out. NOTES 078 verified those coordinates
against the game's own live entity blocks. So a pipe seen far up the track
that looks like it is in the road may simply BE in the road, and the one
you stop beside is a different, roadside one.

**The instrument, so this stops being an argument about pixels.**
`--obj-marks` draws a magenta cross at each object's projected ground point
and cyan ticks where the road's edges fall at that same depth. If a cross
sits mid-road while the pipe belongs at the side, the placement is wrong
and the marks prove it; if the cross is at the edge and the pipe still
looks central, it is the sprite's width, not its anchor.

---

**155b** — The pipe really was in the middle of the road, and it was the
same pipe. The anchor was hanging off the wrong edge of the sprite.

Reported three times, and I explained it away twice - first as the size,
then by suggesting two different pipes were being compared. It was one
pipe, and the cause is this:

The three band drawings do not fill their 16x16 block the same way.

    band 0 (near)  ink rows  0..15   no blank rows below
    band 1 (mid)   ink rows  1..14   ONE blank row below
    band 2 (far)   ink rows  2..13   TWO blank rows below

`draw_entity` hung the RECT's bottom on the projected ground point, so the
mid and far drawings floated above the road by 1/16 and 2/16 of their
height. Measured on the test track, that is **3 to 4 screen pixels**.

Near the horizon that is not a small error. Depth is `K / (line - H)`, so
its slope is `-K / (line - H)^2`, and at frame line 24 **one screen pixel
of vertical error is 88 world pixels of depth**. A pipe floating three or
four pixels reads as several hundred world px further up the track - and
further up the track is nearer the vanishing point, where the road is
narrow and central. Hence: in the middle of the road. Close up, band 0 has
no blank rows, the same pipe sits on the ground, and it looks correctly
placed at the roadside.

Fixed by anchoring the INK rather than the rect: find where the drawing
actually ends inside the block, put that row on the ground point, and
centre on the ink's own columns rather than the rect's.

**Why three checks missed it.** All of them tested the PROJECTION - is the
computed ground pixel right? It always was, to 0.0001 world px. None of
them asked whether the sprite is drawn where the projection says, which is
a different question and the one that was wrong. `--obj-marks` draws the
projected point precisely so the two can be told apart by eye; it should
have been the first thing built, not the fourth.

**156** — The shadow, and how high a Thwomp actually gets.

The user, on Bowser Castle: the Thwomps are missing their shadow, the
height they reach is lower than in the game, and — later, decisively —
"shadow is exactly the same for all objects, because it is an oval, so
maybe it is just generated?" and "shadows are flickering black ovals,
meaning that they are in a different logic (visible every other frame)".

**The shadow is not generated, and it is not in the theme sheet.** It is
two 4bpp tiles in the shared sprite blob `$C1:0000` at decompressed offset
`$120` — the same blob the HUD comes from. The game blits them into
object-sheet slots **43/44**, and slots 43–47 are blank in all six
distinct theme sheets: that is exactly why one oval serves every object.
OAM assembles it as four 8×8 sprites, tiles `$0EB $0EC $0EC $0EB`-mirrored,
palette 5 index 14 = `$0000` — pure black, no shading, a 32×8 ellipse.

It is drawn on **alternate frames only** — measured: the strip is present
on every odd frame of a kart hop and on no even one. That is the SNES
faking translucency, since sprites cannot blend. The port renders the
*result* of that flicker, a 50% darkening, because at an unlocked frame
rate a real flicker strobes rather than shades. LABELLED divergence; the
art and the colour are the ROM's.

**The height.** `SMK_MOVER_UNIT` was 410, from a SINGLE reading. Hopping
the kart in the oracle and logging every frame of the arc
(`tools/labs/hop_shadow.py`) gives sixteen (`$1F`, lift) pairs over
`$1F` = 48…856:

    lift = $1F / 65.3 screen lines,  rms 0.39 px

at the kart's depth of 61. The fit is confirmed by geometry it was not
given: it implies a ground line of `20.36 + 4972/61 = 101.9`, and
`SMK_PLAYER_LINE`, measured independently, is 102.0. One world pixel is
therefore `65.3 × 256/61 = 274` units of `$1F`, not 410 — every Thwomp
sat **1.50× too low**.

**A second bug, in the projection rather than the constant.** The lift is
a VERTICAL quantity but was multiplied by `smk_project`'s `sc`, which is
horizontal (`rw/d`). Those agree only when the window is the view's own
256:112. Rendering the user's exact save-state pose at their screenshot's
1350×505 and measuring both images the same way:

    projected ground row 354        real 362 (shadow bottom)
    Thwomp feet    real y 141        port y 102  (before)  y 136 (after)
    lift           real 47.2 lines   port 55.9 (before)   48.3 (after)

**Why four sweeps found nothing first.** The oracle was driven in a GP
race and the Thwomps never emitted a sprite at any distance, so OAM was
searched for something that was not there. Reading the block's own `$30`
was the wrong instrument — it stays parked at `$0140`, and `$2C` does not
map to screen x by any fixed offset. The kart's own hop is the same
mechanism and is reachable in eight frames; it should have been the first
experiment, not the fifth. The user's two observations — the oval is
shared, and it flickers — are what redirected it.

**Still open.** At this pose the port draws a Thwomp **157 px wide against
the game's 78** — 2.05× too big, measured on matched frames. That is the
same complaint as "our thwomps are more blocky/pixelated than in real
game": a 12×15 drawing magnified twice as far as the hardware ever
magnifies it. Not changed here; it moves every object, pipes included.

**157** — The near drawing: an object is not one 16x16 sprite.

User, after the height and shadow work: "pipe scaling is finally right.
dont touch it, ever. I also checked the thwomp's scaling and is also
correct. The only thing that is incorrect in both is the sprite shown when
getting closer. We are only scaling the sprite used for far away objects."

Exactly right. Band 0 is a **32x32 metasprite**, built the way this game
builds every symmetric thing (the kart is $180/$180-H over $1A0/$1A0-H,
the shadow $0EB $0EC $0EC $0EB-H):

    [ base 0 | base 0 mirrored ]      sheet tiles 0,1,16,17
    [ base 2 | base 2 mirrored ]      sheet tiles 2,3,18,19
    ink 24 x 32

Measured off two uncropped 1444x1036 frames the user captured, by matching
each sprite's row-width profile against every assembly the sheet can make.
A Bowser Castle Thwomp at band 0 measures 21.8 x 30.5 body px and picks
this pair at rms 2.50 against 4.30 for the next candidate; a Mario Circuit
pipe picks the SAME pair independently at rms 3.78. Two themes, one rule.

**Why it was never found.** Bases 0/2/4/6 were dismissed (NOTES 139, and
the TIER comment in main.c) as "skewed perspective variants" because base
0's ink is right-aligned at x 4..15. That is precisely what the LEFT HALF
of a mirrored pair looks like. Reading a half-sprite as a whole drawing is
what left the port magnifying the far drawing at every distance - and what
made a near Thwomp read as blocky, since a 12x15 drawing was being blown
up three-fold.

**The size did not change and must not.** The ink fills the same fraction
of its block as the 16x16 drawings do - 24/32 across, 32/32 down, against
12/16 and 16/16 - so the new art goes into the same rect at the same size.
Measured on matched poses: the port draws 21.5 SNES px wide where the game
draws 21.8, and 30.7 tall against 30.5.

**Also measured, NOT applied.** The SNES cannot scale a sprite, so in the
real game the drawn size IS the art size - which makes the far pipes in
that frame readable directly: 12.1 x 16.6 and 11.2 x 14.7 body px, against
the sheet's base 32 at 12x16 and base 34 at 11x14. So the game's bands 1
and 2 are bases **32 and 34**, where the port uses 34 and 36. The port
scales continuously, so this changes detail and not size; left alone
because the user has approved how the far objects look, and their word on
that outranks the inference.

**158** — The best drawing at every distance, and why the ladder was a
size bug as well as a resolution one.

User, after 157 landed: "they are looking good now. But they flip to the
good sprite too near the player so it looks also like some magic happened
... let's use the last sprite - the one with the best resolution - all the
time, even if they are super far away. That was a limitation back in the
day but right now is no longer needed in this port."

The flip was not only a change of detail. The drawings do not fill their
blocks by the same fraction:

    near metasprite   ink 24/32 across, 32/32 down
    base 34           ink 11/16,        14/16
    base 36           ink 10/16,        12/16

and the drawn size is that fraction of a rect the projection sizes. So
every band boundary was a STEP - about 9% wider and 14% taller crossing
into band 0, more again at the next. The object grew in jumps as you
approached, on top of growing smoothly, which is the "magic" the user
reported here and, in a different guise, for the pipes back in NOTES 154b.

Using one drawing at every distance removes the steps by construction:
one art, one ink fraction, size = 16 world px x the projection's scale, so
the drawn size is continuous everywhere and cannot pop. Measured on the
scaletest straight, width x depth over the clean single pipes reads 12081,
12112, 12241, 12152 - flat to 1.3%, which is the segmentation noise.

The near field does not move: band 0 already used this drawing, so
everything the user has approved close up is untouched. What changes is
mid and far, which grow slightly to become CONSISTENT with the projection
- they were the ones out of step.

LABELLED divergence, alongside S7 and the always-visible objects: the
ROM's ladder exists because the SNES cannot scale a sprite and runs out of
sprite budget. We can scale, and we are not short of budget, so the ladder
buys nothing and costs both resolution and continuity. The band arithmetic
itself stays measured and stays tested - it is still the ROM's rule, we
simply no longer need to obey it.

**159** — How big an object actually is, and a check that was circular.

User, seeing the objects in motion after 158: "Now they look bigger than
real game (you mentioned something like 2.05x?)".

They did, and I had talked myself out of it. NOTES 157 reports the port
drawing 21.5 SNES px where the game draws 21.8 - a match. That check was
**circular**: the depth of the reference frame was inferred from the
port's OWN size law (ink 23.5 -> pw 31.3 -> d 131) and then the port was
rendered at that depth and found to agree. It could not have disagreed.

Taking the depth from the geometry instead - the ground row the sprite
stands on, via depth = 4972/(line - 20.36) - the real game gives:

    Thwomp  depth  72.1  ink 23.8 px -> 6.71 world px
    Thwomp  depth  66.1  ink 23.8    -> 6.15
    pipe    depth  57.8  ink 22.0    -> 4.97
    pipe    depth  64.9  ink 21.9    -> 5.54
    pipe    depth 136.9  ink 14.1    -> 7.52
    pipe    depth 145.6  ink 13.2    -> 7.49          mean 6.40

against the port's 12.0. So 1.9x, which is the 2.05x measured at the very
start. The spread is not noise: it is the band quantisation, since the
game holds one drawing across a range of depths and the implied world size
therefore falls as you close on it. A continuously scaled port has to pick
one number and 6.40 is the mean of what the hardware brackets - so
SMK_OBJ_PIPE_W is 8.5 and 0.75 * 8.5 = 6.38.

**The height is a SHAPE, not a world size** - and getting it
geometrically right is what made it look wrong. Keeping 16 left the port
drawing ink 23.0 SNES px wide by 28.0 lines, both close to the game's 23.8
x 32.5, and yet the user's next frame showed elongated capsules: "you
wanted to make them taller but actually they need to be shorter. Is there
any possibility that now they are not as wide as they should?"

No - the width was right, 23.0 against 23.8. The height was the problem,
and the cause is the port's own view. It renders the 256x112 race view
into 512x448, so a LINE gets 4 host px where a horizontal SNES pixel gets
2: the road is stretched 2x vertically, which is the better road angle the
port exists for. A billboard projected honestly into that stretched space
comes out twice as tall as it reads on a SNES. On screen ours was aspect
0.39 where the game reads 0.87, which the eye calls narrow even though
every world dimension was right.

So the height is set from the SHAPE the game presents rather than the
height it occupies in a stretched world. The game's object is 123 x 141 px
in the user's capture, aspect 0.87; the drawn ink is 0.75 * pw by ph in
square host pixels, so ph = 0.75 * pw / 0.87 and H = 0.862 * W = 7.4. The
kart is drawn the same way - square host pixels per art pixel - so objects
and karts now share one convention.

Verified at the save state's own pose: 43 x 51 host px, aspect 0.843
against the game's 0.872, ink 23.5 SNES px wide against 23.8.

The lesson is worth keeping separately from the number. Three times now a
quantity has been geometrically correct and visibly wrong because the port
does not render the SNES's proportions - the lift (NOTES 156), the sprite
height here, and the band steps in NOTES 158. Anything VERTICAL in this
renderer has to be checked against what it looks like, not only against
what the projection says.

The size constants feed drawing only; collision is untouched.

**160** — Pipes were not solid, and the reason NOTES 151 should have caught.

User: "I can pass through green pipes... no collision".

`smk_course_movers_reset` parks all 32 slots at `SMK_MOVER_PARK` = 4096
whatever the theme, and `smk_course_movers_step` returns early for a theme
with no movers - so on Mario Circuit a pipe's `mv[].z` sits at 4096 for the
whole race and nothing ever moves it.

Drawing never noticed, because `smk_mover_world` gates on the theme and
returned 0, putting the pipe on the ground. Collision did NOT gate:

    if (crs->mv[i].z > SMK_MOVER_PARK / 2) continue;

which is every object on every non-mover track. They drew on the floor and
you drove straight through them. Reproduced with the crash harness: the
kart closes to 1 px of the pipe at (268,92) with speed still climbing and
`bcool` never set.

From commit 3f0b5d5, when the movers landed - not from the drawing work
around it, though that is when the user hit it, having gone back to Mario
Circuit to capture the pipe reference frames.

NOTES 151 paid for this lesson once already, in the other direction:
"there should be two thwomps and there is only one, but you can still hit
the invisible one". The rule it set - whatever is DRAWN is what you can
hit - was right, and it was enforced only for WHICH objects are live, not
for WHERE they are. A per-slot height that drawing and collision each
computed for themselves was free to disagree, and did.

Fixed by making the height come from one accessor, `smk_mover_z`, which
both now call. For a mover theme it returns `mv[slot].z` exactly, so
Bowser Castle and Rainbow Road are bit-identical to before; for any other
theme it returns 0. Pinned by a self-test that asserts the raw z is still
parked while both readers see the pipe on the ground, AND that a parked
Thwomp is still overhead to both.

---

**161** — The starting grid is a stored table after all, and NOTES 142b
looked one indirection short.

The user's report: on Mario Circuit 1 the time trial starts the kart off
the road, pointing across it. True, and the worst case of a known
shortcut — S2 synthesised the grid from the finish-line rectangle, and on
track 7 that lands (897,604) where the game puts (952,756): 161 px out
and 32 degrees rotated.

*Watching it, instead of reading it.* NOTES 142b's next step was the
right one and it was never taken. Hooking the oracle's bus and recording
the PC of every write to `$1018`/`$101C`/`$102A` (`tools/labs/gridpc.py`)
names the writer in one run — and it is not `$819212`:

    $81:906A  sta $18,x     x
    $81:907B  sta $1C,x     y

`$81903C` is the grid builder, and it is trivial:

    tax                       ; A = the course's placement record
    ldy #$010E                ; the grid order - kart block per slot
    inx : inx
    lda $0000,x -> $06        ; x0
    lda $0004,x -> $08, $10   ; x step
    lda $0002,x -> $0A        ; y0
    lda #$0018  -> $0C        ; y step, a constant
  slot:
    ldx $0000,y               ; kart block, 0 ends the list
    lda $06 : sta $18,x  : $06 += $08 : $08 = -$08
    lda $0A : sta $1C,x  : $0A += $0C

So eight karts, one per row 24 px apart, x alternating between two
columns `x step` apart, and `$2A` never written — which is why every
course's grid faces -Y and why the record carries no angle.

*The record, and the indirection that hid it.* NOTES 142b searched the
ROM for the measured coordinates as word pairs and found nothing. They
are there, and they are literal:

    $81:8A79 + track*2   ->  setup entry
    entry word 0         ->  placement record
    record word 0        ->  the placement ROUTINE   ($8F79 GP, $9016 battle)
           words 1,2,3   ->  x0, y0, x step

Two levels of pointer, not one — and the coordinates sit at record+2,
which is why `inx inx` is the first thing `$81903C` does. Track 7 is
`(920, 588, +32)`; the 20 GP courses all carry `$8F79`, the four battle
arenas `$9016`, which is a different builder reading corner pairs.

`$819207` — NOTES 111's lead, and NOTES 142b's dead end — really was
unrelated: it stores position into `$28`/`$2A`, which for a kart is the
heading. It was the wrong structure all along.

*The time trial is not a grid slot.* Both recordings sit a few pixels off
every slot, which is what the user actually reported. `$818F7F` explains
it: build the grid, then call `$819003` on the FRONT kart alone with
`A = #$FFF0` and `$10 = step >> 2` — `y += -16`, `x += step/4`. That is
exactly `demo_tt_track19.csv` frame 0 `(136,524)` against the record's
`(144,540)`, and `gv1_run.csv` `(960,592)` against `(952,608)`.

*Verification.* `tools/labs/gridtable.py` reaches an arbitrary course the
NOTES 118 way (hook the reads of `$0150`/`$0152` so mode entry computes
`$0124` AND the theme) and reads all eight karts while the countdown
still holds them; `gridcheck.py` diffs that against the table walk. All
twenty courses match the game position for position, all eight karts. The selftest now asserts
the strong form — all 160 GP grid slots and all 20 solo starts on
drivable ground, where the old synthesised grid missed on 5 of 24 — plus
track 7's three known positions to the pixel.

Deleted with it: `smk_track_start` and `smk_track_guess_start`, the
fixed-coordinate grid and its road-finding fallback. There is nothing
left for them to guess at.

---

**162** — Lakitu, his light, and the twenty-seven frames nobody expected.

NOTES 145a left this as an instruction for whoever picked it up: *boot the
Python oracle to a race and render the OBJ half of its VRAM, because MAME
exposes neither OAM nor VRAM to Lua.* That is all it took.

`tools/labs/lakitu.py` boots a race, waits for `$0146` to be armed with
-336 and then records the whole 544-byte OAM every frame. The countdown
stops being a question and becomes a reading:

    OAM 11-14   Lakitu: four 16x16 sprites in a 32x32 block at a FIXED
                screen x of 36, every one H-FLIPPED, palette 5 ($D0).
                Tiles $42/$40 over $46/$44.
    OAM  8-10   the light: three 8x8 sprites at x 63, hanging 16, 24 and
                32 px below his block's top, palette 4 ($C0).

*The art was already half-loaded.* The lamps are sprite tiles `$FB-$FE`,
which are not in the object set (`$C0-$F8`) and not in the HUD set
(`$40-$BF`) - they are in the HUD's own stream at `$C1:0000`, in the
first `$200` bytes, the block `smk_hud_load` skipped to get to offset
`$200`. Sixteen tiles, uploaded to `$EF-$FE`, matched one for one
against the oracle's VRAM. `$FD`/`$FE` are a red lamp dark and lit,
`$FB`/`$FC` a green one. Two reds over one green.

*The script, in frames from the arm:*

      1   he drops in from y = -48
    113   settled at y = 5, having overshot to 7 and come back
    179   the first red
    244   the second red
    309   the green - and he changes to the cheering pose with it
    336   THE FIELD IS RELEASED
    377   he starts climbing back out
    439   parked at y = -40, clear of the screen

*The twenty-seven frames.* The green is not the release. It lights on
309 and the field goes on 336: the AI karts all begin to accelerate on
that frame and `$3A` steps 4 -> 6 on it, and NOTES 145's human runs first
move on 339. So the green is an anticipation cue, worth nearly half a
second, and a player timing a launch is timing against it rather than on
it. Checked three ways rather than assumed, because it reads wrong.

*What is NOT decoded, and is labelled at the table.* The trajectory is
the one the game produced, frame by frame, not a law. Its generator was
hunted for: the only WRAM word that tracks his sprite y across the whole
sequence turns out to be the OAM shadow buffer at `$0220`, which is the
output, not the source. So `src/lakitu.c` carries the measured cycle -
the same choice NOTES 152 made for the movers, and for the same reason -
and `tools/labs/lakitu_full.py` regenerates `src/lakitu_track.inc` so the
next reader can re-derive it instead of trusting it.

Two smaller things fell out. `$0142` is not the animation driver NOTES
145a guessed at: it is loaded with 208 at `$80A1C5`, decremented by
`$8097A9` once every second frame (`$8094BB` returns early on odd frames,
which is where the "every second frame" comes from), and it only fires a
state change when it hits zero - a duration for the whole sequence, 416
frames, not an index into anything. And the 3-2-1 digits are gone: the
game never showed any.

Still open, and the user's own framing of it: the light "with the sound"
is how you time the launch. The port has no audio at all, so half the cue
is still missing. S18 in the ledger.

---

**163** — The countdown has its OWN rev routine, and three of NOTES 145's
numbers were wrong.

The user, after playing the port with Lakitu in it: *"I don't see that the
game lets you accelerate during count down. As soon as lakitu appears, I
should be able to rev up. If I rev'ed up too much before the green light,
I can't move, but just slide on my spot as my wheels can't get grip
(smoke coming, small animation). If I accelerate just in the right
timeframe, I get launched with a turbo boost."*

Every part of that is in the game and only part of it was in the port.

*The measurement, which cost nothing.* The `starts` recording - their own
four starts - has been in the repo since NOTES 143, and MAME replays it
under any script we like. `tools/labs/mame/revlog.lua` logs `$C2`, `$E0`,
`$E2`, `$EA`, `$EE` and `$AC` against `$0146` for all four, and the
oracle reproduces it independently once the pad bit is right:
**accelerate is B, bit 7 of the HIGH byte `$4219`**, not `$4218`, which
is why two earlier attempts to drive P1 from the oracle looked like the
kart ignored the throttle.

*Correction 1 - the build rate.* NOTES 145 read "a flat 96 a frame". It
is **+192 on every second frame**. The average is the same, which is why
the check passed; the granularity is not, and the granularity is what
decides which side of the turbo band a press lands on.

*Correction 2 - the row was never in play.* NOTES 143 found `$0E22` =
512 under `$2000` and `$0E24` = 64 over, and NOTES 145 logged that
neither produces the measurement. They belong to `$80B121`, the IN-RACE
builder. **While the lights run the game uses a different routine
entirely**, `$80959F`-`$8095E0`, and its rates are immediates:

    $02 negative (throttle held)
      $8095D5  $C2 += $C0            and $4F00 SETS the wobble flag
      $8095C4  flag set: $C2 -= $280 until under $3F00, which clears it
    $02 positive (released)
      $8095AC  $C2 -= $180, floored at $0100

*Correction 3 - it does not peg, it oscillates.* NOTES 145 noticed the
over-revved run "wobbling around 19-20k" and called it not modelled. It
is that flag: +192 up to `$4F00` = 20224, then -640 down to `$3F00` =
16128, and round again. Nothing else needed.

*And the release snaps the rev.* On the frame the field goes, an
over-revved `$C2` is set to exactly `$3000`: 19264 -> 12288 in the user's
run, 19584 -> 12288 in the oracle's. So the wheelspin lasts the same ~37
frames however badly you over-revved, instead of proportionally longer.
The port had it bleeding from wherever the rev sat, which at the ceiling
would have been four times too long.

*The two effects that were missing, and why nothing was visible.*
`$80B0EE` does three things and the port had one of them:

    $80B0FD  $C2 -= $70                 the bleed - ported
    $80B104  $E2 |= $20                 <- the SMOKE.  That is the very
             bit smk_effects_pick reads as a deep drift, so the wheelspin
             borrows the drift's own ground-effect object
    $80B10C  and #$FFDE                 clears bits 0 AND 5 when it lets go

and bit 0 is not bookkeeping either - `$80AB94`'s accel path reads it,
`A = (flags & 1) ? $C0 : accel[..]`, so a penalised kart is **not stopped
dead, it creeps**: sub-unit acceleration, "speed 0,1,2,3..." in NOTES
143's log, which is the user's "slides on its own spot". The port zeroed
the acceleration outright, so there was no creep, no smoke, and with no
sound either, nothing whatsoever to see. The mechanism had been right
since NOTES 144 and completely invisible.

Ported and pinned: the self-test reproduces the user's own three readings
(11008 normal, 11776 just-missed, 11968 turbo) from the press frame
alone, and the penalty run snaps to `$3000`, smokes for 37 frames, creeps
to speed 27 - the game's own 27 - and lets go.

The turbo window is **two ticks, four frames**: press at f214..f217 of
336. In the light's terms that is 95 frames before the green, which is
worth saying out loud - the green is not the cue for the rocket start,
and with no engine sound the port still cannot give the player the cue
that is. S18's sound half is what is left.

---

**164** — Single race, and the grid runs the other way round.

The user wants races, not a cup: *"a new mode: single race only.  No items
for the moment.  Just AI players and their initial fixed positions
depending on the character chosen by the player.  Stage selector is the
same as the one we already have for time trials."*

Nearly all of it was already built and just never wired together - the AI
step, the per-character grid order (NOTES 111), the rank, the eight kart
sheets, the coin rule, the course-by-cup selector. What was wrong was
WHERE the eight karts stood.

*The grid is indexed backwards.* `smk_grid_order` returns characters by
kart BLOCK - `out[0]` is P1's `$1000`, `out[1]` the rival's `$1100` - and
the port had been reading that index as a grid row, so the player started
on the pole and the field behind him. NOTES 161's own capture says
otherwise, and it has been sitting in the data since:

    block $1000 (P1)  ->  (952,756)  =  y0 + 24*7   the LAST row
    block $1700       ->  (920,588)  =  y0          the pole

So the block index counts backwards from the front, `slot = 7 - block`,
and the player starts eighth. That is `SMK_GRID_SLOT`, and the self-test
pins both ends of it against the measured positions.

Everything else is a wire: the mode row hands `SMK_MODE_GP` to the same
`load_race` the time trial uses, which already draws seven opponents,
steps them, gives the ROM's two starting coins and withholds the time
trial's mushroom. `smk_race_rank` is taken once at the finish - the HUD's
per-frame value keeps moving while the AI carry on - and the results
screen shows the place instead of the record line.

Not built, deliberately, and the user said so: no items, no AI
personality, no cup. A race is a thing to test against now.

---

**165** — Eleven courses had no opponents, and the same list was dropping
obstacles too.

The user, one race in: *"there are some tracks where there is no AI
players on the grid."*  Reproduced by shooting the grid on all twenty:
nothing on 0, 2, 3, 5, 9, 10, 11, 14, 15, 17 and 19, the whole field on
the other nine.

The placement is not the cause and was ruled out first - tracks 7 and 14
share a grid record to the pixel, and printing both gives identical
positions, characters and surfaces, yet one draws seven karts and the
other none.  So it is the drawing, and something per-track that is not
geometry.

It is the draw list.  NOTES 128 made everything on the plane share ONE
depth-sorted pass so a nearer sprite of any kind can cover a farther one,
and the array it sorts into was declared

    struct { float dep; int kind, idx; } item[SMK_CHARACTERS + 8];

sixteen entries - while the course entities are enumerated into it FIRST,
guarded by `n < sizeof item / sizeof item[0]`.  So on any course with 16
or more entities the list was full before a single kart was considered,
and the guard silently skipped the entire field.  The predicate is exact:

    nent >= 16  ->  no opponents drawn

and eleven courses qualify, from track 5's 16 to track 3's 19.  It has
been there since the sort landed; the time trial never showed it because
a time trial draws no opponents at all, and it took a race to make eleven
courses look empty.

The second half is quieter.  With only sixteen slots, a course with 17-19
entities also lost the last few OBSTACLES, so NOTES 155's "every object
on the course is drawn" was untrue on exactly those tracks.  Both halves
go away together.

Sized properly now: `SMK_DRAW_LIST` = `SMK_COURSE_ENTS` + `SMK_CHARACTERS`
= 40, with the entity cap named rather than a bare 32 in two places, and
a self-test that walks all twenty courses and asserts the busiest one's
entities plus the whole field still fit.  A cap that silently drops the
thing you are looking for is worth a test even when it is currently
generous.

---

**166** — Kart against kart: it does exist, and NOTES 112 closed the case
on a race where nobody touched.

The user: *"collision detection and reaction between AI players against
the human player and between them."*

NOTES 112 had ruled it out - "no kart-to-kart response exists in the
demo", from two AI karts passing within 3 px and P2 within 7 px of one,
with nothing reacting. That is one recording of a race in which nobody
leant on anybody, and the conclusion was wrong.

*Finding it, from both ends at once.* Statically: a two-body response has
to write the OTHER kart's state, so search for `sta $0022,y` - the
velocity of the kart Y indexes. Three sites, and `$819BBD` is one.
Dynamically: put P1 on the same pixel as an AI kart in the oracle and
record the PC of every write to either block. `$819BBD` fires, once. The
same routine from both directions, in about ten minutes.

*The test, `$81982A`.* `|dx|` and `|dy|` inside the ROM's `d + 4 < 8`
window, so **[-4, +3] pixels** on the two karts' centres - which is about
one kart wide, a kart being some 8 world px. Then: neither kart in the
pair's cooldown (`$5E`), neither stuck (`$5A`), both within `$0420` of
the ground. A broadphase over two position-sorted linked lists feeds it,
which is why the window is not symmetric: which kart is X depends on
which list is walking.

*The order, `$819867`.* Mark the pair - `$5E` = 8 on both - and sort them
so the HEAVIER is X, by `$4E`, which `$81923A` loads from the table at
`$81:9277` indexed by object type. Its first eight entries are the
drivers, and they are SMK's weight classes:

    Bowser $1B   DK Jr $1B   Mario $1A   Luigi $1A
    Peach  $19   Yoshi $19   Koopa $19   Toad  $19

*The answer, `$819B06`, by the weight difference.* All four branches, each
measured in the oracle:

* **equal** - EXCHANGE the two velocity vectors (`$819CB8`). Two karts on
  one pixel carrying `(0,-600)` and `(0,-400)` came back with exactly
  each other's velocity.
* **heavier, and faster** (`$819C0D`) - the heavy kart keeps its line and
  pays half the closing speed; the light one is flung off its shoulder at
  that speed plus `$20`, `$1800` off the line. To the unit: 600 against
  400 left the heavy kart on 500 and the light one on 632 at 33.75
  degrees.
* **heavier, rammed from behind, two classes apart** (`$819BE4`) - the
  heavy kart is not touched at all and the rammer is turned `$1000` and
  cut to a QUARTER. Measured 99, where a quarter of 400 is 100.
* **one class apart, rammed** - the plain exchange.

*What the port had to add.* An exchanged velocity means nothing if the
next frame rebuilds it: both `smk_kart_face` and `src/player.c` derive
`$22`/`$24` from speed and heading. So the pair's own eight-frame window
does double duty and holds the vector, exactly as the wall bounce's does
(NOTES 044) - and the player's path, which never calls `smk_kart_face`,
runs the same countdown itself.

*The one disagreement, labelled as S24.* `$819CC9`'s separation is
indexed `$14,x` with x = 0 then 2, which does not pair the components the
way the loads at `$819B7F` do; and the single geometry the oracle could
make it fire in came back with `$80` off BOTH x components, which
separates nothing. That measurement is not clean - the partner is an AI
kart whose own frame ran too - and without a separation the field grinds
into a heap within a lap. What ships is the READING, which does the job
the routine exists for, with the disagreement in the source.


**166a** — Why it felt too aggressive: the re-contact path was missing.

The user, playing it: *"probably, between them bouncing is different,
less aggressive."*

Measured first, because `$819C28` really does branch on `cpx #$1000` -
the response reads `$2C` for the player and `$32` for everyone else - so
an asymmetry was plausible. `tools/labs/bump_ai.py` runs the same pair of
velocities twice, once with P1 in the pair and once between two AI karts:
the equal-weight case comes back **identical**, an exact exchange either
way. So the response itself does not care who is in it, and the feel was
coming from somewhere else.

It was `$819B06`'s very first instruction, which this port skipped:

    $819B06  lda $1C ; beq $819B0D ; jmp $9C93

`$1C` is the pair's cooldown ORed together, set at `$819848`, and the
gate above only lets a contact through while it is 0 or 1. So a SECOND
contact, in the tail of the eight frames the first one armed, does not
get the full answer at all - it goes to `$819C93`:

    $819C9A  both karts stopped     -> nudge the heavy one along its
                                       HEADING at $0180
    $819CA6  faster of the two < $C0 -> the same $0180, along its
                                       velocity angle
    $819CB7  otherwise               -> rts.  Nothing whatsoever.

At racing speed a re-contact therefore costs nothing, and the only time
the game pushes is when two karts have nearly stopped on top of each
other - which is exactly the case that needs unsticking. The port was
running a full velocity exchange every time the cooldown allowed one,
which is eight times a second for as long as two karts stay together.
That is what the user felt, and it would show most between AI karts,
because they run in a pack and stay in contact for whole corners.

Ported with both halves of `$819C93` and pinned: a re-contact at speed
leaves all four components untouched, and two stopped karts come apart at
`$0180`.

---

**167** — The rubber band: it is a target-speed ROW, and the port was
passing zero.

The user, describing the original: *"no matter how fast you are they can
keep up and stay behind you all the time... when one of them gets behind
their original position, they start to go faster (sometimes cheating)
until they catch up and get back to their place."*

All of that is `$80AD5E`, the AI's per-frame driver, and it was hiding in
plain sight - the routine at `$80ADE0` was looked at earlier in this same
session while hunting kart collision and dismissed as sprite priority,
because it returns `$0000`/`$0008`/`$0010`/`$0018`. Those are not
priorities. They are byte offsets into the target-speed table.

    $80AD8F  jsr $AD96            pick the row
    $80AD93  sta $C8,x            store it
    $80B074  target = $06B0[ (waypoint attr & 3) * 2 + $C8 ]

`src/ai.c` had the second line right - its comment already said "offset
by the kart's `$C8` row" - and then passed 0 for want of knowing where
`$C8` came from.

*How the row is chosen* (`$80AD96`). From the kart's RANK (`$E6`, from
the sort at `$80A047`, which also builds rank-ordered kart lists at
`$010C`/`$010E`/`$0110`), and the DSP-1 distance (`$80AF5F`) to the kart
one place ahead, compared at `$80AEFC` against a per-(class, rank) table
at `$80AF0F`:

    class 0:  $0080 $0080 $0040 $0040 $0040 $0040 $0060 $0080
    class 1:  $00A0 $00A0 $0050 $0050 $0080 $00A0 $00C0 $00C0
    class 2:  $00C0 $00C0 $0060 $0070 $0080 $00C0 $00E0 $0500
    class 3:  $00E0 $00E0 $0060 $0080 $00A0 $0120 $0500 $0500

*What the rows are worth*, read out of the ROM rather than named from the
branches, because naming them from the branches got it backwards:

                  50cc   100cc  150cc      (plain waypoint)
      CHASE $08    512     688     832     a kart that has lost touch
      EASE  $00    448     560     608     the leader, with clear air
      HOLD  $10    256     560     576     in the pack
      SLOW  $18    256     352     352     $80ADB0's state, not modelled

So the band pulls BOTH ways: drop back and you are handed the fast row
until the gap closes; get clear at the front and you are handed a slower
one. That is the user's "they keep up no matter how fast you are", from
both ends at once, and the "sometimes cheating" is `CHASE` being flatly
faster than anything the player's own class allows the field.

*And a second, flat correction underneath* (`$80B086`): if the `$DA`
timer is running the bonus comes from `$B099`, otherwise from `$B0A1`
indexed by RANK - `0, -2, -4, -8, -12, -16, -20, -24`. Leading costs
nothing; every place further back is a small penalty, which trims the
row's coarse steps.

*Measured through a race* with `tools/labs/rubber.py`, logging every
kart's row, rank, speed and distance to the kart ahead: karts in the pack
sit on `$10` at 548-784, a kart that has lost touch takes `$08` and runs
758-1002, and the threshold matches - at rank 1, class 0, a gap of 126
held station and 142 started the chase, either side of the table's
`$0080`.

*Ported*, with the two tables checked against the ROM in the self-test
(40/40 entries) rather than trusted. LABELLED: the class index is the
engine class, where the ROM uses `$C1,x & 7` and has four rows; `$DA`'s
meaning is still unknown, so the `$B099` correction is not ported and the
`$18` row is never selected.

Not verified in play from here: `--frames` runs in this shell block on
SDL - 61s of wall clock for 0.49s of CPU - so how the field actually
feels is the user's to judge.

---

**168** — Lakitu's lap sign, and the crossing that shows nothing.

The user wants him for the other three jobs he does: the lap sign, the
chequered flag, and fishing you out. This entry is the first.

*Captured the same way as the start* (NOTES 162) - drive the game and
read its own OAM - with `tools/labs/lakitu_lap.py`, which flow-steers P1
until the lap word `$C0` rolls over and then records.

**The first capture recorded nothing, and the reason is worth keeping.**
`$7F -> $80` is the GRID crossing: the field starts behind the line, so
that transition enters lap 1 without completing one (NOTES 052) and the
game shows no sign for it. A hundred and fifty frames of standing HUD,
with every Lakitu tile parked off-screen at x > 256. The sign belongs to
the crossing AFTER that, which costs a full lap of interpreter time to
reach.

*The assembly*, from the OAM at that crossing - four sprites, palette 5,
moving as one group from (X, Y):

    (X,      Y     )  16x16  $A0        the plate, "LAP" in yellow
    (X + 8,  Y     )  16x16  $A3 + n    n = lap - 2
    (X + 1,  Y + 16)  16x16  $46 HFLIP  his cloud, left
    (X + 17, Y + 16)  16x16  $44 HFLIP  his cloud, right

The digit is the subtle part, and reading the tile row got it wrong
twice. The digit sprite sits HALF over the plate: its left half is the
plate's own edge bar (`$A3`) and only its right half is the numeral, so
`$A4`/`$A5`/`$A6` - 2, 3, 4 - are what actually show. Rendering the two
sprites exactly as the hardware assembles them settles it: "LAP 2", one
digit, where the row reads "23".

*The path*, unwrapped from the OAM's 8-bit y, frame by frame from the
crossing: in from off the top-left at (5, -39), an arc down and right
peaking at (93, 44) around frame 88, and back out the way he came, gone
by 164. Generated into `src/lapsign_path.inc` by the lab, the same
standing as the start's own trajectory - MEASURED, not derived.

LABELLED: "FINAL LAP" is a 32x16 block at `$AC`/`$AE` over `$BC`/`$BE`,
read off the sheet and NOT captured - reaching the fifth crossing costs
five laps of interpreter time.

*And a lab bug worth the note.* `tools/labs/track_force.py` ran its own
demo at module level, so `from track_force import boot` booted a second
ROM as a side effect and swallowed the caller's `argv`. The rescue
capture spent twenty minutes forcing track 7 while asking for 16, and
reported nothing. It is behind `if __name__ == "__main__"` now.

Still to do: the chequered flag (its art is the checker across `$68`-`$9F`,
several frames of a wave) and the rescue - whose state machine has been
the ROM's since NOTES 113/124, so only the drawing is missing.

**168a** — And Lakitu fishing you out, which only shows on the way down.

Captured on Ghost Valley with `tools/labs/lakitu_rescue.py`, which walks
the kart off the road in four directions and lets the GAME decide it has
fallen rather than reading the surface table:

    f0    $A0 = fall   the drop
    f60   $A0 = $0C    carried (1008,700) -> (968,600), x THEN y, which
                       is exactly what src/player.c already does.  He is
                       NOT drawn here: his sprites sit parked at x 292,
                       off the right of a 256-wide screen.
    f131  $A0 = $0E    the kart is lowered - and he IS drawn, five 16x16
                       sprites at a FIXED screen x of 97, coming down
                       with it from y -56 to y +38
    f229  $A0 = 0      released

The descent is the kart's own z: `$3000` falling at `$80` a frame is 96
frames, and the phase lasts 98. So he is drawn against z rather than a
frame counter, and the two cannot drift.

*Two rig bugs cost this capture three attempts, and both were already
documented in this repo.* `tools/labs/track_force.py` ran its demo at
module level, so `from track_force import boot` booted a second ROM as a
side effect and swallowed the caller's `argv` - twenty minutes forcing
track 7 while asking for 16. And `track_force` forces `$0124` alone,
which NOTES 118 calls the NOTES 059 trap: the track number without the
theme, leaving the race half set up with the kart at x = 65520.
`gridtable.py`, written earlier the same day, hooks `$0150`/`$0152` and
reaches all twenty courses cleanly. The working method was in the same
directory the whole time.

---

**169** — The AI teleport: a brace, and a watermark that condemns healthy
karts.

The user: *"AI players tend to teleport to get their positions corrected:
they often disappear and re-appear a few meters further."*

Two causes, one on top of the other.

*The brace.* `src/ai.c`'s rescue timer resets when a kart betters its own
progress watermark - and that block sat INSIDE the finish-strip test:

    if ((cell & SMK_SECT_FINISH) && r->lap_cool == 0) {
        if (r->sector != sec) r->esc_len = 0;
    {                                    <- indented as a sibling, nested
        int prog2 = (r->lap << 8) | sec; <- as a child
        if (prog2 > r->rescue_max) { ... r->no_prog = 0; }
    }

So `no_prog` was cleared only while a kart stood on the strip - once a
lap. A lap is far more than the 600 frames the rescue waits for, so
**every AI kart was fished up and set down at its own waypoint in the
middle of every lap.** The indentation shows what was meant; the braces
say otherwise, and the compiler had no opinion.

*The watermark.* With that fixed a kart on track 4 was still rescued at
frame 1308, doing 362 in sector 14. `rescue_max` is a MAXIMUM, so one
backward excursion - a spin, a bump, a corner cut through an earlier
sector's paint - parks the timer until the kart betters its old best, and
a kart driving perfectly gets fished up while it tries.

But simply resetting on any sector change reopens the hole NOTES 057
needed the watermark for: two karts circling between adjacent sectors
reset every sector-keyed timer for ever. Both were tried; 20/20 on
`laptest` fell to 19/20 the moment the watermark stopped gating.

*NET DISPLACEMENT over the window answers all three.* A kart going
somewhere has moved; a wedged one has not; a circling one comes back to
where it started. Every 600 frames, if the kart is within 128 px of where
it was, it is fished up; otherwise the anchor moves on. Ours, and
labelled - the ROM's own trigger is still not decoded.

The self-test is the one that would have caught it: step a racer on every
course for 2400 frames, and for every position jump, check the kart's own
net displacement over the preceding window justified it. Three rescues
across twenty courses now, none of them on a kart that was travelling.

**169a** — And Lakitu's own row on the rescue was built on a misread.

The user, from a savestate: *"our implementation is not bad at all. Just
double check lakitu's position in the animation."*

It was wrong, and the reason is a bad read rather than a bad guess. The
first port drove his row from the kart's height, taken from `$1E` - which
is the LOW word of a 24-bit value whose pixels live in the top byte, and
which alternates 0 and -32768 frame to frame. The capture's own OAM says
his row is not a ramp from the kart's height at all:

    f0..f19   holds at -40
    f49       RISES to -56
    f95       and only then comes down, to +38

He swoops. That shape cannot come out of the kart's monotonic descent
however it is scaled, so the path is now the measured one, generated into
`src/rescue_path.inc` like the start's and the lap sign's, played off its
own frame counter.

**168b** — The lap sign drew two digits, because the numeral is a column.

The user, with a photograph: *"the sign is glitched. It was lap 4 and the
3 was stuck in the middle. Lap 2 had a glitch also because that middle
part where the 3 is in the picture, had garbage only."*

NOTES 168 read the sign's OAM as "one 16x16 sprite at `$A3 + n`", because
that is literally what the capture holds - a 16x16 at X+8 whose halves,
for lap 2, are the plate's edge bar and the numeral "2". Generalising
that to `$A3 + n` puts two CONSECUTIVE tiles on the plate: lap 4 asks for
`$A5` and gets `$A5` and `$A6` side by side, "34", with the bar gone.
The photograph shows exactly that.

The numeral is ONE tile column - `$A4`/`$A5`/`$A6` are 2/3/4, eight
pixels wide and sixteen tall - so the sign is drawn as columns instead:
the plate at X, the bar at X+8, the numeral at X+16. For lap 2 that is
pixel-for-pixel the same sprite the game places, which the self-test now
asserts against the captured frame: 0 px differ. Every other lap then
follows by construction.

The lesson is the one this log keeps relearning: a single captured frame
shows what the game did ONCE, and the shape you infer from it is a
hypothesis until a second case tests it. Lap 2 was the only lap captured,
and it was the one lap where the wrong reading looks right.

---

**170** — A whole recorded race, and what it can answer.

The user played a full five-lap race in MAME and recorded it:
`tools/labs/mame/sessions/flag`, 7579 frames. Their words for why it is
worth keeping: *"tons of data: how AI really works, my speeds, their
speeds, how they attack, coins collection and losing on impact,
animations."*

Replayed under `tools/labs/mame/flaglog.lua` it yields, per frame, P1's
position, speed, coins, rank, hazard state and rubber-band row, and the
same for three AI karts:

    tools/labs/mame/replay.sh flag tools/labs/mame/flaglog.lua 240

What is already visible in it:

* **A complete race**, lap words `$7F` through `$85` - the grid crossing
  and all five laps - with the player coming from **rank 7 to rank 0**.
* **The coin rules, both directions.** 14 gains and 4 losses. Two losses
  are single coins at speed (526 and 653); one is **four coins at once**
  at frame 5596 doing 862. So the loss is not a flat one-per-hit, and
  what distinguishes them is decodable from this file alone.
* **The rubber band, in the wild.** The player's `$C8` is 0 for the whole
  race - the band never touches a human - while the AI karts move between
  rows. That is the first independent confirmation of NOTES 167 outside
  the oracle.
* **Speeds to compare against.** P1 peaks at 1050 and averages 676; the
  AI kart logged peaks at 657, averages 464.

What it CANNOT answer: anything about sprites. MAME exposes no OAM to Lua
(NOTES 145a), so the chequered flag's own art and assembly still need the
Python oracle - but reaching a finish there can now be cheap, by forcing
the lap word near the end rather than driving five laps.

---

**171** — What the user's own race says about speed, and a peak mistaken
for a top.

Their recorded race (`sessions/flag`, NOTES 170) was played on the
EMULATOR, so it is a reference for the original's behaviour, not a test
of this port.

*The class and the coin rule, confirmed from a human run.* Their speed
sat at **864 for 2916 frames** - the 90th and 99th percentiles are both
exactly 864 - which is `784 + 80`: Mario's 50cc top (`$B4`) plus the ten
coin bonus (`$D6 = $B4 + 8 * min(coins, 10)`). Both tables verified
against a person driving, not against the oracle.

*And a peak is not a top speed.* Their run touched 1050, from which this
log first concluded "so they were on 150cc". Wrong, and the same data
said so: 1050 lasted FOUR frames out of six thousand. Where it happened
settles what it was -

    f1655  550  at (952,745)      x 952 with y walking down from 745
    f1660  800  at (952,732)      is Mario Circuit 1's own grid slot
    f1665 1050  at (952,715)
    f1666  526  at (952,710)      and a coin lost the next frame

+50 every frame off the line is the boost drive state's signature
(`$EE = 50`, NOTES 143). It is a TURBO START, in the original, caught
naturally in a race rather than in a four-start test file - the first
such sighting this project has. The user did not count it as "a speed
boost" because they took no item, which is fair and is exactly how the
mistake got made.

*The port's own turbo launch is confirmed separately*, by the user
playing it: "it also works in our implementation. It is slightly harder
to pull off given that there is no sound yet."

The lesson for this log: look at the distribution before the maximum. The
sustained value was sitting in the same file saying 50cc while a
four-frame transient was read as the answer.

---

**172** — The coin loss exists, and a negative result was wrong about the
addressing form.

The user, looking at the coin column of their own race: *"you can't hold
15 coins at the same time."*

Chasing that turned up something the project had written off. `src/
pickup.c` said, from an earlier sweep: *"No coin is ever LOST through
`$0E00` in banks $80-$85 (all addressing forms searched)."* It is at
**`$85:E4B2`**:

    $85E4B2  lda $000E00,x     the coin count
    $85E4B6  beq  ...          none to lose
    $85E4B8  dec A             lose ONE
    $85E4B9  sta $000E00,x
    $85E4BD  $0FC0,x += 2      the sound

"All addressing forms" was not all of them: this uses the LONG form
`$000E00,x`, a four-byte opcode (`$9F 00 0E 00`) that none of the short
`sta abs,x` patterns match. The same sweep found the increment at
`$81B7D2` because that one IS short.

What proved the note wrong was not a better search - it was the user's
recorded race, where the counter goes DOWN four times. A negative result
in this log is only as good as the pattern list behind it, and the way to
test one is data, not more reading.

It decrements by ONE per call, which fits the user's own account exactly:
a bump against another kart costs one coin, and the banana they drove
over cost four - four calls, not a bigger constant.

*And the counter's ceiling is 100, not 10.* `$81B7C7` adds one and wraps
at `cmp #$0064`. The ten lives in the SPEED rule instead - `$D6 = $B4 +
8 * min(coins, 10)` - which is exactly why their run plateaued at 864
from 10 coins all the way through 15 (NOTES 171). Two different numbers
that both look like "the coin cap" from the driver's seat.

Still not ported, and now labelled precisely: WHICH hits call `$85E4B2`.

---

**173** — The engine class, verified by a person driving it twice.

The user raced the original again, this time on **100cc**
(`sessions/cc100`), after the 50cc race of NOTES 171. *"Harder than
expected. Emulator has so much input lag."*

The prediction was fixed BEFORE the run was read, from the ROM alone:
`$81:8000` holds the character's base and `$81:F026` adjusts it by
`-$80 / +0 / +$A0` for 50/100/150cc, so Mario's 50cc top of 784 makes his
100cc top **912**, and `$D6 = $B4 + 8*min(coins,10)` puts the ceiling at
**992**.

    run          plateau   ROM rows that allow it
    flag  50cc      864     Mario 50cc, Luigi 50cc
    cc100 100cc     992     Mario 100cc, Luigi 100cc

Both unique to the row the user says they drove, and 992 - 864 = 128 =
`$80`, the class adjustment measured from the driver's seat. The coin cap
is confirmed a second time and harder: this run carried **up to 17
coins**, and 11, 13, 14 and 15 all park at exactly 992.

*The statistic is dwell, not a maximum.* A race contains speeds the rule
does not govern, in both directions: this run touched **2009** (they took
items, unlike the 50cc one), and at coin counts held briefly the driver
never reached the cap at all. Neither a max nor a percentile survives
that. What separates them is that a cap is a place the kart PARKS - one
speed value collecting many frames - while a boost decays through each
value in a frame or two. `tools/labs/coinspeed.py` scores the highest
speed held for >= 8 frames, and on the 50cc run it recovers
`784 + 8*min(coins,10)` exactly, boost and all.

*And the tool identifies a row rather than fitting a curve.* Free-
parameter fitting was written first and was worse: a driver spends most
of a race away from the cap, and those undershoots drag base and cap off
the real row (the 100cc run fitted to "887 + 8*min(coins,13)"). The ROM
already fixes every legal answer - 24 of them - so the honest question is
which one the data allows, not what curve is nearest.

A caution for reading these plateaus: 864 identifies Mario/Luigi at 50cc
only because the coin term is included. Koopa and Toad's 100cc BASE is
also 864, so a plateau compared against the wrong column names the wrong
kart.

Still on ROM authority alone: 150cc, predicted 1072 + 80 = **1152**.

---

**174** — The AI's speed row, and the flag that makes it a rubber band.

The user: *"There is something missing in AI speed logic. In original
game, no matter how fast I go, I always have someone pretty close
chasing... In our implementation, it is quite easy to over run them and
leave them behind."*

The port had a `smk_ai_rubber` that compared each kart's distance to the
kart ahead against `$80AF0F` and picked one of three rows. Almost every
part of that was wrong, and the measurement that showed it was a row-mix
comparison: our field spent **0.1%** of a race on `$00` where the real
game spends **14%**, and never once used `$18`, which the real game uses
8% of the time.

*The routine.* `$80ADA0` is reached by falling out of `$80AD96`, which is
why searching the whole ROM for a `JSR`/`JSL` to it finds **zero** call
sites - it is entered through `jmp ($AD76,x)`. `$80AD96` also supplies
the parameter, and settles what it is:

    $80AD96  lda $0E50 ; beq +      ; non-zero: row $00 for EVERY kart
    $80AD9B  lda #$0000 ; rts
    $80AD9F  lda $DA,x              ; A = THIS kart's own $DA
    $80ADA0  phx                    ; ...and falls through

*What it keys on, none of which the port had:*

  - **`$10` bit 15 is the human player.** Set on kart 0 for 100% of a
    recorded race, never on an AI. Every branch asks it of the kart
    AHEAD or BEHIND. That is the rubber band: the AI picks its speed from
    where the player is. An AI field with no such test cannot help but be
    left behind.
  - **`$010E` is the rank table.** `$010C,y` and `$0110,y` (y = rank*2)
    are that one table two entries apart - the kart ahead and the kart
    behind.
  - **`$C1` is the LAP.** `$80:89B6` adds `$0100` to the word at `$C0`
    and stores it immediately before `cmp $F8,x`, the progress watermark;
    in the recorded race every kart's high byte walks `$7F -> $80 -> $81`
    one step at a time and never back. `$7F` is the line not yet
    crossed. So `and #$0007` indexes `$80AF0F` **by the lap**: the
    catch-up distances are re-tuned every lap of every race. The port
    indexed that table by ENGINE CLASS, which held it constant.
  - **`$80AEB9`: a midfield AI far behind ADOPTS THE ROW OF THE KART
    AHEAD.** Rows propagate backwards through the pack. It is the single
    most-used branch in our own race (40% of kart-frames).
  - **`$DA`** is static per kart block - `0,0,0,0,2,4,6,8` for the whole
    race - and is also the parameter, so most comparisons are "am I
    carrying a bigger handicap than that kart".

*And the rows are not what they were named.* Their speeds are not a
property of the row but of the physics block, `w[16 + (surface&3) + row]`,
and read there the order is `$08 > $00 > $10 > $18` in every class.
`$00` is the second-FASTEST row; this port called it EASE and gave it
only to a leader with clear air.

*`$80AF0F` has five rows, not eight.* Row 5 onward is the code of
`$80AF5F`. Lap one indexes row 7 - into that code - so the original
gives a first-lap kart an effectively infinite catch-up distance, and
early chasing comes only from `$DA` and from adoption. Reproduced by
reading the bytes verbatim rather than tidying the table: a tidy table
would chase where the original does not.

*The gate.* `tools/labs/mame/rowlog.lua` logs every input the routine
reads and the `$C8` it produced for all eight karts; `tools/rowcheck`
feeds those to the SHIPPED routine. **94.2% of 39,074 kart-frames**, and
on those inputs our row mixture is the game's to within a tenth of a
point (14.2/46.1/31.9/7.8 against 14.2/46.1/32.1/7.7). The residue is
the distance CACHE - `$80AEBC` and `$80AECF` reuse `$92`/`$90` while
`$96`/`$94` still names the same partner, so the original sometimes tests
a distance a few frames old. LABELLED, not modelled.

*Three wrong turns worth keeping.*

  - The first row-mix reference came from `flaglog.lua`, which logs three
    of the seven AI karts. Comparing a 7-kart port against a 3-kart
    sample made the port look worse than it is. Count what the instrument
    actually sampled.
  - `tools/rowcheck` scored 74% until it was noticed that it never called
    `smk_ai_catchup_load`, so every threshold was zero and every distance
    test passed. A broken HARNESS reads exactly like a broken port.
  - Adding branch tags to the C, a mechanical edit replaced
    `if (r->trouble) return SLOW;` with `BR(1, SLOW);` and dropped the
    guard - every kart in trouble, 100%, which the branch histogram
    caught immediately. Instrumentation that reports what it did is how
    that was one minute rather than an afternoon.

*What is NOT yet evidence.* In our own headless race the AI is slightly
SLOWER than the ad-hoc code it replaced (mean 563 against 588). That
comparison is confounded and should not be trusted either way: the
autopilot does not pull away the way a person does, so our field is
bunched (median rank-to-rank gap 158 against the human race's 219, p90
311 against 671), and a bunched field correctly chases less. The
frame-exact replay is the evidence; this needs a person to judge.

LABELLED and not ported: the `$E2` bit 1 policy at `$80ADC0` (0 frames of
5582 in a one-player race) and `$0E50` (never non-zero). `trouble` is
approximated from the port's own crash states - `$84` and `$10` bit 5 are
not decoded - and currently never fires, where the original is at 8%.

---

**175** — The countdown threw the steering away, so the driver never leaned.

The user, listing what is missing: *"When stopped (speed=0) and you press
left or right, the cart doesn't turn, the player only leans their head
left or right. Nothing else. This can be tested easily during count
down."*

Half of this was already right, and that half is worth stating because it
changed the whole size of the job. **The kart does not turn at a
standstill, and never did.** `$80:A9B8` holds the turn per frame for
speeds under `$80`, indexed by `(speed>>4)&7`:

    0  16  32  48  56  60  62  64  128

Entry ZERO covers speeds 0..15. Held LEFT for 90 frames at a standstill
moves the heading by exactly `$0000`. So this was never a physics bug.

What was missing was the LEAN, and the reason is one line in the
countdown at `src/main.c`:

    in.up = in.down = in.left = in.right = false;

The throttle is consumed there deliberately - it feeds the rev, and where
the rev sits when the lights go out decides the turbo launch (NOTES 143) -
and the steering was swept up with it. So through the whole countdown, the
exact place the user says to test, our driver sat rigid. Now only
throttle and hop are taken.

Keeping the steering cannot turn the kart, for the reason above; all it
reaches is the sprite's lean, which `main.c` already ramps toward `$0A00`
from held left/right regardless of speed (NOTES 080's pose ladder puts
that in frame 1's band).

The selftest pins the half that must NOT move, and pins it non-vacuously:
the same input WITH the throttle does turn the kart, or the check would
pass on a kart that never steers at all. One trap on the way: a kart
`memset` to (0,0) is off the map, falls, and Lakitu resets its heading -
which reads exactly like "it turned". The test starts it on the grid.

---

**176** — Seven rigs, one sentence from the user, and a ledgered constant.

The user: *"when you drive under a thwomp and that thwomp has just lifted
up, the system makes you crash/bounce against nothing (the thwomp is
higher, track is free). This does not happen when the thwomp is super
high, but happens while it is just going up, when the original game lets
you pass."*

`src/ai.c` passed a kart under a mover above HALF the parked height and
said so in the comment: *"the height at which it stops touching is ours."*

**The game's rule is not decodable statically.** `$80ADA0`-style reading
found nothing: object blocks are reached only through an index register,
so there is no `JSR` to the routine and no absolute address to search for.
The six apparent hits for `$181F` and friends are inside compressed
graphics.

**Seven rigs, each measuring something other than the question:**

    1-2  kart parked on the object's centre: no hit at ANY height,
         including z=0 on the floor.  The PIPE CONTROL - same rig, an
         object NOTES 072 measured a real crash against - also found
         nothing.  The rig was broken, not the game.
    3    approach geometry, but 8 frames at speed 480 covers 16 units
         against a 26-unit gap.  The kart never arrived.
    4    each trial broke on its first hit, so the game advanced ~1 frame
         per trial and the ~270-frame cycle never turned.  The RISE - the
         whole question - was never sampled.  The tell was in the data:
         the z column stepped -256 -288 -320 -352 -384 -416 -448, NOTES
         152's fall, one step per TRIAL.
    5    the cycle ran, and every trial hit at every height to 9024.
         That is what a WALL looks like: the object is at (388,68), so a
         kart started 26 units due north begins at y=42, off the road by
         the map edge, with an identical $10, $AC and speed loss.
    6-7  approach along the flow field, each trial controlled by re-running
         it with the object moved off the map: now NOTHING hits, at any
         height, including z=0.  Also impossible.

Every one of those teleports a kart at an object. **A person driving needs
none of it**, which NOTES 152 had already taught this log once.

**The recording answers it.** `thwomplog.lua` logs all four object blocks'
height beside the kart's own impact state; `thwomppass.py` separates crash
ONSETS from closest passes, per encounter rather than per frame - the
~9-frame knockback keeps registering while the Thwomp rises, and driving
BESIDE one is not driving under it. From the user's Bowser Castle run:

    crashed, within 6 units:   0, 0, 0, 0, 448, 960
    passed,  within 6 units:   2880, 3008, 3648, 4096

(Three "crashes" 36-40 units away at z 3008-5120 are walls; distance
excludes them.) So the true threshold is between **960 and 2880**, and the
run has nothing inside that band.

**Which is why the old 2048 could not be caught by the recording, but
could be caught by an eye.** 2048 is consistent with every sample above.
It is not consistent with the user, because at z=1500 a Thwomp is drawn 15
px up, looks plainly lifted, and still hit you.

The user then made the call this log should have offered sooner: *"This is
one of the things we don't need to do super accurate and we can implement
our own rule, put it in the ledger and move on. We have spent a lot of
time trying to reproduce it with our own tools."*

`SMK_MOVER_CLEAR = 1280`: 80% of a 16 px kart, at ~97.8 mover units per
screen pixel, and exactly 20 frames of the measured +64 climb. Every
sample in their run falls on the correct side of it. Ledgered as S26.

The lasting lesson is the pipe control. Two runs reported "no collision at
any height" and both were the rig; only an object with a KNOWN answer
could say so. A negative result from a rig that has never produced a
positive is not a result.

---

**177** — The finish sequence, and the first feature built to a different
standard.

The user picked it, and set the rule that shaped it: *"This one doesn't
need to be faithful (faithful is for driving experience, not for hud,
menus, and things that can be better without constraints)."* So the
camera move, its timing and the results layout are DESIGNED. The art and
the data stay the ROM's, and the times are real. Ledgered as S27.

*The race no longer ends on the frame you cross.* `race_over` used to set
`ui.screen = SMK_UI_RESULT` immediately; now it enters `RACE_FINISH`, and
**the simulation keeps running through it** - which is the point, because
the other seven karts have not finished when you do and their times are
what the results are for. Anything still going when the celebration ends
is run on headlessly by `settle_field` until it crosses, so every row of
the table is a time that kart actually drove rather than an estimate.
Capped at 90 s of race time; past that it is DNF.

*Four things had to be found by looking at the picture*, and each one is
the kind of thing no amount of reading the code would have shown:

  1. **The camera has to move AHEAD of the kart, not orbit it.** The
     chase camera sits AT the kart, which is why your kart draws at the
     bottom of a SMK screen. Orbiting it merely pushed it sideways and
     left the driver jammed against the bottom edge.
  2. **`draw_scene` takes `cam_heading` separately from `cam.angle`.**
     Rotating the camera alone turned the GROUND while every sprite was
     still projected on the old basis - and the player's kart vanished
     entirely. `finish_yaw` now carries the same rotation to both.
  3. **The kart draw list starts at slot ONE.** `for (int k = 1; ...)`,
     so slot 0 - the player - could never be in it, whatever
     `racer_draw_mask` said. The mask already clears bit 0 for every
     other caller, so the loop bound was doing the mask's job twice and
     made the celebration's `0xFF` silently mean nothing.
  4. **The furniture has to stand down.** A FINAL LAP sign over a race
     that has just been won reads as a bug, and so does a speedometer.

*And the results screen shows the field.* Place, name and total for all
eight, the player in gold, the top four bright and the rest dimmed, with
the player's own splits underneath. Two things it exposed: `ui.track` is
only set by the shell, so a `--race` or `--timetrial` run had a blank
track name; and the screen could not be looked at without playing through
the menus, which `SMK_RESULT_SHOT` and `SMK_FINISH_SHOT` now fix.

The time trial's own layout is untouched - `entries == 0` selects it, and
`result` is memset per race so it cannot inherit a stale field.

---

**178** — The winner celebrates, and the art decides where the camera goes.

The user, on the first version: *"not bad, but in the real game, the
player also celebrates and continues driving (celebration is important,
continuing driving is not)."*

**The pose exists: frame 47, both arms thrown up.** Found by rendering all
48 frames of a driver's sheet and looking, which took three attempts
because `smk_draw_sprite` anchors at the WHEELS - `x0 = cx - size/2,
y0 = cy - size`, so `cy` is the sprite's BOTTOM. Drawing a contact sheet
at `cy = row * cell` therefore shifts every row up by one cell, and
drawing a single frame at `cy = 0` puts it entirely above the buffer and
renders nothing at all. Two "this frame is blank" conclusions came from
that before the anchor was read.

It is the same slot for all eight drivers - Bowser's shell, Toad's
mushroom, arms out on each - so it is the victory pose and not a
character-specific accident.

**And it is a REAR view**: the back of the head, no face. That settles a
question the first version had already answered the other way. The user's
own description was *"camera shows you from the front while the character
celebrates"*, and the first version swung 180 degrees to do exactly that -
but a front camera and this pose cannot both be had from this art. The
user had already said which half matters ("celebration is important"), so
the camera now draws BACK and up instead of round, keeping the driver's
back to us so the raised arms read.

A front-facing celebration would need the podium art, which is a different
sheet and is not decoded. Labelled, not attempted.

*The winner also stops driving.* Holding the throttle through a
celebration looked wrong and the user said it did not matter, so the
player's controls are simply dropped on the crossing and the kart coasts.
The SIMULATION still runs - the other seven have not finished, and their
times are the results screen (NOTES 177).

The camera constants (16 units back, 5 of eye rise, 50 frames to move,
210 to hold) are ours, under S27.

---

**179** — The finish camera, measured: it really does swing to the front.

The user: *"look at one of my previous full race. The celebration and
camera is captured there."* It was - their `flag` recording runs past the
flag, and no gate had ever looked.

`finishlog.lua` dumps `$0080-$00FF` and the player's whole block every
frame from the last crossing, so the answer comes out of a diff rather
than a hypothesis. The camera azimuth `$94` trails the heading `$A4` by
exactly **192** for the entire race (NOTES 083). After the crossing:

    192  ->  9555  ->  19421  ->  29735  ->  32836  ->  settles ~32800

`$8000` is half a turn. **The game swings the camera round to the front
of the kart over about 80 frames**, which is exactly what the user
described and what the first version of this did before this log talked
itself out of it. Restored, with the measured timing.

The speed goes `862 -> 710 -> 540` and then holds around **510** for the
rest of the recording: the kart keeps rolling, it does not stop. Their
words: *"continues driving"*.

**What the recording does NOT settle is the sprite**, and this is worth
stating plainly rather than papering over. Rendering all 48 frames of the
sheet identifies exactly one celebration pose - `SMK_SPR_WIN` (47), both
arms thrown up, the same slot on all eight drivers. Put beside frame 1
(known rear) and frame 10 (known front) it is unambiguous: it is **frame
1 with the arms raised** - cap dome, no face. A REAR view.

So the two measurements pull apart: the camera goes to the front, and the
only celebration pose faces backwards. Shown together, the winner
celebrates with his back to a camera that has just moved to see his face.

`$1042` is pinned to 0 from the crossing onward, having taken 0..10 all
race, which looks like the frame index being forced - but 0 is the
half-sprite the port folds for the straight-on pose, not 47, so it does
not resolve it either.

Default is the measured camera with the normal front-facing sprite;
`SMK_WIN_POSE=1` forces the arms-up frame. LABELLED and put to the user,
who has seen the original: no rig here can tell which of the two the game
actually shows, and guessing would be inventing a fact.

---

**180** — The winner's pose: found, photographed, and still not drawable.

The user settled it with a screenshot of the original: face on, mouth
open, **both white gloves raised**, kart-sized, with Lakitu waving the
chequered flag alongside. (And a correction worth keeping: the oval under
the kart is LAKITU's shadow - *"mario is not airbone!"* - so there is no
hop to model.)

That killed two earlier readings of mine, both of which had been stated
too confidently:

* **frame 47** is arms-up but a REAR view - and the arms are bare skin,
  where the real pose has white gloves. The white-pixel scan is what
  showed it: 47 has no white in either upper corner.
* **frame 32** is not a pose at all, just the smallest rotation tier of
  the ordinary front view. Hands on the wheel.

The pose IS in the sheet. Frames 33-43 each hold four small sprites
rather than one 32x32, and the white-glove scan puts the raised gloves in
that group (38, 40 and 42 carry the most). But slicing those frames into
16x16 quadrants gives **heads without bodies and arms without heads**:
they do not use the tile arrangement `smk_sprites_load` assumes for a
32x32 (N, N+1, N+16, N+17). The packed frames have their own tile order
and it is not decoded.

So this is a decode, not a crop, and it is ledgered as S28 rather than
guessed at. `smk_draw_sprite_quad` is written and correct for a 2x2
layout, and `SMK_WIN_POSE=1` draws through it - which is how the
scrambling was established, and is the starting point for finishing it.

Default is OFF: a garbled winner is worse than a plain one. The finish
shows the ordinary front-facing driver, which is right in every respect
except the arms - and the CAMERA that frames him is measured, not
designed (NOTES 179).

The cost of this one is worth recording. Three separate "found it"
claims - frame 47, then frame 32, then frame 40's quadrant - each looked
right in a montage and each was wrong. Two of them were wrong because of
how I was DRAWING the sheet, not what was in it (the wheel anchor, then
the tile order). When the thing under inspection is the renderer, a
picture of the renderer's output is not evidence about the data.

**And the right instrument was available the whole time.** The user:
*"I find it weird that you are unable to find this if you have full
access to the cpu debugger, memory and the game rom storing the
graphics."* That is correct, and the answer is that none of the above
used the debugger at all. **OAM names the sprite.** At the celebration
frame it holds the exact tile numbers the game is drawing, their size bit
and their palette - which identifies the pose outright and needs no
theory about how the sheet is packed, which is precisely the thing that
was guessed wrong three times. The oracle exposes OAM, VRAM and CGRAM;
MAME exposes none of them, which is why this had to be the oracle and
not the recording. `movers.py` already shows how to drive the oracle to
an arbitrary race state - reach a finish, dump OAM, read the tiles off.

Filed as S28 with that method written down, because the lesson is not
"the packing is tricky" - it is that a static sheet was read when a
running machine was there to be asked.

---

**181** — The standstill lean, verified on both sides.

The user asked for it again after NOTES 175 fixed the countdown, so this
time it was checked against the game rather than reasoned about.

**The game leans.** `tools/labs/headlean.py` holds the kart at zero speed
and runs alternating phases - neutral, LEFT, neutral, LEFT, neutral,
RIGHT - then keeps only the VRAM bytes that hold one value under neutral
and a different one under LEFT *every time*. **70 bytes** track LEFT
exactly and the same 70 track RIGHT, all in `$B020-$B03F`, which is the
kart's own sprite. The heading `$A4` reads `$0000` in all six phases, so
the kart still does not turn - NOTES 175 confirmed from the other end.

The alternation matters. The first version of this lab diffed one neutral
snapshot against one LEFT snapshot and got 234 differing bytes - which
looks conclusive until you also diff neutral against neutral and get
**162**. The clock and the HUD churn VRAM every frame. A single diff
could not have separated the driver from the timer; correlation with the
input can.

**And so do we.** `SMK_FORCE_STEER=-1|1` holds a direction with no hands,
so the same three states can be shot from our own build: neutral is
symmetric, LEFT and RIGHT are mirrored leans, in the countdown *and* in
the race at zero speed. Nothing needed changing - the countdown fix of
NOTES 175 was the whole of it - but "nothing needed changing" is only
worth saying once it has been looked at.

---

**182** — The standstill lean is not a turn, and the sprite says how.

The user, on the pictures NOTES 181 was pleased with: *"the images you
are providing are not leaning but turning. different things."* Correct.
The port picked an adjacent ROTATION frame, so the whole kart pivoted.

The byte count had already said so and this log read past it: NOTES 181
found **70** VRAM bytes changing, and a whole rotation frame is 512.
Seventy is about two tiles - a head. Counting bytes was the mistake;
drawing the sprite is what settled it.

**The mechanism.** The player's kart is FOUR 16x16 sprites, and at rest it
is symmetric - the right pair is the left pair with hflip set:

    neutral   $180  $180F  $1A0  $1A0F
    right     $180  $182   $1A0  $1A2       the halves stop matching
    left      $180F $182F  $1A0F $1A2F      the same, whole sprite mirrored

Those four tiles are one 32x32 block, so steering simply STOPS FOLDING the
block and draws its own right half instead. Left is exactly right's
mirror. Seventeen per cent of the pixels move: the cap and shoulders
lean, the kart's bumper is pixel-identical in all three.

**Which block.** Rendering VRAM tile `$180` and comparing it against all
48 sheet frames identifies it as **frame 47** - so the port already had
the art. (And that kills a third reading of frame 47: it is not a victory
pose, it is the ordinary rear view with the hands up on the wheel. NOTES
178 and 180 both leant on it being something special.)

Ported as `SMK_POSE_LEAN`: below speed 16 - where `$80A9B8[0] = 0` means
the ROM turns nothing at all (NOTES 175) - a held direction draws frame
47 UNFOLDED, hflipped for the other side, instead of a rotation frame.
Above that the kart does turn and the rotation rule is right.

One wrong turn on the way: the first attempt drew frame 0 unfolded,
because the port's own comment calls its right half "the junk right half".
It is junk - frame 0 is a half sprite and its right side is a different
drawing entirely. The block that folds AND unfolds is 47.

---

**183** — Coins spill when you are bumped, with the game's own coin.

The user: *"when passing over one, coins are thrown up and then fall
down"*, and on scope: *"leave the 4 coin drop for later because they are
part of 'being hit' animation (which is a series of spins). One coin drop
is more relevant since you get to bump into others a lot."*

**The port had no coin loss at all** - `src/pickup.c` said so - so this
adds the mechanic as well as the animation: a fresh kart-to-kart contact
takes ONE coin, which is `$85:E4B2` and matches the user's own account.
The four-coin path (`$85:E4E5`, `sbc #$0004`) is decoded and deliberately
left for the spin-out.

**The art is the game's, found through OAM.** Dumping VRAM and CGRAM at a
real loss, the coin is three 16x16 frames - wide, edge-on, wide - at tiles
`$86`, `$A2`, `$60`, palette 6, about four frames each. Searching the
shared sprite blob at `$C1:0000` for those exact bytes places them at
`(tile - 48) * 32`, so the blob is uploaded from tile 48 - and the port
already decompresses that blob for the HUD and the kart shadow.

**The arc is OURS (S29)**, and one part of it is not: the coin is thrown
FORWARD and inherits the kart's velocity. That is from the recording -
the coin appears high on screen (y 67) and comes DOWN toward the camera
(y 114) - and it matters, because the first version threw coins backwards
where a forward-looking camera never sees them. Nothing was drawn at all
and the arc looked broken when the direction was the bug.

Guarded against the replay gates: coins set the top speed
(`$D6 = $B4 + 8*min(coins,10)`), so dropping one mid-replay would make the
ghost diverge from the recording it is measured against. `demoreplay` is
still 100% within 1 px, coins wrong on 0 frames.

---

**184** — Lakitu's third job: the chequered flag, and the trick that
reached it.

The last of his four jobs, and the one the roadmap had parked as
unreachable: *"needs OAM at a finish (MAME can't give OAM, so the oracle
with a forced lap word)"*. It also stood in the user's own screenshot of a
win, waving at the left.

**The trick is the forced lap word.** Driving five laps in the interpreter
costs the best part of an hour. `$C0`'s high byte is the lap counter based
at `$7F` and `$F8` is the progress watermark it is checked against
(NOTES 174), so setting BOTH to the last lap makes the very next crossing
the finish. `tools/labs/lakitu_flag.py` gets moving, forces them, drives
the flow field into the line, and records OAM through the pass. It reached
the finish in 86 frames.

**What OAM gives, which no sheet-reading would.** His group, steady across
the whole pass, as offsets from his head - all 16x16, palette 5, the same
palette his other two jobs use:

    ( 0,  0)  $4A   his head
    ( 0, 16)  $4C   cloud, left
    (16, 16)  $44   cloud, right
    (16,  0)  the flag, upper half
    (24,  8)  the flag, lower half

The flag WAVES through three tile pairs at about 17 frames each -
`$6C/$6E`, `$80/$82`, `$8C/$8E` - which is the checker across `$68-$9F`
the roadmap predicted. He enters from ABOVE the screen (y is signed and
starts at -48) on the left and sweeps right and down over 230 frames,
captured frame by frame into `src/flag_path.inc`.

`tools/labs/oampix.py` came out of this and is the general form of the S28
lesson: give it a VRAM/CGRAM/OAM dump and it draws every distinct sprite
with its tile number. Compositing the whole dump is what identified the
flag in one look.

**And the same trick unlocks two things still labelled.** The FINAL LAP
plate is "read off the sheet, not captured - reaching the fifth crossing
costs five laps of interpreter time" (NOTES 168b); that reason is now
gone. So is the excuse for S28: a forced finish puts the celebration on
screen, and OAM will name its tiles.


---

**185** — The item system, decoded, measured and in.

The user: *"investigate the item system. find patterns, they are not so
different. decode the whole system, draft a spec, and then implement. we
may need to decode in parallel the 'getting hit' reaction."* `docs/ITEMS.md`
is the spec and carries the addresses; this entry is what it cost and what
turned.

**One word.** `$0D70,y` is the whole player-side state: `$A000` starts a
roulette, `$8000|id` holds, `$4000` is READY, and the button (`$81:B40A`,
a LEVEL test) ORs `$81:B336[id]` into the kart's `$E0` - one bit per item,
which `$80:E88B`'s handlers consume. The game's own debug cheat at
`$80:E8B3` writes `$E0` straight from the pad, which is what made every
item fireable in the oracle with no box.

**The outcome is three tables deep**: the track's block (`$81:B471` by
`$81:8B73[track]`), a record by lap and rank (`$81:B666`: lap 1 everyone,
then leader / 2nd-4th / 5th-8th), and five random bits against eight
thresholds whose ninth byte is both the catch-all and the roulette's
sequence. The user's two Mario Circuit races used sequence 4 and landed on
5, 0, 3, 7 - every one inside record 1's live set. Ported with the ROM's
bytes; the roll is ours.

**Two hit reactions, not one.** A banana (`$81:9982`) sets `$E2 |= $1000`
and `$80:B443` clamps the speed to `$300`, counts `$FA` down from 60 and
spins the pose `$0A00` a frame in states `$0A/$0C`. A shell or lightning
(`$81:9ACE`) sets `$E2 |= $0300` and `$80:B4D1` puts the kart in state
`$1A` with drive `$14`: speed falls 56 a frame to nothing while the pose
spins at `$E4`, which starts `$1000` (`$2000` for an AI) and decays `$40`
a frame - about 60 frames - then settles through zero at `$180`. The
user's banana at frame 5596 of `flag` shows the first (`$E2 = $9004`,
`$FA` 59, 56, 53...); the oracle's shell hits show the second (`$AC = $14`,
`$EE = -56`, `$AA` +4096 a frame). Both take four coins (`$85:E4DA`).
The port carries all three states on the player and a tumble on the AI.

**The projectiles are measured, not read.** `$80:F243` is a chain of
per-frame handlers run through `jmp ($0000,x)`, and reading it would have
cost the day. The oracle gave the numbers instead (docs/ITEMS.md §5):
kart speed + `$300`, a 7/8 wall bounce, `$0400`-a-frame homing after
8 frames, and - the one that decided the port's rule - a red shell DIES on
a wall where a green bounces.

**The icons are BG3, and that is why no dump found them.** `$81:B31C`'s
`$0C26` is a tilemap cell, not an OAM slot; three OAM dumps through a
roulette showed nothing changing. Decoding VRAM as 2bpp at every base and
looking found the mushroom at word `$7000`; decompressing every start in
banks `$C0-$C7` and searching for those bytes found the blob at `$C1:12F0`.
Side by side with the game's own render they are identical.

*Three things that cost time.* The oracle's attract race sets `$0E50`,
which disables the projectile spawner (`$80:F4D7`) AND forces the AI onto
row `$00` - the un-demo hook did not cover it, so the first six item runs
fired into nothing; `Lab(zero=(0x0E50,))` now clears it. `$0DFA` holds a
ROM address: the block LIST lives in ROM and the blocks in WRAM, and a lab
that walked the list out of WRAM printed garbage blocks for an hour. And
`$81:9F02`'s turn table is `$0800, $F800, $0400, $FC00` read as words -
read as bytes it said ±4, which is nothing.

Gates: the roulette against the user's race 285/285 frames exact, plus a
shell's launch speed, the red's turn cap, and both hit reactions
(selftest 76 -> 83). Every earlier gate unchanged.

---

**186** — The spilled coin's path, captured from a bump the game made.

The user, on the coin that sat over Mario's head in every picture, and on
how to measure it: *"start any race. accelerate as soon as the countdown
ends and drive straight: you will hit the kart in front of you."*

`tools/labs/coinarc.py` does that in the oracle - P1 parked behind the
kart ahead with B held - and logs every OAM sprite wearing a coin tile
(`$86/$A2/$60`, palette 6) with its screen position, frame by frame.
The game bumped at frame 7 and took a coin; the coin sprite appeared at
frame **11** - four frames later - at (117, 67) in the top-half view whose
kart sits at (112..144, 70..102): level with the kart's top and ahead.
It rose to y 45 by frame 21, fell to 114 by frame 41, **bounced** once
(114 → 110 → ... → 94), and was gone at frame 55: 44 frames on screen,
drifting left two pixels a frame the whole way, spinning `$A2 → $60 →
$86` every four.

That is not a coin thrown off the kart. Three physical launches were
tried before measuring (with the kart's velocity: it hovered over the
driver; backward: behind the camera, never drawn; slower than the kart:
below the bottom edge) and none could have produced a coin that appears
four frames late, far up the screen. So the port REPLAYS the captured
path relative to the kart sprite's top centre (`src/coin_path.inc`), at
the kart's scale, with the game's own spin frame per step, mirrored
left/right for successive coins. The world-space `smk_coin` fields stay
for a future measured launch; the bounce and the lifetime are the
capture's.

LABELLED: the sideways drift is that one bump's geometry (the kart was
turning), kept rather than guessed away; and the capture is the 2P
top-half view, whose perspective is squashed, so the vertical extent may
read slightly short in the full-height view.

## 187. A bump with no coins is a spin-out — measured, and the port's third reaction

The user: *"getting hit by an item produces the same reaction as bumping
another kart with no coins in the pocket: the kart spins and ends up with
zero speed. if the kart gets hit by an object, you also lose 4 coins."*
The port took one coin on a bump and did nothing at all with none left.

### The decode

`$80:EAE3` mirrors the coin count into the kart: `$4E,x` bit 3 = "has
coins" (`lda $0E00,y; bne → ora #8`). The kart-kart bump at `$81:9B4D..9B7C`
tests that bit on each kart of the pair and, for the one without,
`$81:9AF5/9AFC` sets **`$E2 |= $0800`** — a third reaction bit beside the
shell's `$0200/$0300` and the banana's `$1000`. The dispatcher at
`$80:B49D` orders them: bit 9 → the tumble (`$B4D1`), bit 11 → **`$B435`**,
bit 12 → the banana (`$B443`). `$B435` does almost nothing: `$A6 = $0E`
if the pose lag `$AA` is negative, else `$10`; `$A8 = 0`; clear bits 11
and 12. No speed clamp, no `$FA` countdown, and the drive state `$AC` is
left alone — unlike the banana, which clamps to `$300`, zeroes `$AC` and
arms 60 frames. States `$0E/$10` (`$80:A904/A927`) are the ones the port
already had for the slide spin-out: `$E2 |= 8`, a star goes straight to
`$1C`, the pose turns `$480` a frame, and once the speed is under `$180`
the first turn that wraps the pose through zero ends it in `$1C`. The
decel is `$80:A64F`'s `$EE = -16` (the banana's sibling at `$A656` is
−8); both read as −15.5 / −7.5 on the machine because the fraction byte
`$ED` is left stale.

The 1-coin routine's own no-coin branch (`$85:E4B6 → $E69F`) only silences
the coin sound; the spin is the bump's, not the coin loss's. The 4-coin
loss for objects (`$85:E4DA`) was already in.

### The measurement (`tools/labs/bumpspin.py`)

Attract race un-demoed, P1 at 835 with B held, `$0E00 = 0`, then
`$E2 |= $0800` written by hand — the dispatcher's own path, without the
bump's push:

    f   spd  $A6  $AA      $E2
    0   835  00      0    0800
    1   835  10   1152    0008     +$480 a frame from here
    8   727  10   9216    0008     −15/−16 alternating
    32  355  10 −28672    0008
    56    0  10  −1024    0008
    57    0  1C    128    0008     the pose wrapped
    58    2  00      0    0000     and it drives off again

56 frames from 835 to nothing, B held throughout — "ends up with zero
speed" is the decel, not a clamp. The banana under the same rig: 768 at
once, then −7.5 a frame, 60 frames of `$FA`, stopped at frame 72.

### The port

`smk_player_hit_bump` enters `$0E/$10` exactly as `$B435` does; a fresh
bump (`bump_cool` rising) with `coins == 0` calls it instead of taking a
coin. AI karts now carry `coins` (start 2, `$81E3DA`), lose one per fresh
bump, and with none spin as `smk_racer_hit` kind 4: `$480` a frame flat,
15.5 a frame off the speed, `hit_t = speed·2/31 + 4`. Item hits keep the
4-coin loss and their own reactions (banana `$1000`, shell `$0300`,
lightning = shell + shrink), which the user's sentence groups with this
one: all three spin and end at nothing; the coinless bump is the plain
version.

LABELLED (ROADMAP S31): the AI's coin count only ever falls — the AI's
coin pickups (`$80:E9DD`) are not modelled, so an AI kart spins on its
third bump where the real one may have refilled.

## 188. The AI's weapons: absent from every recording — a blocker, not a decode

Asked to go on with items, the open piece was the AI's own weapons
(ROADMAP S31, docs/ITEMS.md §8). Before reading bank `$85` for them, the
recordings were searched for one happening.

### What was looked for

`tools/labs/mame/objspawn.lua` (a block in `$0800..$0FFF` / `$1800..$1FFF`
going live at `+$12` bit 15 or from all-zero), then
`tools/labs/mame/objdump.lua` / `objdump2.lua` (the whole `$1800..$1FFF`
window every frame, with every kart's position, `$10`, `$E0`, `$E2`,
`$14`), replayed over four sessions: `flag` (MC1 50cc), `cc100` (MC1
100cc), `gv1` (Ghost Valley) and `thwomp` (Bowser Castle) — 5,831 +
6,089 + 22,988 + 22,458 race frames. Post-processing counted every
rising bit of `$E0` and `$E2` per kart, and every object block whose
position jumped by more than 64 (a respawn or a drop), with the nearest
kart at the landing point.

### What is there

- The object pool is FOUR `$80`-byte blocks, `$1800/$1880/$1900/$1980`,
  chained through `+$00/+$02` from the head `$00C0`, and re-seeded per
  lap segment (NOTES 078, `entspawn.py`). MC1 keeps all four busy with
  pipes; Ghost Valley with Boos (`+$04 = $E6FE`), Bowser Castle with
  Thwomps (`$E18B`). Every jump in every session lands at a track object
  position, never at a kart.
- The projectile list in 1P is exactly two blocks — `$80F174: 1A00 1A80
  0000` — both the human's (`$0DFA` set at `$80F4E8`). The 2P and battle
  lists (`$80F15E/$80F16C`) add `$1B00/$1B80` and kart-side blocks. An AI
  kart in 1P has no projectile block to throw from.
- `$E0` (the item-effect word) NEVER rises on karts 1–7 in any session;
  `$E2` on them shows only the human's own hits (`$0200` twice in cc100,
  once in flag) and one `$2000` per kart at the finish. No star, no
  mushroom, no lightning, nothing.
- `$1A00..$1BFF` outside the human's throws and `$1C00..$1FFF` are DMA
  and scratch (`$1E00` fills and clears ~100 times a race).

The oracle attract race (MC1, 2,400 and 6,000 frames, `tools/labs/
aiweapon.py` / `aiweapon2.py`) shows the same: nothing.

### What that means

Either the CPU field does not use weapons in what has been recorded so
far (all four sessions are Mushroom Cup courses; class 50cc and 100cc),
or it uses them only under a condition none of these races met. A decode
from the ROM alone would be unverifiable — no recording has an event to
gate it on — so it stops here, per the standing rule of surfacing a blocker early: the
user can say whether they have ever seen a CPU kart drop or throw
anything in these runs, and if it exists a recording of it (a higher
class, a later cup) is the gate. Until then the AI has no weapons in the
port, and that is not a shortcut but the measured behaviour of every
race we hold.

### Addendum — the user confirms they exist; where else was looked

*"every cpu character has their own single power. while mario and Luigi
become invisible, the rest of the players have one item that can drop
behind, static, or throw ahead. shells for koopa troopa, banana for dk
jr, a shrinking mushroom for toad and princess, an egg for yoshi and a
fire ball for bowser, that is the only non static item."*

One item per kart suggested it lives in the kart's own block, so
`tools/labs/mame/kartdump.lua` dumped `$1000..$17FF` every frame of the
100cc race and `lowdump.lua` the `$0C00..$0FFF` window. Every word that
moves on an AI kart is accounted for: `+$10` bit 11 with `+$50` = the
human's shell block while a red homes in (37 frames), `+$10 = $7000` for
the 8 frames of a hit, `+$5E` the 7→0 bump countdown with `+$50` = the
other kart, `+$AA/+$E4` the tumble of the two karts the human hit, `+$C8`
the AI's `$10/$08/$00` driving flags, `+$3A/+$3C` its waypoint target,
`+$1E/+$20/+$26` the ramp jumps. In the low window `$0D34` is the object
segment, `$0D50/$0D52` the two coin-spill lists, `$0E12/$0E5E` lap
countdowns; `$0DFA = $F174` throughout (the two-block human list) and
`$0E50 = 0`.

The ROM side: the track-object kinds are a script chain at
`$81:8CB9..8D1B` (7-word records `[$9136, $0404, $91D0, handlerA,
handlerB, $90F6, next]` — `$81:91D0` arms a block with `$12 = $C000`),
one record per theme object (pipe, Thwomp, Boo, mole, …) and none for a
weapon. The only other spawner that arms a block with the projectile
handler `$F243` is `$80:F56D`, which reads records 16–23 of the track's
own object list — moving track furniture, not a kart's throw.

So in a full 100cc Mario Circuit 1 race the CPU field never armed a
weapon. It is real and it was not there; the gate is a recording in
which it happens (a higher class or a later cup is the guess, not a
finding).

## 189. The user's quick test: five fixes, four of them measured

*"1. coin animation for losing one is great. But coin animation when
picking up one from the floor is not existent. 2. lakitu's sign and the
item box when emptied present graphical issues. 3. when using a star
power up, in original game you can also 'kill' pipes. Here I just
crashed and got blocked. Also, the star animation is correct, but it is
constant. Here it is a small period with the colors, then nothing. 5. I
couldn't see any item drop. [banana: button = thrown ahead in an arc,
button + down = dropped behind; shell: button = ahead, fast, bouncing;
button + down = static behind]."* And: *"the sprite used for the green
shell is the green light for lakitu's semaphore."*

### The picked-up coin (`tools/labs/coinpick.py`, `src/coinup_path.inc`)

Same rig as the spilled coin (NOTES 186): flow-drive the attract race
over its coin rows and log every coin-tile sprite from the frame `$0E00`
goes up. The coin appears **2 frames** after the count moves, at the
kart sprite's top (x = 119 for a kart at 112..144: centred), rises 45 px
in 17 frames, comes back down past where it started and is gone at
frame 36 — a straight vertical hop, x never moving, spinning
`$A2 → $60 → $86` every four frames. Captured to `SMK_COINUP_PATH` (34
steps) and replayed relative to the kart sprite exactly like the bump
coin; `smk_coin.kind` picks the path. Spawned when `smk_pickup_step`
moves the coin count.

### The star is an OAM palette sequence (`tools/labs/starpal2.py`)

First try read CGRAM every frame after `$E0 |= $2000` and found entries
3 and 5 of four sprite palettes toggling `$2525`/`$35A9` on 4- and
8-frame periods — and a control run WITHOUT the star showed the same
toggles. That is the tyre animation, not the star. The star is in OAM:
the kart's four 16x16 sprites change palette every frame, **5 4 7 6 1 0
3 2**, one frame each, forever. The port held each palette four frames
(and cycled 0-7 in order), which is what read as "colours, then nothing".
Now `STAR_PAL[fx_ticks & 7]`.

### The item slot is the game's own map (`tools/labs/hudslot.py`)

The HUD map at `$0C00` (32 cells a row), columns 18-21, rows 0-3, through
a roulette, a held mushroom and its use:

    fresh   3CD2 3CCC 3CCD 7CD2 / 3CD3 3CEA 3CEB 7CD3 / 3CD2 3CEC 3CED 7CD2 / 3CD3 3CEE 3CEF 7CD3
    held    3CD2 34D4 34D5 7CD2 / 3CD3 34D6 34D7 7CD3 / (rows 2-3 as above)
    used    3CD2 3CE9 3CE9 7CD2 / 3CD3 3CE8 3CE8 7CD3 / (rows 2-3 as above)

Four rows, sides `$D2/$D3` alternating and H-flipped on the right, the
icon in rows 0-1 (palette 5), rows 2-3 holding the PLACE as a 16x16
glyph the game DMAs into `$EC-$EF`. There is no `$D0/$D1` top row: that
was a misread of the roulette VRAM dump (row -1 is `$1616` filler) and it
drew the black bar the user saw. The port now draws this map; the place
in rows 2-3 is set in the blob's own 8x16 digit font (`$80+d`/`$90+d`)
on a black card, because the blob holds only the one glyph (LABELLED:
ROM art, our composition).

### The lap sign's stripe

NOTES 168's OAM lists the plate first and the digit sprite (edge bar +
numeral) second, at X+8, half over the plate. Lower OAM index paints on
top, so the plate covers the bar's left half; the port painted the bar
over the plate and put a stripe through "LAP". The self-test that
"proved" the composition built its reference the same wrong way round
— a reference derived from the model, the trap of NOTES 176 again. Both
now paint bar, numeral, then plate.

### Items: controls and the static shell

Button = throw ahead (banana in its arc, shell fast), button + DOWN =
leave it behind, static, for both — the user's description, replacing
UP-to-throw. A green shell dropped behind is now a stationary object
eight pixels back like the banana (OURS: what it does when hit is the
shell reaction). A starred kart knocks obstacles out instead of crashing
(`smk_course.dead[]`, cleared at every segment respawn - OURS; the real
pipe's flight is not measured).

### Still open from the same test

The road sprites. The shared-blob tile the port used for the green
shell is Lakitu's green light (the user), and two OAM dumps after throws
showed no shell at all, so OBJ VRAM was rendered whole instead
(`tools/labs/vramdump.py` / `vramrender.py`, every sprite palette
stacked). The shell is there: sprite tiles `$100-$103 / $110-$113`, two
16x16 frames of a dome, green in sprite palette 0 and red in palette 1
— the effects stream `$C4:9C1A` the port already decodes (which is why
`$100`/`$110` never matched a puff: they are the shell's left column).
Drawn as that now, at the entities' size law, the two frames alternating
every four frames (OURS). The BANANA is not in any resident sprite tile
(`$000-$1FF` in all eight palettes), and a VRAM dump after a banana drop
differs from the shell's only in the kart's own animated quadrants — so
it is neither resident nor uploaded on the drop. Still the icon on the
road, still open.

### Addendum — the second test

*"2. The item box is still garbled. You added a second box below the
item box assuming that it was for holding the position as it is wrong.
There wasn't a second box and the number you see in screenshots is just
a placeholder for Player 1 or two until you get your first item. 3.
Lakitu's waving flag: the flag is still on the wrong hand. 5. the shell:
throwing a green shell is still using the wrong sprite. The in game
sprite is at least the same one in the item box if not more detailed.
6. I cannot drop a shell behind me with down arrow plus item button. 7.
dropping a banana just appears at the car's butt and left behind, no
animation (right now it appears overhead first)."*

- The box is two rows in a one-player race; the 2P attract race's map
  carries two more rows with the "2" the user names. Rows 2-3 and the
  place digit removed.
- The flag: every flag tile in the finish capture carries attribute
  `$6A` — bit 6, H-flip — and the port drew them straight. Flipped now.
  The lap sign, the same measurement, was read right; the flag was
  drawn from the tile list without its attribute.
- The shell: the effects-tile dome is not it either. The user's word is
  the reference: the road shell looks like the item box's shell, so it
  is drawn from the icon tiles like the banana, until a replay with one
  on screen says otherwise.
- 6 and 7 are one bug: `in.down` is the BRAKE (SNES Y: X button, left
  trigger, keyboard Down), not the d-pad, and a pad's d-pad DOWN only
  fed menu navigation. A new `dpad_down` level (d-pad, left stick down,
  keyboard Down/S) is what the item reads, so button + DOWN now drops
  the banana or shell behind; the button alone throws, which is the
  arc the user saw "appear overhead" when they meant to drop.

## 190. The AI's weapons, from the user's `attack` recording

NOTES 188 searched four sessions and found none; the user then gave the
rule — *"they attack only when the player is near and it is enabled
starting lap 2. during lap 1 they don't attack. they don't use items
between them. it happens in any cup regardless of difficulty"* — and
recorded a race built to provoke them (`sessions/attack`, MC1: *"I
couldn't fall on the banana, but I think there is a lot of data about DK
jr attacking. Peach also dropped a mushroom"*).

### Where they live

Not in the pipe pool, not in the kart block, not in `$E0`: the AI's
objects are the SAME two projectile blocks the human's items use —
`$1A00`/`$1A80`, the `$0DFA = $80F174` list. Five AI events in the race,
every one going live (or re-armed while live) at an AI kart's exact
position (d = 0), owner in `+$6A` (`$1600`/`$1700` = karts 6 and 7):

    block  frame  owner  position     player d (euclid)  lap  since same kart
    $1A80  3302   k6     (144,700)    141                $81  -
    $1A00  3893   k7     (335,127)    127                $82  -
    $1A80  4777   k7     (566,227)    153                $83  884
    $1A00  5423   k7     (769,899)     53                $83  646
    $1A80  6297   k7     (632,756)    151                $84  874

Two of them replaced the human's own live object: two slots for the
whole track, whoever needs one next. The four earlier sessions had none
because the player was never close for long on lap 2+ — the rule, not a
gate on class or cup.

### What the object does

For **58 frames** it moves at exactly its kart's velocity — 2.27 px/frame
beside a kart at (520,261), 3.11 beside (-712,-358) — with its own
`+$22/+$24` velocity words at zero, i.e. it is carried, not thrown; then
it stops dead where it is and stays (the `$1A00` object of f3893 was
still there 486 frames later, until a slot was needed). That is the
"drop behind": ridden for a second, let go. Nothing in the run moved on
its own afterwards (no fireball was provoked; the user names Bowser's
as the only non-static one).

### The port

`smk_ai_weapon_of(character)`: Mario/Luigi the star, Bowser the
fireball, Peach/Toad the poison mushroom, DK Jr the banana, Yoshi the
egg, Koopa the green shell (the user's list, SMK_DRIVERS order).
`smk_proj_ai_drop` arms a slot at the kart with `carry = 58`; `smk_proj_
step` keeps it eight pixels behind the owner (the banana's own offset)
until the carry runs out, then it is a static object; the owner cannot
touch it while carried plus the usual 60. Trigger per AI kart per frame:
weapon != none, lap >= 2, not finished, not tumbling, cooldown 0, the
player within SMK_AI_NEAR = 160 px (drops measured at 53..153) →
drop, cooldown SMK_AI_COOL = 640 (intervals measured 646/874/884).
Mario/Luigi set `star_t` on themselves instead: hits on them are
ignored and they flash the measured palette run. Reactions on the
player: mushroom = the shell tumble + shrink, egg/fireball = the shell
tumble, all with the 4-coin loss; on an AI the matching `smk_racer_hit`
kinds.

LABELLED (S31): the distance bound and the cooldown are ours, bracketed
by five events; the fireball's flight is a green shell's launch; the
mushroom, egg and fireball on the road wear roulette icons in borrowed
palettes (the roulette never shows them); the AI star's length is the
player's `$200`.

### Addendum — the user's own two objects in the same recording

`$1A00` f2962: a dropped banana — born at the kart, slid 9 px in 3
frames as the kart pulled away, then never moved (`+$70 = 0`, static).
`$1A00` f4576: a thrown green shell — `(vx, vy) = (110, -1497)`, 1,500
units = 5.9 px/frame, CONSTANT for 112 frames to a wall, where `vy`
became +1290 (7/8, as NOTES 185 measured); `+$70 = 2`. No forward banana
throw in the run, so its arc stays OURS — and it has been made fast and
high enough to be seen leaving (kart speed + $600, `zv = $180`): at a
shell's speed it hid under the kart and landed under it, which the user
saw as "does not work". The road objects now scale by the karts' own
1/distance law, continuously; the two-step integer size before did not
read as distance.

### Addendum — the road items' art: a scaler, not a sheet (the user's rip)

The user left a ripped sheet (`tmp/new/items-on-the-road.png`,
QuadFactor's rip): every road item is a SIZE LADDER — green shell 16/14/
13/11/8/6/4 with three spin frames per tier, banana 16..6 in 8 tiers,
the blue/green/red domes, and the CPUs' mushroom, egg and fireball
ladders — and the item-box icons are not among them.

Where it is in the ROM: nowhere as tiles. An eroded-mask matcher that
finds the shared blob's coin at once (the rip carries a one-pixel
outline the tiles do not) finds NO tier of any item in resident OBJ VRAM
(three dumps), in any pointer-table stream, in any decompressible
stream at any 32-byte offset, or raw, in either 16x16 tile pairing.
What the game does instead: the object's tiles are WRITTEN every frame
into sprite tiles `$160/$170` — after a throw they hold a 2x3 sliver, after
a banana drop the rip's 6x4 smallest banana exactly — and those bytes
exist nowhere in the ROM. The road items are rendered by the game's own
software scaler from a template, the way the tyre puffs are (effects.c:
templates in the `$C5:EE00` stream at WRAM `$2000`, records at
`$80:D1CE`). The ladder the rip shows is that scaler's output.

Next: the templates. The effects records name a template offset per
kind; the items are the kinds effects.c does not handle yet. Render every
record's template and the shell, banana, mushroom, egg and fireball
should be there at full size, with the scaler's law to port beside the
pipes' tiers. Until then the icons stand in (LABELLED, S31).

## 191. The AI karts' size: continuous now, because the tiers were sized for 256x224

The user, twice: *"They are normal size when far, get scaled until being
midget in mid distance, and then magically grow back to their original
size."* The tier trace (`SMK_TIER_TRACE`) was monotonic in distance every
time, and a kart parked straight ahead (`SMK_TEST_PLACE=q:d`) shrank
step by step at 512x448 - so the pick was right and the report still
true. A contact sheet of an autodrive race at the 1920x1080 window
(frame 960x540) showed it: a kart at the horizon drawn at full tier size
beside pipes the size of a grain.

The cause: tiers 0-2 were drawn at `rw/256` whatever the distance and
the mini at half of it. That is the game's own scheme, and it is right
only in the 256x224 view the tiers were made for. In a 16:9 window the
road compresses vertically, so a kart 100 px away already sits at the
horizon - at 75 px tall. What the eye then meets, driving up on a kart:
the mini (48 px) far out, then tier 2 (75), tier 1 (84), tier 0 (93) -
and with the horizon that close, "far" is the tier-2 kart looking normal
at the skyline, "midget" the mini a little nearer, "full size" the tier
0 a little nearer still.

Now the tier still picks the ART (the game's own ladder) but the pixel
size is `rw/256 * 66 / distance`, capped at the player's own - the same
law the road objects follow since NOTES 189 - through
`smk_draw_sprite_scaled`, mirror pose included. LABELLED (S10): the
continuous size is ours; the game steps.

## 192. The road items' art: the sheet is the source, through the ROM's palettes

The user: *"go and decode the real sprites. We have the reference (and we
could even use the ones in the sheet, they have been extracted from the
rom by someone else on the internet)."*

The decode was tried to the end first. The per-kind pointers in the
projectile block (`+$6E = $80:F6DB/F6E3/F6EB/...`) are script-entry
pointers, not art; the effect "templates" (`effects.c`) are OAM
assemblies of tiles, not pixels; and a scan of the whole ROM for a 32x32
kart-layout frame whose 2:1 sample is any of the sheet's full-size items
(the far-kart minifier's shape, NOTES 076) found nothing, raw or in any
decompressible stream, on top of NOTES 190's 16x16 sweeps. The bytes the
game writes into sprite tiles `$160/$170` for a live item exist nowhere
in the ROM: the ladder is computed.

So the sheet (`tmp/new/items-on-the-road.png`, QuadFactor's rip of the
game's scaler output) is imported by `tools/labs/itemsheet.py` into
`src/itemart.inc`: the green shell's 7 tiers x 3 spin frames, the
banana's 8 tiers, the poison mushroom's 7, the egg's 8, the fireball's 8.
Each item quantizes with ZERO error onto one of the game's own OBJ
palettes (shell 6, banana / mushroom / egg 5, fireball 6, red shell 5),
which is the proof the rip's colours are the ROM's - so they are stored
as palette indices and drawn through `trk->palette` like everything
else. The red shell has its own 16x16 on the sheet (its shading differs
from the green's, not only its palette); its smaller tiers are the green
ladder's indices remapped by the map the two full-size shells define,
with a red-for-green colour swap for the indices they never vote on
(LABELLED). The tier is picked by the width the karts' 1/distance law
asks for and drawn at the screen scale; the shell's three frames turn
every four frames (LABELLED: the rate).

## 193. One size law for everything on the road: the pipes' (from the eye)

The user, after the tiers went continuous: *"They look small, start
linearly to scale up, but there is a specific point where they grow
faster than how they get closer... This also happened previously with
the pipes (now the way they scale is perfect)."* And for the items: *"use
our scaling formula from the higher res shell, because switching to the
low res sprites makes them look awful too soon."*

NOTES 154b had already found this for the pipes: `$4200 / zf` and the
karts' `66 / distance` are measured from the KART, while every scale in
the renderer is measured from the EYE, 61 px further back - so over
40..400 world px the sprite shrank 10x where the ground shrank 4.6x,
and swelled as you closed in. The pipes were fixed to smk_project's own
scale (screen px per world px at the object's depth); the karts and the
items had kept the kart-measured law. Now all three use the pipes':
`ks = sc * SMK_CAM_TRAIL / SMK_PROJ_LES`, which is the player's own
scale at the player's depth and the projection's everywhere else.

With size continuous there is no reason left to change drawings: the
tier ladders were the SNES's substitute for scaling, and swapping to a
coarser drawing while the size is still large is the step the eye sees.
The karts draw their highest-resolution frame (tier 0, or the mirror
pose) at every distance; the items their largest tier. The smaller tiers
stay in the data.

Also from the same test: Bowser's fireball is not a shell - *"they don't
have speed therefore there is no bounce. Bowser puts or throws them but
they stay in place and then have a zigzag but only to the left and
right."* It is now carried and let go like every other drop, then weaves
left and right of the spot (OURS: 20 px, 40-frame period), through the
weave offset `wx/wy` the hit test and the drawing both read.

## 194. Six from the same play session

1. **The 2-coin item** now hops two coins the way a picked-up coin hops
   (NOTES 189), 4 px either side of centre, the second a frame late.
2. **Road items' hit box**: `SMK_PROJ_HIT_R` 8 -> 5 world px each way. A
   kart is ~8 world px wide and an item ~4, so 8 each way was a 16 px
   square around a 4 px object - "super easy to get affected by an item
   even if I am not even touching it" (the user). Still OURS, labelled.
3. **AI karts hit by their own drops**: the carried object sits 8 px
   behind its kart inside that square; the owner is immune while it
   carries and 60 frames after, but the kart BEHIND it was not - the
   smaller box is most of the fix, and the immunity stands.
4. **The mushroom off-road**: `can_use` refused any `hazard`, and the
   deep off-road classes are hazards (the wade drive). Only the fall,
   the carry and the drop (6 / $0C / $0E) hold the item now. (A headless
   rig to put the kart on grass was tried and did not take; the trace did
   confirm the boost itself: 0 -> 1500 in 32 frames on the road.)
5. **AI karts never spin from a bump** - "they don't get affected by the
   coin situation as the player. This applies also to them hitting
   between themselves" (the user). NOTES 187's kind-4 spin is the
   player's only; an AI bump costs nothing.
6. **The rescue**: Lakitu's descent is the captured path (NOTES 168a) on
   a frame clock from the `$0E` entry, while the kart's z ran its own
   96-frame fall and landed first - "it appears with higher altitude, so
   it only comes from the top once you are on the ground". The kart now
   HANGS from him: at the path's end his row is 38 and his block's bottom
   (70) is the kart sprite's top on the ground (102 - 32), so the kart's
   lift is `38 - his row` all the way down.

## 195. The rescue, measured from the kart's own sprite; and four more

The user: *"Lakitu drop-off is still wrong and now the kart gets reseted
when coming down. Check the rom (to test, just pick ghost valley and go
straight: after bumping a few blocks, you'll fall down. Lakitu brings
the player from the top of the screen to the ground)."*

`tools/labs/lakitu_rescue.py` now logs the KART's four sprites through
the fall, the carry and the drop (OAM rows, Ghost Valley, the game
deciding the fall):

    fall   f0..30   row 69 -> 130, the sprite drops down the screen
    carry  $0C      row 128 - below the 112-line view, unseen
    drop   $0E      row 128 + 2t, wrapping: 168 at t=19, 230 at t=49,
                    254 at t=63, then 2 at t=65 ... 70 at t=98 = released
    Lakitu (NOTES 168a's path) = the kart's row - 27, from t=65 to the end

So the kart is not lowered from a height of its own: its sprite runs
down the screen at 2 px a frame from below the view, wraps, and comes
in from the TOP at frame 64, hanging 27 px under Lakitu, and both land
together at frame 98. `$1E` reads 0 / -32768 the whole way - the z is
not the picture. The port draws exactly this: hidden until frame 64,
then lift = 70 - (128 + 2t mod 256), with Lakitu on his own captured
row; the physics z keeps its 96-frame fall for the collision side only.

Also from the same session:
- the 2-coin item's two hops share one X (the user);
- the fireball's weave period 40 -> 96 frames ("too fast" - OURS still);
- the poison mushroom SHRINKS and nothing else - "doesn't trigger
  spinning, it triggers the shrinking animation" - for the player and an
  AI alike; `$80:EA3B`'s `$0300` tumble is the lightning's routine;
- Choco Island's piranha plants and Koopa Beach's cheep-cheeps are not
  walls: touching one spins you (the banana reaction; the AI's kind 1).
  Their art was already the theme's own sheet (`$C10AA5`, `$C11706`,
  57 tiles each, the plant and the fish ladders) - the fish is
  asymmetric and the near band's mirrored-half assembly (NOTES 155) will
  not suit it; LABELLED, to be looked at with a screenshot.

## 196. The camera turns with the tumble - and only the tumble

The user: *"it was not only that the kart did a few full spins but also
the camera did some 360... it could be good to get it from the rom."*

`tools/labs/spincam.py` injects a reaction bit on P1 in the attract race
and logs the camera azimuth `$94`, the heading `$A4`, the pose lag `$AA`
and the pose `$2A` every frame:

    banana  $1000        $94 = $A4 + $C0 throughout; $2A = $A4 - $AA/2
    coinless $0800       the same
    object  $0300+$E4    $94 = $A4 + $C0 + $AA/2 ; $2A = $A4 - $AA/2

(the first shell try did nothing: `$819ACE` arms `$E4 = $2000` with the
bit, and without it the tumble has no rate.) So through state `$1A` the
camera advances `$E4/2` a frame - 4096 at the full rate, a whole turn
every 16 frames, decaying `$40` a frame with `$E4` - while the sprite
turns the other way by the same amount: relative to the camera the kart
spins at the full lag, and the world spins behind it at half. The
banana's and the bump's spins move the sprite only.

The port adds `plag/2` to the azimuth while `state == $1A`; the sprite's
relative angle was already the full lag, so nothing else moves.

## 197. Surfaces, the picture: the shake, measured per class

The user: *"we need to analize different surfaces and their effect, not
only on speed but also visual effects... they make the kart vibrate.
for example grass ($5A), or the bridge that is only vibration and sound
($50). gravel had also the effect of being super slippery ($52) or mud
that had special sound and actual mud coming from the wheel ($5E)."*

`tools/labs/surffx.py`: the road made one class at a time (the grip and
cap sweeps' `surface_fill`), P1 driven on it with B held, and every
frame the kart's own four OAM rows, every other sprite within 48 px
(tile, attribute), the speed and `$EE`. Nineteen classes, 80 frames each,
on the attract race's theme (Mario Circuit).

The kart's TOP ROW, per frame (row 70 is the resting sprite):

    $40 $52          70 69 70 69 ...                    the 1 px engine bob
    $44 $50 $58 $5A  69 69 67 69 69 70 70 70 (period 8)  a 3 px dip: the shake
    $4A $54          69 70 69 70 70 69 68 68 (period 8)  softer
    $5C $5E          72 73 72 73 ...                     SUNK 2-3 px, buzzing

Gravel ($52) does not shake at all - its business is the grip table
(NOTES 079) - and the bridge ($50) shakes with nothing else on screen,
as the user said. Ported as `smk_shake_of(class)` on the player's sprite
lift, 8-frame patterns as measured; classes not in the sweep take the
road's bob (LABELLED).

The effect sprites, same sweep (attribute bits 1-3 = palette):

    $44 $4A $4C $4E $52   tiles $100-$104, palette 5 (white smoke), while moving
    $54 $56 $58           tiles $100-$104, palette 7 (tan dust), every frame
    $5A                   tiles $024-$02C, palette 7   = kind $12 ($80:D3F3)
    $5C $5E               tiles $000-$004, palette 7   = kind $06 ($80:D3D2)
    $20 $22 $24           the water: kart hidden / sunk, Lakitu's tiles - the fall

and the handler table is `$80:D31A + class` (words): $00-$1E and $40-$52
-> `$80:D37A`, $54-$58 -> `$D3B6`, $5A -> `$D3F3`, $5C/$5E -> `$D3D2`,
$20 -> `$D40B`, $22/$24 -> `$D418`, $26-$3E -> nothing. The templates of
kinds $06 and $12 name sprite tiles $000-$00E and $024-$02C - Lakitu's
cloud puffs, identical on every theme - while the $100-$11F puffs are
the theme's own (3 of 32 tiles shared between Mario Circuit and Koopa
Beach). Neither art is in the shared blob or any aligned stream; where
the game DMAs them from is the next step, and the port's effect kinds
$06/$12 and the white smoke on the road family wait on it.

### Addendum — where the effect art comes from (tools/labs/dmalist.py)

Logging every VRAM-bound DMA from reset to the settled race, with the
VRAM address each landed at:

    VRAM $4000  spr $000   <- 7F:C000   64 tiles   then <- 84:C500  32 tiles (earlier; overwritten)
    VRAM $4400  spr $040   <- 7F:C200  128 tiles   (the HUD / Lakitu set)
    VRAM $5000  spr $100   <- 7F:6C00   32 tiles   (the theme's puffs)
    VRAM $5200  spr $120   <- 7F:C800   52 tiles   (the results faces)

`$7F:C000`'s 64 tiles are the stream at **`$C0:0903`**: it decompresses to
exactly 2048 bytes and tile n of it is sprite tile $000+n, byte for byte,
all 64 - the cloud puffs the spray and the splash assemble, identical on
every theme. The port loads it as `smk_effects.lo[64]` and the draw
takes tiles under $100 from there (attribute bit 0 clear); kinds $06
(spray: $5C/$5E) and $12 (splash: $5A) are in, with $36/$3C for the
near-stopped variants the handlers pick. The theme puffs at `$7F:6C00`
are the `$C4:9C1A` stream on Mario Circuit; the other themes' are being
looked for the same way.

### Addendum — the puffs are global; the water kinds

Re-dumped 700 frames into Koopa Beach 1 and Choco Island 1, the theme
puff tiles `$100-$11F` are the `$C4:9C19` stream on both, all 26
non-blank tiles - the "3 of 32 shared" of the first dumps was the
countdown, before the upload. And `$120-$13F` stays the results faces on
Koopa Beach: the water kinds `$0C`/`$30` name tiles 32-34 / 49-51 with
attribute bit 0 clear, i.e. sprite `$020-$022` / `$031-$033`, cloud puffs
from the same `$C0:0903` set. So every effect kind's art is one of two
global streams, and the themes differ only in which kind and palette a
class asks for. Kind `$0C` is now picked on the shallow water ($22/$24,
`$80:D437`) while moving; `$20`'s handler (`$80:D40B`) is the deep
water's and stays out.

## 198. The cup around the race

Grand Prix is no longer refused at the mode screen. A cup runs its five
courses in the ROM's order (`$81:EC1B`, NOTES 147), and after each result
a STANDINGS screen: every driver by points, this race's place beside.
Points are the ROM's own words at `$85:BEB4` - 9, 6, 3, 1 - which the
results code at `$85:C0C6` reads for a kart whose index in the order
table `$010E` is under 4 and for nobody else; the port awards them from
the finishing table it already builds (NOTES 179) to the top four
drivers, the AI included. A player who finishes fifth or worse is
RANKED OUT: no points that race, and ENTER runs the same course again
(the retry is the game's rule as played; its exact text and the AI's
points on a retried race are not measured - LABELLED). After the fifth
course the standings are final and ENTER returns to the course screen.
Self-tested through the shell's own state machine.

## 199. The winner's pose, from a real finish (S28 closed)

Three forced-finish rigs (the lap word poked to the last lap, NOTES 178
and 184's) never showed it, and this one explains why: after a faked
crossing the game drops the kart from OAM entirely and freezes the
camera azimuth. So `tools/labs/winpose_real.py` drove a real race - the
seven AI karts held still every frame, P1 on the flow field for five laps
(5,956 frames), a genuine first place - and after the crossing compared
the sixteen tiles the game uploads for the kart (sprite $180-$1B3) with
every 32x32 frame of Mario's sheet, every frame, with OAM and VRAM dumps.

    +0..+5     frame 47 (the drive), $94 leaving $A4: the camera swings
    +10 +20    frames 2, 4, 5 ... +60 frame 10      the rotation frames
    +90        FRAME 46, exact - the front view; $94 = $A4 + $8000 now
    +180 on    frame 46 with FIVE tiles different, alternating with the
               exact 46 (+280's dump was exact again)

The OAM at +200: two 16x16 sprites at tile $180 (x 112) and $180 H-FLIPPED
(x 128), and the same for $1A0 - the kart is its left half and that
half's mirror, exactly the "mirrored pose" NOTES 080 found for the rear
view, on the front. The five changed tiles are rows 0-2, columns 0-1 of
that half - the driver's head and arms - and every one of them is a tile
of the sheet's first 64-tile band: 3, 16 (18 is its twin), 19, 34, 35.
Nothing is in the packed frames 33-43: NOTES 180's "frame 40, upper
right" is superseded; that reading came from a quadrant that happened to
hold a head.

The port builds frame 48 at load - frame 46 with those five tiles
swapped in - and, while celebrating with the camera on the kart's front
(|rel - $8000| < $C00), draws 46 or 48 as a mirrored half: 46 until
SMK_WIN_ARMS_AT frames after the crossing, then the two alternating on
SMK_WIN_TOGGLE. The per-frame capture (`winpose_real2.py`, the same
race again) pinned both: frame 46 exact from +85, the five arm tiles from
+101 for 16 frames, plain for 16, and so on - period 32, arms up first.
Nothing about the pose is labelled any more.

## 200. The track map

The user's list, item 12: *"Everyone's position on a small map. The
original only has one because the screen is split; ours must not cost
the big view."* So the art is the course's own tilemap - `smk_track_texel`
sampled 2x2 per cell into a 96 px square at race load - and the
placement is ours: the bottom-right corner, three parts map to one part
scene so the road shows through, every kart a white dot and the player
a larger gold one drawn last. M toggles it. Ledgered as S33 (OURS: the
placement, the size, the dots; the picture is the ROM's).

## 201. Sound: the route, proven on one song

P7 decided pre-recorded, and the question was how to record the game's
own music without an SPC700 of our own. The toolkit has none
(`smktool/apu.py` only answers the 65816's waits), but MAME has, and
`ffmpeg` here is built with libgme, which plays `.spc` files.

`tools/labs/mame/spcdump.lua` replays a recorded session and, on the
frames asked for, writes an `.spc`: the sound CPU's 64 KB (`:soundcpu`
program space), its registers (`state`: PC S P A X Y), and the 128 DSP
registers. Two traps, both hit: `-sound none` leaves the DSP idle (its
registers stale - MVOL 0), so the replay runs with `SDL_AUDIODRIVER=dummy
-sound sdl`; and the `:s_dsp` data space does not return the live
register file either - the registers must be read the way the SPC700
reads them, index into `$F2`, value from `$F3`, `$F2` restored.

Snapshot at frame 3000 of `sessions/cc100` (Mario Circuit 1, racing):
MVOL 96/96, DIR `$3C`, and libgme renders 45 s of audio (rms ~1500) -
the driver playing on from that moment, so the file IS the course music
(with whatever engine voices were live at the snapshot decaying out).
What remains for P7: a snapshot per theme and for the title / menu /
results / GP-end music at moments with no engine voice (the countdown,
the results screen), loop points, and the port's playback - a WAV per
song mixed under the race (S8). The user chooses the moments by ear.

Addendum: the menu-time snapshots came out silent, and the reason is the
timer control register `$F1` - write-only, so the snapshot holds 0 and
the driver's loop never ticks under libgme. Patching the byte to 1
(timer 0 enabled; 7 plays the same) makes frame 1000 (the menus) and
7600 (after the race) play; frame 1900 stays silent because the game is
silent there. The dumper writes the 1 itself now.

## 202. Music in the port: pre-recorded, user-mapped

`src/audio.c`: SDL audio, one music channel, fed by SDL_QueueAudio once a
frame and looping the file whole. The state picks a KEY - `title`,
`menu`, `results`, `theme0`..`theme7` (the course's theme in a race) -
and `rom/music/map.txt` (beside the ROM, one `key file.wav` a line,
written by the user) maps keys to files rendered by NOTES 201's
spc-snapshot route. A missing map, key or file is silence, never an
error; nothing derived from the ROM is committed (rom/music is
git-ignored). N toggles the music. LABELLED (S8): loop points are
whole-file; the engine and item SFX are still absent.

## 203. The feather flew for one frame: the item switch runs after the field's sync

Bug 6 of the user's list. `smk_player_feather` was right all along - zvel
`$01E0`, drive 2, state $18 - and a trace showed the launch land in the
kart (`air 1, zvel 480`) yet the very same frame's end read `air 0, z 0`.
Not the physics: a gdb watchpoint on `kart.airborne` named the line. The
main loop copies `me->k = kart` BEFORE the item switch, runs the item
(which launches `kart`), then the kart-vs-kart collide pass copies
`me->k` BACK - the pre-item snapshot, grounded, erasing the flight one
line after it began. The next frame's pose machine saw a grounded $18 and
settled out, so the feather "did nothing" while every piece of it worked.

One line fixes it: re-sync `me->k = kart` after the item switch. The test
rig (`SMK_ITEM_TEST=1:150`) now shows a ~37-frame arc, peak z ~16 px,
the pose rolling +$0800 a frame - $80B6D1's rate, the 360 the user asked
for - and a clean settle through $1C on landing. The launch strength is
OURS (S34); the ROM's own feather arc is still unmeasured.

The same batch, from the user's list: deep water on themes 2/4/5 routes
class $24 into the $22 fall-in (full control, crawl, Lakitu - bugs 7/8);
Rainbow Road's Thwomps spin you and flash (bug 11); the field extrapolates
a time for anyone still out 90 s after the winner (bug 4); the rescue
Lakitu is drawn from the kart's own screen row (bug 3). And one found on
the way: a misplaced `else` printed "warning: item tables not loaded" on
every SUCCESSFUL load since the line was written.

## 204. The Thwomp squash (bug 13): the user's flattened sheet, per driver

tools/labs/flatsheet.py imports tmp/new/flattened-racers-after-thwomp.png
(the user's rip) into src/flatart.inc: 8 blocks of 3 poses in two sizes,
each quantised against one of the four DRIVER palettes in the oracle's
CGRAM - every block fits one palette with near-zero error (<=23 avg sq),
which also NAMES the blocks: Mario, DK Jr, Bowser, Luigi across the top,
Yoshi, Koopa, Peach, Toad below (the two green blocks assigned by eye
within their palette family, S35).  Drawn through `trk->palette` so the
per-theme re-tints apply, nearest-neighbour at the continuous scale,
anchored at the wheels.

The rule: `smk_collide_objects` flags hazard kind 2 when the kart stands
in the footprint of a mover in its FALL phase, checked BEFORE the
overhead skip - a kart cannot bounce off the underside of a descending
block, and the drop takes ~15 frames, faster than any kart leaves a
6 px footprint.  (First cut waited for z < 400: that band lasts ONE
frame of the drop and nothing was ever squashed.)  Player and AI both:
squash_t = 100 frames flattened at speed 0 (OURS, S35).

Found on the way: in every autodrive race the Thwomps NEVER moved - the
release is `crossings >= 2` and the autopilot never finished a BC lap,
so 22,000-frame test races had zero falls.  SMK_MV_ON=1 now releases
the movers from frame 0 for rigs; the climb is clamped at the parked
height (the trace showed blocks rising to 7680 and falling from up
there); and two selftest checks pin the squash and the overhead pass.
SMK_SQUASH_TEST=frame flattens P1 and kart 1 for an eyeball shot.

## 205. Bug 14 was two ghost Thwomps: the spawn offset is a TABLE, and its fifth entry is zero

The user: "Bowser Castle 1 has one or two Thwomps in the wrong place."
BC1 is track 17 (the $0150/$0152 boot into cup 0 course 3 lands there).
Its start-segment entities match the game's live blocks EXACTLY -
(388,68) and (388,52), read from the booted game's $1800/$1880 blocks -
so the positions were never wrong.  The WINDOW was: the port respawned
`seg * 4` words into the entity list, but the game takes the offset from
the word table at $84:DAC5 - 0, 8, 16, 24 bytes... and then ZERO.  The
fifth segment respawns the FIRST pair.  With five thresholds on BC1
(9/16/23/32/$FF) our linear rule reached entities 16-17 - (396,44) and
(956,148), a pair the game never spawns on any segment.  One or two
Thwomps, in places the game keeps empty.

Verified by driving the game's own spawner: tools/labs/bc1seg.py boots
BC1, pokes the player's waypoint ($10C0) through every segment, and logs
the spawned blocks - segments 0-3 give entity pairs 0/1, 4/5, 8/9, 12/13
(our windows agree), and waypoint 40 ($0D34 = 8) spawns (388,68)/(388,52)
again.  Ported: smk_course carries seg_off[] read from the table, and
smk_course_spawn indexes it instead of multiplying.  Selftest-pinned on
both the table bytes and the wp-40 respawn.

## 206. Bug 12, the moles: what the probes ruled out, and the recording needed

Choco Island 1 is track 18 (cup 1 course 0; CI2 is track 10).  Booted
there, the live entity blocks are STATIC for 900 frames - their +$18/+$1C
words match entities 0/1 exactly - and each entity is a PAIR of records
with handlers $84:E06E and $84:E07A (the same two-record shape as every
theme).  Parking the kart ON one produces a single write burst (+$06
counts down to 0, +$2C/+$2E/+$38 take $7F7F/$7F65) and then nothing for
600 frames.  So the moles that pop from the dirt are NOT these blocks:
these are the piranha plant(s) the port already draws and spins.  The
moles are almost certainly a dynamic spawn family (the $0800 blocks that
objspawn.lua watches alongside $1800), and those come alive under a real
drive, not a parked probe.

Ready meanwhile: tools/labs/molesheet.py imports the mole's ten-step
size ladder from the user's ripped hazard sheet against Choco's own
CGRAM (tmp/cgram_c10.bin, palette 0; avg sq err ~1449 - the rip's tans
sit between the palette's, labelled).  src/molart.inc waits unused.

THE ASK: a MAME recording on Choco Island - drive the dirt, let moles
pop, catch one on the face and shake it off - the same route that
cracked the AI weapons ("attack") and the coin pickup.

## 207. Round 2, the first sweep: the wall clock fooled the water rig, not the code

WATER (bugs 4/5/6).  The class maps (new SMK_SURF_MAP, a 128x128 PGM of
every cell's class) prove Donut Plains / Koopa Beach / Vanilla Lake deep
water is ALL class $22 - round 1's fix keyed $24 on those themes, dead
code, now gone (KB's fine shallow water is driveable $5C; VL's ice holes
are the $26 drop).  waterlab/waterlab2 measured the live game's $22
cycle: skim at speed (about -$A0 a skip), fall-in with $CA = $FF - the
$0102 the port used is the RE-drop value - crawl +1 a frame to 123,
rescue when $CA runs out.  The port runs the same cycle end to end once
SMK_PLAYER_AT can park the kart in a pond (fixed: the collide pass's
copy-back erased the teleport - NOTES 203's lost-write again).  What the
user could not see was the PICTURE: the kart sat at full height on the
water; it now rides 9 px low while wading (OURS).  And the "frozen" test
races that looked like the sink breaking were the WALL CLOCK: a --frames
run that finishes in 10 s gets ~590 sim ticks, whatever the frame count.

FEATHER (bug 3).  $80B6D1 disassembled exact: while airborne $AA steps
$0800 a frame, and when the step wraps through zero with $26 already
negative the spin ends - $AA = 0, state $1C.  One full 360, landing
straight.  The DRAW was the visible bug: frame_for's bands cap at the
side-on frame, so half of every spin showed one frame; the six spin
states ($0A/$0C/$0E/$10/$18/$1A) now draw through the measured full
rotation rule.  Shots at f160/f172 of the test rig show the kart high
over its shadow in mid-rotation.

THE CAMERA (bug 14).  $AA is the spin's home in the feather's own
handler, so $94's $AA/2 term is not a state-$1A special: cam_spin now
rides every spin state.

SMALLER: the star's colour run died on the TURNING sprites - one of the
three player draw branches drew drv->pal (bug 17); the squash waits for
the CONTACT band of the drop, and a descending block overhead is
neither wall nor hit (bug 11, selftest-pinned both ways); Rainbow
Road's Thwomps spin AND stand (bug 9); load_race never rebuilt the
minimap, so every shell race showed the boot track's map (bug 13); the
near AI draw rounds its centre instead of truncating (bug 16,
attempted); SMK_SURF_FILL brings the oracle's surface_fill to the port.

## 208. Round 2, the second sweep: the star has no pause, and the small kart is the same art at half size

THE STAR (bug 18).  starlab/starlab2 poke a READY star ($0D70 = $C002),
press A, and watch $86 with the class under the kart: on $40, $4E, $54
and $5A alike it runs down 1 a frame and the star ends at 512.  The
"pauses at 1 below $52" reading in docs/ITEMS.md (now corrected there)
never manifests.  The port's flat $200 was right all along.

THE SMALL KART (bugs 19-22).  shrinklab2 pokes $84 = $440 and diffs the
whole OAM: the big kart's four 16x16 blocks ($80/$82/$A0/$A2) become
$80/$A0 - the straight pose's LEFT column - drawn twice each, one
h-flipped, with the OAM size bits dropped.  Half size, same art,
mirrored half: the exact operation the port's half-scale draw already
performs.  $84 ticks 1 a frame (1088 = ~18 s), watched live.  New rules
from the user, ported: a hit while small SQUASHES (the bug-13 flatten,
player and AI, every kind but the lightning itself), and a second
poison mushroom restores full size.

ALSO IN: Rainbow Road's Thwomp is the ripped BLUE sprite - zero-error
on RR's own OBJ palette 1, and palette 2 is the same sprite's flash
coloring (three entries differ), so the flash is a palette-pair swap
(the period is OURS, S36); Lakitu now takes his two-coin fee after the
drop and rises away while the kart is held (OURS: the pacing); the
double feather-roll (an older copy of the step next to the pose
machine) is gone - the flight held 33 frames at the single $0800 rate.

## 209. The user's recordings: FOUR live entities, and the cheep-cheep measured

Three MAME recordings (cheep-cheep / choco / moles) played back through
a new per-frame block logger (tools/labs/mame/entblocks.lua: every $18xx
entity block's position and motion words, with P1's own position and
state on the same line).

FOUR LIVE, NOT TWO.  Both short recordings show four entity PAIRS alive
for the whole race - Koopa Beach 1's four fish are exactly track 13's
decoded list entries 0-3, Choco Island 1's plants are entries 0-3 of
track 18 - so the $84DAC5 windows are four words wide and $819136's
"two in a one-player race" reading (NOTES 078) was BACKWARDS.  Ported:
want = 4 in 1P.  That closes round 2's bug 12 as well: BC1 and Rainbow
Road were running half their Thwomps.

THE CHEEP-CHEEP (bug 10).  One fish per entity.  Its jump: the +$26
word saws from +316 down at 18 a frame to -316 and relaunches - a
~35-frame leap peaking ~11 world px - while the block wanders a few px
around its list spot.  The port draws the ripped fish ladder (zero
error on KB's OBJ palette 6, WHITE with the red back - the user's
colours) with that parabola, two flip frames per hop (the flip period
and the per-fish phase stagger are OURS).  Touching one spins and
passes through, as before.  The recording also shows the touch drops
the player into state $02, not the $0A/$0C banana pair - the felt
result is the same spin; noted, not chased.

## 210. The moles, measured and in: Donut Plains' entities, a 130-frame pop, a latch that lasts forever

The moles recording's third race is DONUT PLAINS 2 (track 2 - cup 1's
third course), and its four live blocks sit on track 2's own entity list
(entries 0-3, each +6,+6 from the hole).  So the moles are the same
entity system as the plants, fish and Thwomps - the per-track handler is
what differs - and the spawn windows apply unchanged.

THE POP (tools/labs/mame/moledump.lua, every word of one block, 300
frames): the block's +$20 steps 0 -> 6 -> 0 - about 12 frames rising,
9 held, 12 sinking - on a ~130-frame period, with +$1E animating only
while up.  Ported as smk_mole_step(): the exact profile, the per-mole
stagger OURS (the movers' 37).  A mole underground is nothing at all -
not drawn, not solid; popped, touching it LATCHES it onto the kart.

THE LATCH (tools/labs/mame/kartdiff.lua at the recorded touches): the
kart block takes the mole block's ADDRESS at +$50 with counters at
+$52/+$5E, and the ride sits at crawl speeds for as long as the user
cared to let it ("they can stick forever").  Ported: mole_on caps the
kart at $100 (the cap is OURS - the recording shows a crawl, not the
constant), the mole is drawn riding the driver's head, and three fresh
hops shake it off (OURS: the user never shook theirs, so the real
shake-off is unmeasured).  The art: the ripped mole ladder fits DP's
OBJ palette 7 with ZERO error - it was Choco's palette 0 at avg err
1449 until the recording relocated the moles to Donut Plains.

## 211. The sound effects: the game's own ids, captured from the running game

THE PATH.  `$81:F57A` is the play-sound call - A = the sound id - and it
queues at `$0E6C,x` with the index in `$0E6A` (`$81:F5E2`, three a
frame).  Poking that queue asks the REAL driver for the REAL sound in the
real race state, which is what the capture rig does.

THE IDS, from the ROM itself (a scan for `JSL $81:F5xx` sites with the
`LDA #imm` before them - 50 sites, ~30 distinct ids):

    $21 $80:B555 hop, and $80:B68C the $2A bump    $22 $80:B6B9 mole
    $23 $80:B66A the drop        $24 $80:B57B feather (docs/ITEMS.md)
    $25 $80:B20E hazard machine  $27 $80:B5BB water ($22's handler)
    $28 $80:B647 lava / the pit  $2A $80:B75A spin out, $80:A9A8 skid
    $48 $80:B48C mushroom        $49 $85:B10F item box
    $4C $80:B204 hazard          $55 $80:9B32 coin
    $65 $80:A497 lap             $68 $80:8A2A the lights
    $2C/$2E/$2F $85:85xx/$94xx/$95xx the menu; $20/$29/$37/$4A/$4B/$5D/$5F/$64 elsewhere

THE CAPTURE (tools/labs/mame/{sfxgrab.lua,grab.sh} + tools/labs/sfxcut.py).
MAME replays a recorded race twice with `-wavwrite`: once poking ONE id,
once poking nothing.  The emulation is deterministic, so subtracting the
two leaves the effect alone.  One id per run matters: poking perturbs the
driver's channel juggling, so a second poke in the same run sits on the
first one's wake - one per run gives 45-70 dB over the residual where
eight per run gave 10-15.  sfxcut then trims each slot to where the
effect is actually sounding, fades 10 ms both ends and normalises.
31 effects, in rom/sfx/<ID>.wav beside the ROM (git-ignored, like the
music).  `smk --sfx` plays them all with the ROM's own name for each -
for naming the rest by ear.

WHAT IS NOT IN.  The ENGINE.  It is not in this queue at all: the only
per-frame-ish id, $7E (720 requests in one race), captures as SILENCE, so
it is a parameter update, not a sound - the engine note lives inside the
driver, pitched from a value the 65816 writes.  Two dead ends worth not
repeating: id $17 wedges the driver (everything after it is one steady
820 Hz tone, and it stops answering requests), and $7F - the id the boot
code sends through $81:F504 - does not stop the music either.

## 212. The engine note: a parameter, not a sound - and its pitch law, measured

The engine never goes through the sound queue (NOTES 211).  `$80:9643`
is `LDA $42 / STA $2142`: the 65816 hands the driver ONE BYTE a frame on
APU port 2, and the driver holds a tone at that pitch.  ($81:A26F's
`LDA #$00 / STA $42 / STA $43` is the driver's own silence, which is how
we know 0 is off.)

THE PITCH, measured by PATCHING THE ROM IN MAME: `A5 42` -> `A9 vv` pins
the parameter for a whole replay, so a run is a steady tone and its
spectrum can be read straight off.  Against the median spectrum of the
whole sweep (which cancels the music), the tone is unmistakable:

    $10 514 Hz   $18 572   $20 632   $28 692   $30 752   $38 812   $40 872

- exactly 60 Hz per 8 of the parameter, so **f = 392 + 7.5 * v Hz**, dead
linear.  The timbre is nearly a pure tone: the second and third
harmonics measure 0.10 and 0.15 of the fundamental and nothing above is
over 0.04.  The port synthesises from that profile (a single-cycle table
stepped at the wanted pitch) rather than looping a capture, because a
capture cannot be pitched without clicking and the diff-isolated engine
was only 6% harmonic energy - the music does not cancel when the engine
voice moves.

THE REV is the ROM's own, `$80:9543`, transcribed: `$C2,x` accumulates
+$00C0 a frame while the throttle is held (bit 15 of the pad word at
`$0020,y`) until it reaches $4F00, which sets the over-rev flag `$C4,x`;
the flag then costs $0280 a frame until the rev falls back under $3F00
and clears it - the SURGE you hear at full speed, not a constant note.
Off the throttle it decays $0180 a frame to a $0100 idle.  The
parameter is the rev's high byte, so the note runs 400 Hz idle to 984 Hz
at the limit.  `smk --sfx` plays the sweep after the effects.

VERIFIED end to end (and it was NOT working when first written): SDL's
`disk` audio driver writes the port's own output to a file -
`SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=x.raw` - so the mix can be read
back without a speaker.  The first three attempts dumped pure silence,
which looked like a broken hook and was really the rig: the runs never
left the countdown, where the rev is $01 and the engine is meant to be
silent.  Long enough to reach the throttle, the dump reads 425 Hz at
idle, 969 Hz at the limit and 840-867 Hz through the over-rev surge -
the measured law, out of the port's own speaker path.

## 213. Reading the sound off the CHIP: the BRR samples, and the engine's real pitch

The user, after listening to NOTES 211's captures: "they all have the
music in the background... probably we need a different approach for
dumping sounds.  The only one that came clean was the engine sound,
although with a higher pitch."  Both halves of that are right, and both
have the same fix: stop recording the speaker.

THE SAMPLES.  The SPC snapshot (tools/labs/mame/spcdump.lua) carries the
whole 64K of sound RAM, and the DSP's DIR register ($5D = $3C here)
points at the sample directory: four bytes a sample, start and loop.
tools/labs/brr.py decodes the game's BRR blocks - a shift/filter header
and sixteen 4-bit deltas through the SPC's four filters - so every sample
the game has can be read straight out, perfectly clean.

THE EFFECTS.  tools/labs/mame/voicedump.lua logs every voice's SRCN,
PITCH, VOL and ENVX EVERY FRAME, once with the sound poked and once
without; tools/labs/sfxrender.py keeps the voices that change in the
first frames after the poke (the driver answers about five frames later
- the first window of five missed it entirely) and rebuilds them from
the BRR samples at the logged pitch and envelope.  Music cannot bleed
in because none of it is ever rendered: a voice the effect STEALS is
rendered as the effect, and the note the song would have played there is
simply absent.  The mushroom, as a check, comes out as one voice, sample
$13, a 0.96 s pitch sweep from 2031 to 3359 Hz with a decaying envelope
- and nothing else.

THE ENGINE'S PITCH.  It is DSP voice 7, sample SRCN $02 (a 1920-sample
loop), and its pitch register is exactly

    P = $4700 + 34 * v          (measured at ten values of v)

so the sample plays at ((P & $3FFF)/4096)*32000 Hz - 14.3 kHz at idle,
35 kHz at the limit.  NOTES 212's synthesis was an octave and a half
sharp because the spectral peak it fitted (632 Hz at v=$20) is the
sample's NINTH partial, not its pitch: the loop's own period is 320
samples, so at that rate the engine's fundamental is about 70 Hz.  The
port now loops the game's own sample at the game's own rate, so neither
number can be wrong again.

CAVEAT worth keeping in sight: the samples and the voice logs both come
from a snapshot taken DURING A RACE (Donut Plains, frame 2400 of the
`moles` recording), so every effect is rendered through the race's
sample bank.  The menu sounds ($2C/$2E/$2F) are requested there too and
render, but if the driver swaps banks between states their samples
belong to a different set - re-capture from a menu snapshot before
trusting those three.  Also measured on the way: the driver answers a
request on VOICE 3 in this state, every time.

## 214. Naming the sounds by what the GAME does, and the engine's parameter fitted

The user auditioned NOTES 213's renders and could not place most of them
out of context ("had to skip too many since I had no recall of them"),
which is the right answer to a bad question: a 60 ms blip means nothing
on its own.  So the naming moved to the recordings.
tools/labs/mame/sfxevent.lua logs every sound request WITH the state
around it - speed, the drive and pose states, the hazard, the surface
class, the coin count, the item word - and the ids name themselves:

    $20  the COIN        41 of 63 requests land as the coin count rises
    $21  the HOP         fires exactly as jump_state goes to 2
    $25  the LANDING     drive $02 -> $00 with z falling to 0, every time
    $23  the RAMP        airborne (drive $02) on all 88
    $4E  the BIG JUMP    airborne at speed ~1620, the ramps
    $4C  MUD             all 13 on surface class $5E and nowhere else
    $55  the ITEM BOX    16 of 22 with the item word changing
    $48  the MUSHROOM    all 35 in the boost drive state $10
    $22  the MOLE        at the mole's own grab (NOTES 210)
    $3F  the WALL        drive $16, the bounce state, with big slowdowns

and the user's ear supplied the rest: $24 feather, $27 falling off the
road, $29 hitting another kart, $2C/$2E/$2F the menu, $4D menu
scrolling, $5D/$5F the poison mushroom shrinking and growing back, $66 a
kart spinning out, $68 the finish.  Half of NOTES 211's ROM-call-site
readings were WRONG - the coin was $55, the item box was $49, the finish
was the lights - which is worth remembering: a call site says where the
code plays a sound, not what the sound is.

THE ENGINE, corrected.  The user: "perfect, but in game is slower."  It
was: the parameter $42 is written from $C2 INSIDE the sound update and
the physics reuses $C2 later in the same frame, so NOTES 212's
transcription of $80:9543 could never be checked - and it ran the
parameter at $43-$4F where the game's own trace (10,172 logged frames,
tools/labs/mame/revlog.lua) sits at a median of $39 and a maximum of
$4E.  The port now walks the parameter toward speed * 0.07, at most 3 a
frame up and 1 a frame down, which is a FIT to those frames rather than
a transcription (S38) - and it puts the note where the game keeps it.

## 215. "Some sounds are just a part of the whole sound"

The user's second audition, and the sentence that matters: several of
these renders are fragments.  Three things came out of chasing it.

FIRST, the renders WERE cut short.  sfxrender stopped a voice at its
first silent frame; several effects are two or three bursts with gaps
between them, so the gap ended the render.  It now stops only when the
baseline is playing exactly the same thing again and STAYS that way for
eight frames.  Everything got longer - $48 0.97 -> 1.29 s, $5D 1.58 ->
1.92, $61 1.84 -> 2.46, $20 and $21 tripled.

SECOND, a sound can be SEVERAL IDS.  The recordings show ids firing
within a few frames of each other at one event - $3C+$3F against a wall,
$20+$42, $20+$21 - and, more often, an id firing again one frame later
($25 in 120 bursts, median two requests; $48 once for 27 requests over
12 frames, the whole boost).  But those composites CANNOT be rebuilt by
poking: the driver answers every request on voice 3, so a second id -
even three frames later - simply replaces the first.  The composite
renders were all just the second sound, and were dropped.

THIRD, and the real conclusion: an isolated 200 ms fragment is the wrong
thing to ask a person about.  The user got through two passes and could
place a third of them.  What they COULD name were the long, distinctive
ones (the poison mushroom, Boo, the finish, the menu) - and what they
could not are exactly the short ones the game layers under an engine.
So the naming that is left belongs IN PLAY, with the effects wired to
their events, which is now the case for fifteen of them.

Their names, applied: $27 falling off the road, $29 hitting with a
shell, $2B firing an item forward, $39 an AI kart taking a hit, $55 the
item chosen off the roulette (not the box), $56/$57 Boo, $65 the
feather, $66 an AI kart falling, $68 the finish, $5D/$5F the poison
mushroom both ways, $4C mud, $4D menu scrolling, $2C/$2E/$2F the menu.
And the engine, at last: "correct - the rev and its pitch".

## 216. What the user heard in play, and what each fault turned out to be

Five reports from a race, and every one had a cause worth writing down.

**"The coin sound is wrong, or maybe incomplete - two tones, low-high."**
Right on both counts, and the fix was two bugs deep.  First, sfxrender
was rendering up to eight frames of the BASELINE after an effect ended,
so every effect carried a musical tail - that was a phantom second tone.
Second, and the real one: the coin IS two tones, and the poke rig had
been losing the second.  A live pickup (frame 2072 of the moles
recording, watched on the chip) plays sample $0D at pitch $17AD, then
THREE FRAMES LATER re-keys the same sample at $1F96 - a fourth higher.
The poke captured only the first because in that particular frame the
music reclaimed voice 3 in between.  So each effect is now captured at
FOUR different moments and the fullest render wins; the coin comes out
1967 Hz then 2633 Hz, which is the two tones, low-high.

**"Kart engine idle should be present since the race starts."**  The
game's trace holds $42 at $01 for the whole countdown - an idling
engine, not a silent one - and the port was treating $01 as off.  It
also climbs about 0.4 a frame while the throttle is held at a standstill,
which is the rev you hear before the lights (and the cue the turbo
launch is timed against).  Both in.

**"Jumping has wrong or incomplete sound."**  Same truncation: $21 is a
rising sweep from 267 to 639 Hz over 0.41 s, and it had been cut to
0.07 s.

**"Lakitu's semaphore lights have a sound."**  It is not a sound EFFECT
at all - nothing is queued during the countdown.  It belongs to the
music driver's start jingle, which is why it vanished when the music
went off.  tools/labs/sfxrender.py grew a no-baseline mode that renders
every voice of a passage for exactly this; the passage is captured but
not yet wired.

**"When slipping, there is a missing sound."**  A lead, not an answer:
ids $5D-$62 are played by $80:96A6/$80:9721, the code that also flips
bit 7 of $42 - the engine parameter - so they are an engine/surface
family, not one-shots.  (The user named $5D and $5F as the poison
mushroom by ear; the ROM plays them from bank $84 as well.  Both can be
true - the driver reuses samples - but the skid is in this family.)

**"Menu items are playing two sounds."**  Not reproduced headlessly yet;
the port makes exactly one call per menu action.  Needs SMK_SFX_TRACE=1
from a menu to see which two ids actually play.

## 217. Lakitu's lights, taken out of the music: three beeps at frames 187, 258, 331

The user: "let's make Lakitu's countdown separate from music.  It is
super important to have timings right for hinting the user when to push
the accelerator for the turbo start."  Right - the turbo window is 95
frames before the green lamp (NOTES 143) and the lamp itself is easy to
miss, so the beeps ARE the cue.

Found by watching the countdown on the chip.  `$0146` is the game's own
start counter ($80:9FE1 loads it with -336 and it counts up to 0 at the
green light), which puts the countdown at frames 1282-1618 of the moles
recording.  Over that window the driver keeps five voices busy - it is a
passage of music, not silence - but VOICE 0 keys on exactly THREE times:

    countdown frame 187   sample $0E  pitch $0FD2     656 Hz
    countdown frame 258   sample $0E  pitch $0FD2     656 Hz
    countdown frame 331   sample $0E  pitch $1FB9    1320 Hz  (an octave up)

That is the beep-beep-BEEP, and the last one lands five frames before
the lamp goes green.  Rendering voice 0 alone over each of those gives
the two sounds - rom/sfx/count_beep.wav and count_go.wav - with no music
in them at all, and the port fires them off its own countdown counter at
those frames, whether the music is on or off.

Verified through the port's own output (SDL's disk driver, engine muted
so the beeps stand alone): 662 Hz, +1.20 s, 662 Hz, +1.20 s, 1323 Hz -
against the game's 1.18 and 1.21 s.  The turbo window opens at frame
241, which is 17 frames before the second beep, so "press on the second
beep" is now a cue a player can actually hear.

(The renderer grew SFX_SKIP_VOICES for this: voice 7 is the engine and
the port synthesises that itself, so a countdown capture must not carry
a second engine inside it.)

## 218. Five more from a race: the rev's rates were backwards, and the roulette fires on the STOP

**"If I start over-revved the sound gets stuck in high rev while the
speed catches up"** and **"if I hit something at high speed the sound
doesn't adjust"** are one bug: the rise and fall limits were the wrong
way round.  The game's own trace says the parameter RISES about 1 a
frame and FALLS up to 3 - after a crash from $3F it is back at $20
within ten frames, and after an over-revved launch it drops from $3F to
$25 in fifteen.  The port had rise 3 / fall 1, so a note that went up
stayed up.  Now 0.8 up and 3 down, refitted (target = speed * 0.079 - 6).

And through the COUNTDOWN the port no longer approximates at all: it
already keeps the game's own rev - smk_player_rev transcribes $80:95BB
and maintains $C2, the very accumulator the sound parameter is taken
from (NOTES 163) - so the engine now reads its high byte and inherits
the real thing, oscillation and all.

**"Turbo boost on start doesn't have the mushroom sound"** - because the
sound was hung on the mushroom ITEM rather than on the drive state.
$48 now plays whenever drive becomes $10, which is the mushroom, the
boost pads AND the turbo start: one hook, and it matches what the user
called it by ear ("floor or mushroom boost").

**"The coin sound is the right one, although it sounds three times"** -
the render was running past the effect.  The coin is ONE sample struck
twice: $0D at $17AD, then three frames later at $1F96, a fourth up, with
a long decay.  A THIRD strike is the music taking the sample back, so
the renderer now stops there; the coin is 0.77 s, low then high, once.

**"The item roulette doesn't have a sound"** - it did, 65 frames late.
$0D70 is $A000 while the roulette turns, and MEASURED across ten spins
in the recording (32 to 206 frames long, all different), the game plays
$55 on the exact frame that bit clears - the moment it lands on an item.
The port was playing it when the item became USABLE ($4000, 65 frames
later).  Hitting the box itself is silent, which is worth knowing too.
