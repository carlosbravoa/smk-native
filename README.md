# Super Mario Kart — reverse engineering & rebuild toolkit

Tools for disassembling Super Mario Kart (SNES), understanding its data, and
rebuilding a modified ROM reliably.

**No game data is distributed here.** You supply your own ROM; the build
applies patches on top of it. See [`rom/README.md`](rom/README.md).

## Quick start

```bash
cp /path/to/your/smk.sfc rom/smk_usa.sfc
make verify        # is this the expected dump?
make tools         # build the vendored assembler (asar)
make test          # 25 checks: identity, disassembly, codec, build loop
make build         # -> build/smk.sfc
```

A build with no patches reproduces the base ROM byte-for-byte. That is
asserted by the test suite, so any difference you see later is your change.

## Making a change

**Code** — add an `.asm` file under `src/patches/` and include it from
`src/main.asm`:

```asm
org $80803C
    stz.w $4200          ; overwrite in place

freecode                 ; or let asar find room
MyRoutine:
    ...
    rtl
```

**Assets** — export, edit the decompressed bytes, drop them in
`assets/import/`, rebuild:

```bash
./tools/smk assets list                                  # what's there
./tools/smk assets export palette 0 -o assets/import/palette_0.bin
#   ... edit palette_0.bin ...
make build
```

Re-imported assets are re-compressed and written back in place when they fit,
or relocated into free space with the pointer table updated. Every build
prints exactly which byte ranges changed.

## Commands

```
make build        base ROM + patches + assets -> build/smk.sfc
make test         full regression suite
make roundtrip    prove every disassembled instruction reassembles exactly
make trace        coverage report
make dis          annotated listing -> build/smk.asm
make jumptables   discover dispatch tables, iterate to a fixpoint
make extract      export every known asset
make health       trace-quality metrics
```

```
./tools/smk info                    cartridge, header, vectors
./tools/smk hex $80803A -n 0x40     dump at a SNES address
./tools/smk dis -s $808000 -E $8080A0
./tools/smk gfx palette 0 --identify
./tools/smk freespace
./tools/smk expand --size 1m -o build/smk_1mb.sfc
```

## Layout

```
tools/smktool/   rom, opcodes, disasm, listing, symbols, tables,
                 compress, gfx, assets
symbols/         annotation database (00_core hand-verified,
                 10_jumptables auto-discovered)
src/             asar patches applied to the base ROM
assets/import/   decompressed assets to re-import at build time
docs/FINDINGS.md what the ROM turned out to contain
```

## What is established, and what isn't

The cartridge mapping, entry points, dispatch tables, the graphics
compression format and the palette table are verified by tests. Roughly 8%
of the ROM is traced as code, and every traced instruction reassembles
byte-identically. Track layout data and per-asset tile formats are **not**
settled — see the end of [`docs/FINDINGS.md`](docs/FINDINGS.md).

## Method

The general technique is written up as a skill in
[`.claude/skills/snes-rom-reverse-engineering/`](.claude/skills/snes-rom-reverse-engineering/SKILL.md).
