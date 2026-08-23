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

*(next entry: 020)*
