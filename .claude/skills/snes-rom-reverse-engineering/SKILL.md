---
name: snes-rom-reverse-engineering
description: Reverse engineer a SNES/65816 ROM into a state where changes can be made and the ROM rebuilt reliably. Covers cartridge mapping and mirrors, a tracing disassembler with context-sensitive M/X flag propagation, jump-table discovery (the thing that actually gates coverage), byte-exact round-trip verification with asar, finding and proving a graphics decompressor, asset pointer tables, palettes, tile-format identification, and a base-ROM-plus-patches build that never redistributes game data. Use when starting or continuing a SNES romhack or disassembly, when a 65816 disassembly desynchronises, when hunting a game's compression format, or when setting up a reproducible ROM build.
---

# Reverse engineering a SNES ROM to a rebuildable state

The goal is not "read the code once". It is a **loop**: change something,
rebuild, verify. Everything below exists to make that loop trustworthy.

Built from a full Super Mario Kart (USA) teardown. The techniques are
general 65816/SNES; the specifics quoted as examples are from that game.

## Ground rules on game data

Ship **tools and addresses, never bytes**. Addresses, structure
descriptions, and your own code are your work. The ROM, and anything
extracted from it (graphics, music, text, tables of game data), are not.

- The user supplies their own ROM; the build applies patches on top of it.
- `.gitignore` the ROM, the build output, and the extraction directory,
  from the very first commit — not after the first accidental `git add -A`.
- Record a **hash** of the expected dump, not the dump. A hash is not the work.
- Extracted PNGs/binaries are derivative works. Keep them out of the repo.

## Step 1: identify the cartridge before anything else

Every later decision depends on the mapping being right, and it is cheap to
get wrong.

- The header is at file `$7FC0` (LoROM) or `$FFC0` (HiROM). **Score both**;
  do not assume. The decisive test is the checksum pair: `complement ^ checksum
  == $FFFF`, and the recomputed sum matches. A garbage title string at one
  offset and a clean one at the other settles it immediately.
- `mapmode & $0F`: `0` LoROM, `1` HiROM, `5` ExHiROM. Bit `$10` is FastROM.
- A file size of `N*1024 + 512` means a copier header — strip 512 bytes.
- Verify the reset vector points at plausible boot code. A SNES reset
  routine almost always starts `SEI / CLC / XCE` or `SEI / REP #$xx / XCE`,
  then `SEP #$30`, sets the stack, and writes `$420D` if FastROM.

**HiROM mapping** is uniform: `pc = ((bank & $3F) << 16 | addr) & (size-1)`,
valid for `$40-$7D`/`$C0-$FF` at any address and `$00-$3F`/`$80-$BF` at
`$8000-$FFFF`.

### Mirrors will duplicate your disassembly

A 512 KB HiROM answers to the same bytes at `$00`, `$08`, `$80`, `$88`,
`$C0`, `$C8`… Real games use several aliases in the same binary — SMK calls
`JSL $088AED` and `JSL $808AED`, which are the same instruction. **Canonicalise
every address** (`pc_to_snes(snes_to_pc(a))`) before storing it, or you
disassemble the same routine several times and your coverage numbers lie.

## Step 2: the M/X flag problem is the whole difficulty

On the 65816 the *length* of an immediate depends on runtime flags: `A9 58 00`
is `LDA #$0058` when M=0 and `LDA #$58` followed by a stray `00` when M=1.
One wrong flag desynchronises everything after it. A linear disassembly of a
65816 ROM is worthless.

So: **trace real control flow**, carrying `(M, X)` along each path.

- `REP #$20`/`SEP #$20` clear/set M; `$10` does X. `REP #$30` does both.
- `XCE` into native mode forces M=X=1.
- Model `PHP`/`PLP` with a small stack in the path state. The idiom
  `php / sep #$30 / … / plp` is everywhere, and treating `PLP` as "unknown"
  poisons long stretches of otherwise clean code.
