---
name: snes-rom-reverse-engineering
description: Reverse engineer a SNES/65816 ROM, either to rebuild a patched ROM or to reimplement the game natively on SDL2 with no emulator. Covers cartridge mapping and mirrors, a tracing disassembler with context-sensitive M/X flag propagation, jump-table discovery (the thing that actually gates coverage), byte-exact round-trip verification with asar, finding and proving a graphics decompressor, locating assets by their DMA upload size, Mode 7 tilemaps and tile expansion, palettes, and a native port that reads the user's own ROM at runtime so no game data is ever redistributed. Use when starting or continuing a SNES romhack, disassembly, or native port, when a 65816 disassembly desynchronises, when hunting a game's compression or asset format, or when building an SDL host for a console game.
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

## Step 9: find assets by the size of their DMA

This is the highest-yield trick for a console game and it is easy to miss.

A console has to *upload* every asset, and the upload records the asset's
exact shape: the destination register says what kind of thing it is, and the
transfer length says how big. Scan for the DMA size register write and read
back the immediate that precedes it:

```
grep for `sta $4305` (DAS0L); look back a few bytes for `lda #imm16`
```

On Super Mario Kart, 36 such sites produced exactly two large ones, and both
were the answer:

| size | B-bus target | meaning |
|---|---|---|
| `$4000` (16384) | `$18` VMDATAL, **low bytes only** | the 128×128 Mode 7 tilemap |
| `$3000` (12288) | `$19` VMDATAH, **high bytes only** | 192 Mode 7 tiles |

Mode 7 VRAM interleaves the tilemap into the low byte of each word and tile
pixels into the high byte, so a DMA writing *only* one half of each word is a
positive identification, not a guess. From those two routines, one xref hop
reached the loader and the per-track pointer table.

Read the DMA setup fully — `DMAP` (transfer pattern), `BBAD` (destination
register), `A1T`/`A1B` (source), `DAS` (length), `VMAIN` (increment mode). It
is a complete, machine-checkable description of an asset.

## Step 10: expect assets to be compressed more than once

A ROM-wide scan for streams decompressing to the tilemap size found *one*
candidate in the whole 512 KB, and it was not a tilemap. The reason:

```
ldx #$C000
jsl Decompress        ; ROM      -> $7F:C000   (still compressed!)
ldy #$C000 / lda #$007F / ldx #$0000
jsl Decompress        ; $7F:C000 -> $7F:0000   (now the real 16384 bytes)
```

The same decompressor, called twice, the second time with WRAM as its source.
If a scan for "streams of the size you expect" comes up empty while the
loader clearly produces that size, **try decompressing the output again**
before doubting the codec.

## Step 11: tiles are usually packed tighter than the hardware format

Hardware wants Mode 7 tiles as linear 8bpp, 64 bytes each. Almost no game
stores them that way — it would be twice the size. Look for an expander
between the decompressor and the upload buffer.

The Super Mario Kart one is worth knowing because the shape is common:

```
+$000   one palette-base byte per tile
+$100   32 bytes per tile: two 4-bit pixels per byte, LOW nibble first

