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

*(next entry: 008)*