- Follow both edges of a conditional branch; stop at `RTS/RTL/RTI/STP` and
  after unconditional `JMP/JML/BRA/BRL`.

### Subroutine returns: use context-sensitive summaries

This is the mistake that costs the most time.

The naive approach — on `RTS`, push every recorded call site with the flags
seen at that `RTS` — **cross-contaminates**. A three-instruction helper
called from both 8-bit and 16-bit code will hand 8-bit flags back to a
16-bit caller, and the resulting garbage looks like a plausible routine
hundreds of bytes away from the real bug.

In the SMK teardown this single defect produced 5154 flag conflicts and
13742 bogus instructions from one entry point.

Instead key everything on the **context** `(callee_entry, entry_M, entry_X)`:

```
summaries[ctx] : set of (M, X) observed at that context's RTS/RTL
callers[ctx]   : set of (return_addr, php_stack, caller_ctx, call_M, call_X)
```

Resume a call site only with exits produced by **that** entry context. Then
add a fixpoint fallback: a callee that never produced a summary (it tail-jumps
away, or is data) must still let its call sites continue — resume those with
the call-site flags and iterate until nothing new appears.

The same fix took that entry point to 33 conflicts and 28 bogus instructions.

### Build a provenance trail early

Add "how did the tracer reach this state" to the tracer from the start:
record `state -> (predecessor_state, reason)` on every push. When an
instruction decodes wrongly you want the answer in one query, not by bisecting
a 20 000-instruction trace. Every flag bug above was found this way.

## Step 3: jump tables are what actually gate coverage

Tracing from the vectors alone reached **0.5%** of SMK. Resolving three
dispatch tables took it to 8%. Games dispatch through
`JSR ($nnnn,x)` constantly, and the tracer cannot follow those.

Find the dispatchers first — they are usually two or three instructions after
the reset/NMI handler and index a mode variable:

```
lda $36            ; game mode
tax
jsr ($8197,x)      ; table at $PB:8197
```

`JMP/JSR ($nnnn,x)` fetches its pointer from the **program bank**, so the
table is at `$PB:nnnn`.

### Bounding a table automatically

Table length is not stored anywhere. A nearby `CMP #n` guard is tempting but
unreliable — in practice it produced counts like 801 and 424. Three
*structural* rules work far better, and agreement between them is your
confidence signal:

1. **Pointer plausibility** — an entry must map to ROM, and be `>= $8000`
   in a bank whose ROM lives in the upper half.
2. **A table cannot extend past its own lowest forward target.** If any entry
   points forward to `$F010` and the table starts at `$F000`, the table is at
   most 16 bytes. This alone nails the common case exactly.
3. **Entries cluster.** Real tables target a narrow address range; an entry
   more than ~$1000 outside the established span ends the table.

Then **linear-decode each candidate target** and reject it if the first ~48
instructions contain `BRK`, `COP`, `WDM` or `STP`. Shipped game code
essentially never contains these; a stale pointer trips one within a few
instructions.

Iterate to a fixpoint: newly reachable code exposes more dispatch sites.

### Health metrics, not vibes

Track these every run and watch the deltas, because coverage alone rewards
garbage:

- **junk opcodes** (`BRK`/`COP`/`WDM`/`STP`) as a fraction of instructions
- **overlapping instructions** — an instruction start inside another one is
  proof of desync
- **flag conflicts** — the same address decoded with two different `(M,X)`

Adding unvalidated tables to SMK raised coverage 4.05% → 8.75% while junk
went 4 → 286. With the structural rules it reached the same coverage with
junk at 32. *Coverage that raises junk is not coverage.*

## Step 4: prove the disassembler with a byte-exact round trip

This is the highest-value thing in the whole project and it is cheap.

Emit every decoded instruction as assembler source, assemble it back onto a
copy of the base ROM, and compare bytes. Zero mismatches means the
disassembler is not lying to you. Wire it to `make roundtrip` and run it
after every change to the tracer.