pixel = nibble ? (nibble | palette_base[tile]) : 0
```

Two details decide whether the output is right:

- **Zero is never remapped.** It is the backdrop/transparent index; OR-ing the
  base into it shifts the whole image into the wrong palette band.
- **Low nibble first.** Getting the order wrong mirrors every tile
  horizontally in 2-pixel pairs, which reads as "slightly wrong texture"
  rather than obviously broken.

`0x100 + tiles*32` should exactly equal the decompressed blob size. That
equation is your confirmation.

## Step 12: reimplementing natively

### The ROM is a runtime dependency, not a build input

Ship code that **loads the user's own ROM at startup** and decompresses in
process. This is the only distributable arrangement, and it is also the best
engineering: there is no intermediate asset file to drift out of date, and
the game exercises your decoders every run. Verify the dump on load and say
plainly what is expected when it does not match.

### Get the tick rate right before anything else

SNES NTSC vblank is **60.0988 Hz** (PAL 50.007). If the game's main loop
spins on a flag set by NMI — most do — then that is exactly one simulation
step per frame, and every duration in the game is a count of those steps.
Run a fixed timestep at that rate with an accumulator; do not tie simulation
to the host's refresh.

### Mode 7 without the PPU

The SNES builds perspective by rewriting the M7A–M7D affine matrix every
scanline via HDMA. Natively you do not need the matrix at all — compute the
ground plane directly, and you get resolution independence for free:

```
z       = height * focal / (screen_y - horizon)     // ground distance
centre  = camera + forward * z
step    = right * (z / focal)                       // world units per screen pixel
sample along the row from centre - step * (w/2)
```

That is the entire renderer. It is scale-invariant, so the same camera
parameters look right at 256×224 and at 4K, and a software implementation
still runs at ~100 fps at 1080p single-threaded. Keep a `--pixel N` divisor so
the chunky original look is still available.

### Two implementations that check each other

Keep the exploratory toolkit (a scripting language, fast to iterate) *and* the
runtime (C, what ships), implement the codec and asset layout in both, and
test both. They cannot silently drift, and a disagreement is a real bug
found for free. The C side gets a headless self-test that loads a real ROM
and asserts the same facts.

### Look at the output constantly

Decode errors are far more obvious to an eye than to an assertion. Render a
**contact sheet of every track / level / sprite** at once — all 24 Super Mario
Kart courses in one image immediately confirmed the tilemap, the tile
expander and the palette in a single glance, and would have shown a wrong
palette base or a mirrored tile just as fast. Automate it; run it after every
format change.

Corollary: greyscale-by-index is not a substitute. Get the real palette first
(§7) or you will misjudge correct output as noise.

### Be explicit about what is not the original

A native port accumulates behaviour the ROM never had: a camera height, a
steering feel, a start position. Every one of those must be **named as a
placeholder in a comment**, with a note on what would replace it. Otherwise
six months later it is indistinguishable from a decoded fact — which is the
prime directive in §0 turned inside out.

## Step 13: when static reading runs out, build a minimal machine

Static disassembly has a **structural ceiling**, not an incidental one. Count
the dispatches whose target is already in a register:

```
jmp ($0000,x)    177 sites
jsr ($0000,x)     81 sites
```

Each takes its pointer from a state-machine record, so no amount of reading
resolves them. Past that point, behaviour has to be **observed**.

The good news is that the machine you need is small, because you are not
rendering anything. In rough order of what unblocks what:

1. **APU handshake.** The 65816 blocks on the sound CPU long before anything
   interesting happens, and it is pure control flow — no audio required. The
   IPL protocol is fixed hardware behaviour and can be reimplemented exactly.
   Three details cost a day between them:
   - the byte written to port 1 *before* the `$CC` kick is **the first data
     byte**, doubling as the "data follows" flag, so it must be non-zero;
   - the end-of-block test belongs at **block boundaries**, not per byte —
     port 1 carries data during a transfer and is frequently zero;
   - the final block's **echo must survive**: the CPU is still waiting to
     read back the value it wrote, so advertising "ready" immediately
     destroys the reply it is spinning on.
   Afterwards the game talks to *its own* driver. Echo those commands — a
   race start is sequenced against the sound driver, and refusing to
   acknowledge leaves the game waiting forever.
2. **NMI**, dispatched from the main loop's vblank spin. One simulation step
   per vblank is the game's own pacing.
3. **IRQ and a scanline counter.** If `NMITIMEN` has bits 4-5 set the game
   expects H/V IRQ, and anything sequenced from it silently never happens.
4. **`$4212` bit 6 — HBlank.** Games spin on `bit $4212 / beq`. With no dot
   counter, alternate the flag on each read; every such wait then terminates
   in a couple of iterations, which is all the game wants from it. Missing
   this is a two-instruction infinite loop that looks like a crash.
5. **DMA, VRAM, CGRAM, OAM.** Not to draw — so that asset formats can be read
   *out of the machine* rather than inferred (§15).

Cost: a few hundred lines on top of the CPU. Payoff: every remaining
question becomes measurable.

## Step 14: the two techniques that find everything

**Instrument writes to find who owns a field.** This was the single most
productive move. Watch a memory address while the game runs and record the
PC of whatever writes it:

```
watch $0690..$06CF during race setup   -> exactly one writer, $81FEB6
watch the kart's acceleration field    -> $80B048, and its target, $80B074
watch the steering angle               -> $80AFCE, fed from $80AD68
```

One query each, no searching. It also finds things a static trace covering
8% of the ROM will never reach.

**Change state directly instead of navigating to it.** Games have a
*pending-mode* variable that a transition routine copies into the live one
(`lda $32 / sta $36 / stz $32`, then re-enable interrupts). Writing the
pending mode runs the game's own setup; writing the live mode skips it and
gives a half-initialised state. Find that routine early — it turns "get
through four menus" into one poke.

Know the limits of the shortcut, though: a forced mode can leave a state
that runs its simulation but never renders, or renders but never starts.
Check that the thing you actually want is happening before trusting numbers
taken from it.

## Step 15: verify against the running game, not against expectations

Once the machine runs, stop arguing about behaviour and diff it.

Reimplementing the position integration, two candidate readings differed
only in *which frame's velocity applied*:

| prediction | result |
|---|---|
| using the earlier frame's velocity | 190 exact, 288 differ |
| using the later frame's velocity | **478 exact, 0 differ, error exactly 0** |

That settles the arithmetic *and* the update order inside a frame — which
matters, because order decides observable behaviour. No amount of reading
the disassembly would have been as convincing.

The same trick verifies asset pipelines end to end: run the game, then
compare your extraction against what it actually put in VRAM. Ours came
back 12288/12288 identical on tiles and 16306/16384 on the tilemap — and the
0.5% gap was real, the game edits its own tilemap when item boxes are taken.

That comparison also caught a bug in the *harness*: VRAM held a different
track than the one requested, because the track index was being written to a
variable the forced entry ignored. Every measurement taken through that
harness was on the wrong course. **Check what the machine actually loaded,
not what you asked it to load.**

## Step 16: find streamed assets by their DMA source

Assets uploaded once are found by their DMA size (§9). Assets streamed
*per frame* are found by their DMA **source address**:

```
128-byte transfers from banks $C0/$C2/$C4/$C5, addresses $200 apart
    -> a frame is 512 bytes = 16 tiles = 32x32 pixels
    -> $200 spacing means frames are contiguous
    -> different banks mean one sheet per character
