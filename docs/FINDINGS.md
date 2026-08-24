# Super Mario Kart (USA) — reverse engineering notes

Addresses and structure only. No game data is reproduced here.

Everything below was derived by tracing the ROM's own control flow. Claims
are marked **verified** where a test in `tools/test.py` checks them, and
**observed** where the evidence is strong but not mechanically asserted.

## Cartridge

| | |
|---|---|
| mapper | HiROM, FastROM (`mapmode $31`) — **verified** |
| size | 512 KB, headerless |
| cart type | `$05` (ROM + SRAM + battery), 2 KB SRAM |
| coprocessor | DSP-1 (Mode 7 maths) |
| checksum | `$EB44` / `$14BB` — **verified** |
| sha1 | `47e103d8398cf5b7cbb42b95df3a3c270691163b` |

Code lives in the `$8000-$FFFF` halves of banks `$80-$87`; the `$0000-$7FFF`
halves (reachable as `$C0-$C7`) are almost entirely data. The game calls
into several mirror aliases of the same bytes — `$08`, `$80` and `$C0` forms
all appear — so addresses must be canonicalised.

## Entry points

| address | role |
|---|---|
| `$80FF70` | reset — `SEI / REP #$09 / XCE / SEP #$30`, stack to `$1FFF`, `$420D`=1 (FastROM), `JML $80803A` |
| `$80803A` | boot init: clears `$4200`/`$420B`/`$420C`, `INIDISP`=`$8F`, `JSL $81E000`, then the main loop |
| `$808000` | NMI handler |
| `$80801F` | IRQ handler |
| `$808056` | main loop |

### Main loop

```
$808056  jsl $81E067            ; per-frame work
$80805A  stz $44                ; clear the vblank flag
$80805C  lda $44 / beq $80805C  ; spin until NMI sets it
$808060  ldx $36                ; game mode * 2
$808062  jsr ($8197,x)          ; mode handler
$808065  bra $808056
```

The NMI handler increments `$34`, calls `$80B181`, then dispatches through a
second table with the same index.

## Dispatch tables — **verified**

| table | entries | indexed by | called with |
|---|---|---|---|
| `$808197` | 15 | `$36` (mode × 2) | M=0 X=0 |
| `$8081BF` | 15 | `$36` | M=0 X=0 |
| `$808B12` | 6 | `$D0` (index × 2) | M=1 X=1 |

Entry 13 of both mode tables repeats entry 0. Index 15 and beyond is
unrelated data, which is how the count was established. A further ~112 tables
were found automatically; see `symbols/10_jumptables.sym`, which records the
confidence of each.

## Direct page

| address | meaning |
|---|---|
| `$34` | frame counter, incremented every NMI (16-bit) |
| `$36` | game mode × 2 |
| `$44` | vblank flag — set by NMI, spun on by the main loop |
| `$D0` | IRQ handler index × 2 |

## Graphics compression — **verified**

Two decompressors, byte-identical apart from the destination WRAM bank:

| routine | output |
|---|---|
| `$84E09E` | bank `$7F` |
| `$84DF38` | bank `$7E` |

Both are entered with `Y` = source address, `A` = source bank, `X` =
destination offset, and use direct page `$0E` (output cursor), `$10` (source
cursor), `$00` (output origin), `$14` (count), `$16` (invert flag).

### Stream format

```
byte0 == $FF                 end of stream
(byte0 & $E0) != $E0         cmd = byte0 >> 5,       len = (byte0 & $1F) + 1
(byte0 & $E0) == $E0         cmd = (byte0 >> 2) & 7, len = (((byte0 & 3) << 8) | byte1) + 1
```

| cmd | meaning |
|---|---|
| 0 | literal — copy `len` bytes from the stream |
| 1 | byte fill |
| 2 | word fill (two bytes alternating) |
| 3 | incrementing fill |
| 4 | back-reference, 16-bit offset from the output origin |
| 5 | as 4, every byte `EOR $FF` |
| 6 | back-reference, 8-bit distance back from the cursor |
| 7 | as 6, every byte `EOR $FF` |

Back-references copy one byte at a time, so a distance shorter than the
length repeats. Command 7 cannot occur in a short header, because `$E0` is
the long-header escape.

### Evidence

- 69 referenced assets decode without error.
- 49 of them begin exactly at the previous asset's `start + consumed`, which
  independently validates the consumed-length calculation.
- An independent encoder round-trips all 69 losslessly, and produces a
  smaller stream than the original for every one of them (94.1% overall).

## Asset pointer tables

Three bytes per entry: 16-bit address, then bank. Indexed by `index * 3`
(computed as `n*2 + n`).