To make it work with **asar**:

- **Emit explicit width suffixes** — `lda.b`, `lda.w`, `lda.l`, and for
  immediates pick `.b`/`.w` from the operand size the tracer decided. Without
  this the assembler re-derives the width from the value and silently changes
  the encoding.
- **Relative branches must target a label.** asar treats a numeric operand on
  a branch as the *literal displacement byte*: `bmi $808097` assembles to
  `30 97`. Generate a label at every branch target. For targets that are not
  instruction starts, emit `org $addr` + a bare label at the end of the file —
  that defines the symbol without writing bytes.
- **Block moves take operands in written order.** Object code is
  `54 dstbank srcbank`; asar emits the two operands exactly as written, so
  write them in memory order. (This was the last of 1112 mismatches.)

## Step 5: find the decompressor by its call shape

Compressed assets are reached through a pointer table, and the call site is
distinctive — a small fixed preamble loading a source pointer and a
destination, then a `JSL`:

```
jsr ComputeIndex          ; index * 3
lda.l $81EBA3,x           ; -> Y : source address
tay
lda.l $81EBA5,x           ; -> A : source bank
and #$00FF
ldx #$C000                ; -> X : destination
jsl Decompress
```

Three bytes per entry (16-bit address + bank) is the giveaway for a
cross-bank asset table. Find the routine that everything with this shape
calls, and you have the decompressor.

Two decompressors that differ only in one byte per store are the *same*
algorithm writing to different WRAM banks — diff them before assuming
otherwise.

### Transcribe the command decoder exactly

Read the header parser first; it tells you the whole shape. A very common
family (SMK, and Nintendo LZ generally):

```
byte0 == $FF                 end of stream
(byte0 & $E0) != $E0         cmd = byte0 >> 5,        len = (byte0 & $1F) + 1
(byte0 & $E0) == $E0         cmd = (byte0 >> 2) & 7,  len = (((byte0 & 3) << 8) | byte1) + 1
```

with commands: literal, byte fill, word fill, incrementing fill, absolute
back-reference, absolute inverted, relative back-reference, relative inverted.
Back-references copy **byte at a time**, so overlapping runs repeat — games
rely on it, so implement it as a byte loop, not a slice copy.

Note the escape: because `$E0` is the long-header marker, command 7 is
unreachable from a short header.

### Prove the codec three ways

Do not trust "it produced plausible-looking bytes":

1. **Every referenced asset decodes** without running off the end.
2. **Adjacency.** Compressed blobs are packed back to back, so
   `start + consumed` should land exactly on the next blob's start. In SMK
   49 of 69 assets did. This validates your *consumed* count, which nothing
   else checks.
3. **Write an encoder and round-trip it.** `decompress(compress(x)) == x` for
   every asset. This is also what makes editing possible.

## Step 6: an encoder good enough to edit in place

If a re-encoded asset is larger than the original you must relocate it and
repoint the table — and a full commercial ROM has very little free space
(SMK: ~18 KB of filler in 512 KB). So the encoder's quality is a *practical*
constraint, not vanity.

Progression measured on SMK's 69 assets:

| encoder | fits in original slot |
|---|---|
| greedy, plain matches only | 41/69 |
| + inverted matches, lazy matching | 51/69 |
| + shortest-path DP parse | **69/69**, 94.1% of original size |

Two things mattered most:

- **Emit the inverted-copy commands.** Skipping them costs badly on tile data.
- **Parse by dynamic programming, not greedily.** `dp[i] = min cost to encode
  src[i:]`, transitions being the literal lengths and the best command of each
  type. Greedy takes a long match that strands the remainder; DP does not.
  Offer each command at both its full length and 32, because 32 is the last
  length that still fits a one-byte header.

Always assert the round trip *before* writing into the ROM. A compressor bug
that only manifests in-game is miserable to find.

## Step 7: identify tile formats by measurement

The ROM does not record a blob's pixel format — the routine that uploads it
knows. Do not guess repeatedly; score the candidates.