```

That is the sprite format handed to you without decoding a single
instruction. And because the game re-uploads the chosen frame every frame,
the source address *is* the frame it picked — so a selection rule that would
otherwise need a decode becomes a measurement, provided you can reach a
state where the thing is actually being drawn.

Sprite layout gotcha: console sprite sheets are usually stored in **PPU
order**, not as a picture. A 32x32 sprite is 4x4 tiles with a **16-tile row
stride**, so rendering the raw data 16 tiles wide shows clean sprites while
"row-major within each frame" shows vertical shredding. Try the hardware
layout first.

## Step 17: sanity-check every observed value

Values read out of a running game feel authoritative. They are not, if the
state you forced is not the state the game normally reaches.

A forced race gave every course the same starting grid — a tidy result that
looked like "the grid is fixed in world space". Checking those coordinates
against each course's own surface table put **5 of 24 starts inside solid
geometry**. The grid was real for the course actually loaded and a leftover
default for the rest.

The check took one query and cost nothing. Whenever a measurement comes from
a state you constructed rather than one the game walked into, ask what it
would look like if it were wrong, and test that.

## Step 18: reaching the state you need to observe

Getting the machine to run is only half of it; you then have to get the game
*into the situation you want to measure*, and that is its own problem.

**Attract sequences have their own pacing.** A wait that feels generous in
wall-clock terms can be short in game time when the simulation runs at a
fraction of real speed. One title screen held for 28 seconds of game time —
about 1700 frames — before anything else happened, and a demo race then held
its karts on the grid for hundreds more.

**Watch all the actors, not the first one.** A demo race looked frozen for
several runs because only karts 0 and 1 were sampled; karts 2 and 3 were the
first to move. Poll the aggregate ("has *anything* started moving?"), not a
representative.

**A forced state is not the state.** Writing the pending-mode variable gets
you into a race in seconds, but that race ran its physics and never drew the
karts, while the demo race drew everything and ran no physics. Two halves,
neither sufficient. Before drawing conclusions from a state you constructed,
confirm the specific thing you care about is actually happening in it.

**An absence constrains where to look; it does not say what is wrong.** "The
kart update never executes" was read as "the karts are parked in an idle
state", and the one-line test of that — force the state index — showed the
index was already correct. The dispatcher simply was not being reached. Cost:
one wrong entry in the log. Test the cheap consequence of an inference before
building on it.

## Step 19: know what your model cannot see

An oracle that runs the game is only as honest as its hardware coverage,
and the gaps are invisible until they bite. Three that cost real time here:

**HDMA.** Anything a Mode 7 game does per-scanline — the matrix, the
horizon, gradient skies — arrives by HDMA, not by CPU writes. Before it was
modelled, a search for the camera angle came back empty four different
ways: not in the coprocessor's parameters, not in any RAM global at any
lag, not in the trigonometry call stream, and only one frame in nine
hundred wrote the matrix registers directly. Every one of those negatives
was true and none of them was the answer. **When a value must exist and
four searches miss it, suspect the model, not the value.**

**A command that only runs in the mode you did not capture.** The
coprocessor's raster command is issued 129 times during boot and *never*
during a race — the in-race matrix is built on the CPU instead. Sampling
only gameplay would have left it looking dead; sampling only boot would
have made it look central.

**State that only exists in a state.** Injecting a height value and running
frames showed gravity doing nothing, because gravity only runs while an
airborne flag is set. The fix is to reproduce the game's own entry
conditions — set the state the way its own launch routine does — and then
measure. "I set the variable and nothing happened" usually means you set
one of several variables the behaviour needs.

The general rule: write down what your machine models and what it does not,
and re-read that list whenever a measurement comes back empty.

## Step 20: a unique instruction is a free identification

Grep the whole image for the constant you think a rule uses. If there is
exactly one site — `sbc #$001A` appeared once in a 512 KB ROM — the
identification needs no further argument, and the surrounding six
instructions are the whole rule. This is the cheapest high-confidence move
available and it costs one command.