| table | entries | contents |
|---|---|---|
| `$81EB5B` | 24 | large graphics, 1.5–7.6 KB each |
| `$81EBA3` | 8 | graphics, 1.5–6.4 KB each |
| `$81EBBB` | 8 | **256-colour BGR555 palettes**, 512 bytes each — verified |
| `$81EBEB` | 8 | small graphics, 0.3–1.8 KB each |
| `$81EC03` | 8 | graphics, 1536 bytes each |
| `$81F47E` | 14 | graphics, 0.5–1.4 KB each |

Table boundaries come from the code that indexes them; counts are how many
entries still resolve to a decodable stream. The palette table is confirmed
by content: all 8 × 256 colour words have bit 15 clear, as BGR555 requires.

## Free space

About 18.6 KB of `$00`/`$FF` filler in 243 runs of 64 bytes or more; the
largest single run is 726 bytes at `$C7602B`. This is a very full cartridge —
anything substantial needs `smk expand`, which grows the image to 1 MB or
2 MB and fixes the header size byte.

## Mode 7 track pipeline — **verified**

This is what the native renderer runs on.  Three assets per track, uploaded by
two DMA routines whose sizes gave the whole structure away:

| routine | size | destination | meaning |
|---|---|---|---|
| `$81E7B5` | `$4000` | VRAM **low** bytes from `$7F:0000` | 128×128 tilemap |
| `$81E769` | `$3000` | VRAM **high** bytes from `$7F:4000` | 192 Mode 7 tiles |

### Tilemaps are compressed twice

`$81E745` is the loader, and the double decompression is why a single ROM-wide
scan finds almost no 16384-byte streams:

```
jsr $E64A          ; x = track * 3
lda.l $81EB5B,x    ; -> Y  source address
lda.l $81EB5D,x    ; -> A  source bank
ldx #$C000
jsl $84E09E        ; ROM      -> $7F:C000   (still compressed)
ldy #$C000 / lda #$007F / ldx #$0000
jsl $84E09E        ; $7F:C000 -> $7F:0000   (the actual 16384-byte map)
```

Table `$81EB5B` has **24 entries** — 20 GP courses plus 4 battle courses — and
all 24 decode to exactly 16384 bytes.

### Tiles are 4bpp packed with a per-tile palette base

The expander at `$84E3C7` turns the blob from table `$81EBA3` into Mode 7's
linear 8bpp tiles:

```
+$000   one palette-base byte per tile (256 bytes)
+$100   32 bytes per tile: two 4-bit pixels per byte, LOW nibble first
```

Each nibble becomes one 8-bit pixel; a **non-zero** nibble is OR-ed with that
tile's palette base, which is how a 16-colour tile reaches anywhere in the
256-colour Mode 7 palette. Zero is the backdrop index and is left alone.
`0x100 + 192*32 = 6400` — exactly the size of the tileset blob.

## Game modes

The main loop dispatches on `$36` (mode × 2). Mode changes go through a
**pending-mode** variable `$32`: `$81E09A` copies it into `$36`, clears it,
and re-enables NMI/IRQ. Writing `$32` runs the game's own setup; writing
`$36` skips it.

| mode | handler | what it is |
|---|---|---|
| 0 | `$808096` | bare `rts` — idle |
| 1 | `$808067` | the attract-mode demo race |
| 2 | `$8080BA` | title screen (~28 s, then advances) |
| 3 | `$8080CA` | a menu screen (bank `$85` routine chain) |
| 6 | `$808136` | the played race |
| 13 | — | boot / initial mode, set by `$81E02D` |

## Kart state

Eight karts at WRAM **`$1000`, stride `$100`**. `$B4` holds the base of the
kart currently being processed, so every `$18,x`-style field is relative to
it. Fields established so far:

| offset | meaning |
|---|---|
| `$10` | flags; bit 0 = off the map |
| `$16` / `$18` | X position, 16.16 (fraction / integer, 0..1023 px) |
| `$1A` / `$1C` | Y position, 16.16 |
| `$1E` / `$20` | Z position |
| `$22` / `$24` | velocity X / Y, 8.8 px per frame |
| `$2A` | heading, 65536 = one turn, 0 = −Y, clockwise |
| `$42` | collision state |
| `$58` | current tilemap index |
| `$68` | current surface byte |
| `$A2` | current steering angle |
| `$AC` | **state index** into the jump table at `$80AD76` |
| `$C0` | character / stat index |
| `$E8` / `$EA` | speed, 32-bit; `$EA` is the 8.8 value |
| `$EC` / `$EE` | acceleration, 32-bit |
| `$FA` | target steering angle |

## Motion — **verified**

```
speed32 += accel32                      $80A4E1, clamped at zero
(vx, vy) = DSP1_sincos(angle, speed)    $80A4F6 - sin, then -cos
position32 += velocity << 8             $80879D
```