Decode the blob under each of `{mode7 linear 8bpp, 4bpp planar, 2bpp planar}`
× `{as-is, even bytes, odd bytes}` and measure **local coherence**: the mean
absolute step between neighbouring pixels. Real art is locally flat; a wrong
format shreds bytes across pixels and looks like noise.

**Normalise by the format's level count** (2bpp→3, 4bpp→15, 8bpp→255).
Without this, 2bpp wins every time — it only has four possible values, so its
raw differences are small no matter how wrong it is.

Two things that will mislead you:

- **Greyscale-by-index makes correct 8bpp art look like noise**, because
  palette indices are not ordered by brightness. Get a real palette before
  judging by eye.
- **Period-2 vertical striping means interleaved data.** SNES Mode 7 VRAM
  holds the tilemap in low bytes and pixels in high bytes; de-interleaving is
  often the answer.

Palettes are easy to confirm: SNES colour is BGR555, so **bit 15 of every
word is clear**. A 512-byte blob with 256/256 words having bit 15 clear is a
palette, not a coincidence.

## Step 8: the build loop

```
base ROM (user's own)  ──copy──▶  build/rom.sfc
                                    │
                       asar patches │  src/*.asm
                                    │
                      asset import  │  decompress → edit → recompress →
                                    │  fits? write in place : relocate + repoint
                                    │
                        fix checksum▼
                                  report changed byte ranges
```

Non-negotiables:

- **Never modify the base ROM.** Copy, then patch the copy.
- **Report every changed byte range** on each build. A patch that silently
  clobbers an unrelated table is otherwise invisible until the game breaks.
- **Recompute the checksum** at the end, after all patching.
- **A build with no patches must reproduce the base ROM byte-for-byte.**
  Make it a test. It catches the whole class of "the build itself corrupts
  something" bugs.
- Prefer asar's `freecode`/`freedata` over hardcoded offsets, and check
  free space (`$00`/`$FF` runs) before planning a large hack. If there is not
  enough, expand the image — HiROM addresses banks `$C0-$FF`, so 1 MB and
  2 MB both work; pad with `$00` and fix header byte `$D7` to `log2(size_KB)`.

### Keep annotations in a plain-text database

Labels, entry points, forced flag states, data regions and jump tables belong
in line-oriented text files, not in code — diffable, greppable, and
appendable by a discovery tool. Separate **hand-verified** from
**auto-discovered** so a bad automatic proposal can be dropped without losing
human work.

Give the tracer a `flags $addr M=0 X=1` directive. When one routine defeats
the flag inference, asserting the answer is a one-line fix, and the assertion
documents a real fact about the game.

## Order of work

1. Identify the ROM; get mapping and mirrors right. Add a hash check.
2. Stand up the build (copy + assemble + checksum) and prove a no-op build
   reproduces the base ROM. Do this **before** the interesting work.
3. Trace from the vectors. Expect ~0.5% coverage and do not be alarmed.
4. Find the main/NMI/IRQ dispatch tables by hand. Expect a 10-20x jump.
5. Add round-trip verification. Fix the assembler-syntax issues until it is
   zero mismatches.
6. Automate jump-table discovery; iterate to a fixpoint; watch the health
   metrics, not just coverage.
7. Follow the pointer-table call shape to the decompressor. Transcribe it,
   then prove it three ways.
8. Write the encoder. Now assets are editable.
9. Name things as you learn them; keep the symbol database growing.

## What not to do

- Do not linear-disassemble a 65816 ROM and try to patch up the desync.
- Do not broadcast subroutine exit flags to all call sites.
- Do not trust a `CMP #n` as a jump-table length.
- Do not chase coverage without watching junk and overlap counts.
- Do not judge a tile format by eye with a greyscale palette.
- Do not commit the ROM, extracted assets, or a build output.
- Do not claim a routine "is" something from its shape alone — say what the
  evidence is, and mark the rest as unverified.
