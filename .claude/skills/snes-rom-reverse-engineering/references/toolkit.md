# Toolkit shape

Modules worth having, in dependency order. Each is small; the value is in
the separation, because you will rewrite the tracer several times and want
everything else to stay put.

```
rom.py       header parse & scoring, mapping (snes<->pc), mirror
             canonicalisation, checksum compute/verify/fix, dump hashes
opcodes.py   the 256-entry table, operand sizes, flow classification
symbols.py   plain-text annotation database (labels, entries, flag hints,
             data regions, jump tables), loaded from a directory of files
disasm.py    the tracer: worklist, per-path (M,X,php-stack), context-sensitive
             call summaries, provenance trail, `explain(addr)`
listing.py   assembler-syntax output; a "raw" mode with no labels or comments
             for round-trip verification
tables.py    jump-table discovery, code plausibility, health metrics
compress.py  decoder, validity scan (size only, no output), encoder
gfx.py       tile decode (planar 2/4/8bpp, Mode 7 linear), BGR555, PNG writer
             with no dependencies, format scoring
assets.py    pointer tables, free-space pool, repack (in place or relocate),
             ROM expansion
```

Commands to expose:

```
info verify        cartridge identity, header, vectors
trace health       coverage and desync metrics
dis                annotated listing
jumptables         discovery, iterated to a fixpoint
hex                dump at a SNES address or file offset
assets list/export compressed asset inventory
gfx --identify     score tile formats; render to PNG
freespace expand   space accounting and image growth
checksum           verify / fix
```

Make targets: `build verify test roundtrip trace dis jumptables extract`.

## The tests that matter

1. Base ROM hash is the expected dump.
2. `pc_to_snes(snes_to_pc(x)) == x` across the image.
3. Reset vector points at a plausible boot sequence.
4. Trace produces a substantial body of code with junk under ~0.5%.
5. **Every traced instruction reassembles byte-identically.**
6. Every referenced asset decodes; `compress`/`decompress` round-trips.
7. Blob adjacency holds for a good fraction of assets.
8. Palettes are the right size and valid BGR555.
9. **A build with no patches reproduces the base ROM byte-for-byte.**
10. Edit an asset → rebuild → read it back → the edit is there and nothing
    else changed.
