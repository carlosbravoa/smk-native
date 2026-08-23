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

*(next entry: 011)*