The same trick works for structure: three consecutive 16-bit copies of
`$16/$18`, `$1A/$1C`, `$1E/$20` between two blocks identify a position
triple without reading a single line of the code that uses it.

## Step 21: a measured constant may be a derived quantity

A wall bounce measured as "about eight frames of knockback" was recorded as
a constant, ported, and worked. It was wrong. The collision routine sets a
*vertical velocity*, and eight frames is simply how long that velocity
takes to fall back to the ground under the game's gravity. The constant was
an artefact of measuring one instance of something dynamic.

Ask of every constant you measure: *what would make this value change?* If
you cannot answer, you have probably found a symptom rather than a rule.
Constants that fall out of an already-decoded law are trustworthy;
constants that stand alone deserve one more experiment at a different
input.

## Step 22: your verification harness can pass for the wrong reason

The strongest-sounding result of this project — "478 predictions, 0
mismatches, the ported kinematics reproduce the game exactly" — was
produced by a harness that never once sampled a kart. It forced the race
mode variable and reached a static scene where every kart sits at speed 0;
it then chose its subject from the game's current-object pointer, which
names whatever the engine was processing that instant; and it settled a
count of frames that landed inside a scripted countdown. The object it
happened to follow obeyed the same integration rule, so the check passed —
across many sessions — until an unrelated fix changed the scene and it
failed with a constant residue that unravelled all three faults at once.

Rules that fall out of this:

- **A harness has preconditions; make it prove them.** "The subject is a
  kart", "the kart is actually driving", "positions are integrated, not
  scripted" were all assumed. Now the check tests each frame for free
  motion (moving, grounded, velocity consistent with the object's own
  heading) and counts the rest as skipped.
- **Add an INCONCLUSIVE outcome.** A verification that can only say
  pass/fail will say one of them even when its preconditions collapsed.
  Refusing to answer ("no subject qualified in this window") is the honest
  third state, and it is what turns a silent lie into a visible gap.
- **Never key a measurement on the engine's own scratch pointers.** A
  register like `$B4` here is the current-`this` of an object loop; its
  value depends on when in the frame you sample it. Address your subjects
  absolutely.
- **When a long-green check suddenly fails after an unrelated change,
  bisect the change first, then audit the check.** The A/B (each fix
  disabled, then all together) took minutes and cleared the new code;
  everything after that pointed at the harness.

## Step 23: a scripted edit that doesn't assert is a lie waiting to ship