Checked against the running game: the position step is exact over hundreds
of frames, and it settles the update order — velocity is computed *before*
position is integrated.

Acceleration is a table lookup on current speed (`$80A7E1`) toward a target
speed (`$80B074`); deceleration is a four-entry table at `$80B064`. Both
tables live in WRAM at `$0690` and `$06B0`, filled by **`$81FEB6`** from a
ROM source: `$81FED5` holds one pointer per engine class to a **64-byte**
table, each byte widened to a word by `<< 4`. Three classes, 64 bytes apart.

Kart states, dispatched by `$80AD6F` through `$80AD76[$AC,x]`: state 0
drives, 1 coasts, 10 brakes, 4 is the boost/brake check.

## DSP-1

Used constantly, at DR `$6000` / SR `$7000`. Commands actually issued by
this game (measured with a fully-synced stream, NOTES 039):

| cmd | use | traffic |
|---|---|---|
| `$00` multiply | physics | heavy |
| `$04` sin/cos | velocity from heading | heavy |
| `$06` project | world point → screen H, V, size — the kart sprites | heaviest (~14/frame) |
| `$02` projection setup | camera per viewport, 2×/frame | heavy |
| `$0A` raster | per-scanline Mode 7 matrix, 96 lines per split-screen half | boot/menus |
| `$28` distance | proximity checks | light |
| `$01`, `$0B`, `$10` | transitions | rare |

Protocol facts worth keeping: commands are single-byte stores, parameters
16-bit words LSB-first; raster is a **streaming mode** (one Vs, then a
4-word group per scanline auto-advancing as read, ended by `$8000` sentinel
words); and `$80` is written 128× at boot by a flush loop at `$81E3EC`.

**The camera model** (from `$02`'s own parameters): `F` is the focal point
on the ground — the player's kart, in quarter-pixel units — with the eye
`Lfe` away at elevation `Azs` behind azimuth `Aas`, and the screen `Les`
from the eye. Race values (Lfe=Les=256, Azs=73°) put the eye 18.5 px behind
and 61 px above the kart. Forward is `(sin Aas, −cos Aas)` — the same
0 = −Y convention as kart headings.

The sin/cos scaling is pinned by unit analysis — `radius * sin` unshifted —
and its result order (sin then cos) is confirmed three ways. `$81F638` is
an **atan2** helper used by the AI.

## Kart sprites

Uncompressed 4bpp, in PPU order: a 32×32 sprite is 4×4 tiles with a
**16-tile row stride**, frames advancing 4 tiles across then 64 down. Each
frame is 512 bytes. The sheet holds **three size tiers** of ~11 rotation
steps (a distance LOD).

Seven sheets at `$C0`–`$C6`:`$2000` cover eight drivers — Mario and Luigi
share one and differ only by palette. In OAM a kart is four 16×16 sprites
with its own tile slot, four tiles apart.

Sprite palettes come from the same 256-colour blob as the track, and are
**theme-dependent**: 37–53 of the 256 bytes change between themes, so
drivers are re-tinted per course.

## Other

* **Text**: `letter = byte − $0A` with `A = 0`, `$FF` terminates. String
  table at `$81DC7F`.
* **SRAM** at `$30:$6600`: six 3-byte BCD best times per track, 20 bytes per
  track; `$81DB94` initialises them to `$99 $59 $0A` (9'59"0A).
* **Cup order**: `$81EC1B`, 20 bytes, indexed `cup*5 + course`.

## Not yet established

- The pixel layout of each graphics blob class. `smk gfx --identify` scores
  the candidates, and palettes decode correctly, but the per-asset tile
  format has not been confirmed against the game's own upload code for every
  table. Treat rendered PNGs as provisional.
- **Lap counting and checkpoints.** No per-track checkpoint data located.
- **Per-course start lines.** The grid at (952,756) stepping down by 24 is
  real and verified on track 7, but lands on solid ground for 5 of 24
  courses, so those place their karts some other way.
- **The sprite frame-selection rule** — which frame is shown for a given
  heading. Recoverable from OAM tile numbers during a race; not yet read.
- **The game's character table** binding a driver to a sheet and palette.
- **Item behaviour and the AI racing lines.** The AI's steering is known in
  shape (`$80B0B1` steers toward a waypoint from `$0900`/`$0A00` via atan2),
  but the per-track waypoint data has not been located.
- **Audio.** The SPC700 is not emulated; `smk spc` dumps the driver the game
  uploads, which is the intended route to pre-rendered music.
- The audio engine, object/kart behaviour tables, and text encoding.
- 8% of the ROM is traced as code. The remainder is a mix of data and code
  reachable only through dispatch paths not yet resolved (52 indirect sites
  remain unresolved).