Two successive patches to the same function silently no-opped - each used a
string replacement whose pattern no longer matched the file, Python's
`str.replace` does nothing on a miss, the build stayed green (the OLD code
still compiled), the suites stayed green (they tested other things), and
two user-facing releases were announced with a fix they did not contain.
The failure was only caught by instrumentation that printed the live
values out of the running binary.

Rules: every scripted source edit asserts its pattern matched and asserts
the result is present afterward; and a claim about changed behaviour is
checked against the BUILT ARTIFACT's output, not against the editor having
run.  A test suite that passes proves only what it measures - if the
change is behavioural, print the behaviour.

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
10. For a native port: find the asset uploads by DMA size (§9), decode the
    asset formats (§10, §11), then build the SDL host around a correct tick
    rate and a directly-computed renderer (§12).
11. When static reading stops paying (§13), build the minimal machine —
    APU handshake, NMI, IRQ, HBlank, DMA/VRAM — and switch to instrumenting
    and diffing (§14-§17). Everything after the asset layer is easier to
    measure than to read.

## What not to do

- Do not linear-disassemble a 65816 ROM and try to patch up the desync.
- Do not broadcast subroutine exit flags to all call sites.
- Do not trust a `CMP #n` as a jump-table length.
- Do not chase coverage without watching junk and overlap counts.
- Do not judge a tile format by eye with a greyscale palette.
- Do not commit the ROM, extracted assets, or a build output.
- Do not claim a routine "is" something from its shape alone — say what the
  evidence is, and mark the rest as unverified.
- Do not bake extracted assets into the port. Read the user's ROM at runtime.
- Do not tie the simulation to the host refresh rate; use the console's.
- Do not let a placeholder constant lose its comment.
- Do not report absence from a partial trace ("nothing writes X") as a fact.
- Do not trust a measurement taken from a state you forced without checking
  the game actually reached the situation you think it did.
- Do not let two render paths exist. A feature added to one and missing from
  the other makes screenshots disagree with the program, twice if you do not
  fix the cause.
- Do not sweep an in-game measurement before rendering ONE frame and seeing
  the thing you measure actually appear. Three OAM sweeps in a row measured
  HUD churn, stale post-race state, and the rear-view mirror before a
  sprite-layer render exposed each mistake in seconds. Related: many SNES
  racers run a permanent split screen (view + rear-view/map); filter OAM by
  screen half, and remember the followed camera may not be the kart you
  think - and the world-forward sign is worth one render to verify.
- Do not test a primitive and call the FEATURE verified. Twice on SMK a
  library-level test passed for rounds while the caller was broken: the
  hop arc was pinned by a selftest that called the gravity routine
  directly, while nothing in the player's tick called it at all; and the
  wall bounce passed every library test while the player's own velocity
  update overwrote the rebound each frame. If two code paths drive the
  same primitive (a player and an AI, a game loop and a screenshot
  path), test the one the user actually runs, or make them share code so
  there is only one.
- Do not measure a CONTACT by putting the object inside the thing it is
  supposed to contact. Filling a map with the surface class under test
  puts the kart inside a solid, and an embedded object reads as
  "stopped, no travel, speed zero" whatever the real contact response
  is. It produced three confident wrong answers on SMK (a bogus rebound
  distance, "speed preserved on sustained contact", and a whole invented
  "dead stop" surface family) before a rig that painted obstacles into
  the path of a normally-moving object gave the real numbers. Measure a
  collision by arranging one, never by starting inside.
- Do not decode a coprocessor protocol from its data stream when the
  game's own READER routine is findable: SMK's DSP-1 raster protocol
  (one command, one Vs, then pure reads with auto-increment, $8000 to
  terminate) took four failed stream-parsing attempts and fell out of
  fifteen minutes of hand-decoding the game's reader at $81:F97D.  The
  reader IS the protocol spec.  Related: reference emulator source
  (snes9x dsp1.cpp) is the authority for coprocessor MATH - port the
  algorithm structure in floats and keep the chip's internal ROM tables
  (Nintendo data) out of the repo.
- Do not assume a per-track data list holds every entity kind. SMK's object
  list is stamped GROUND features only (boxes, coins, oil - even the
  kinds >= $C0, which stamp coin scatters); the solid obstacles live in a
  separate spawn system. A cross-check - live entity positions vs list
  records - settles it in one probe.
