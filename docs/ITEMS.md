# The item system — decoded, measured, and how the port carries it

Status: **implemented** (2026-08-29); everything below is in the port unless marked OURS or OPEN. Everything marked
DECODED is read from the ROM at the address given; MEASURED is read off
the running game (the user's two recorded races via `itemlog.lua`, and
`tools/labs/itemfx.py` in the oracle); OURS is a choice this port makes,
ledgered in `docs/ROADMAP.md`. Numbers with `?` are still being measured.

## 1. Where it lives

| piece | address | role |
|---|---|---|
| item word | `$0D70,y` (y = player×2) | roulette / held / ready / spent |
| item timer | `$0D78,y` | roulette countdown, then hold blink, then post-use flash |
| roulette cursor | `$0D74,y` | pointer into a SEQUENCE (which icons cycle, in order) |
| roulette target | `$0D7C,y` | the item id it will land on, chosen when the box is hit |
| effect word | `$E0,x` (kart) | one bit per item, set on use; each handler clears its bit |
| the box | `$81:B34A` | starts the roulette (`$0D70=$A000, $0D78=$C1`) |
| per-frame item | `$81:B387` | the state machine (below) |
| use | `$81:B3FB` → `$81:B41F` | `$E0 \|= $81:B336[id]`, `$0D70=0` |
| effects | `$80:E88B..EA35` | per-bit handlers, run each frame per human kart |
| projectiles | `$80:F17A` | spawner; blocks `$1A00`/`$1A80` (1P list `$80:F174`) |
| the hit | `$81:9967` | a player projectile touches a kart |
| the spin | `$80:B443` → states `$A6=$0A/$0C` at `$80:A94F/A97A` | the reaction |

## 2. The item word — DECODED (`$81:B387`)

`$0D70` is negative while anything is going on. Bits:

    $8000  something is here (roulette, held, ready, or the empty flash)
    $2000  ROULETTE spinning
    $4000  READY: the button will use it
    $1000  post-use FLASH of an empty slot (Boo's theft)
    low byte: the item id currently shown / held

Timeline, MEASURED from the user's races (frame numbers from `flag`):

    f2167  box hit        $0D70=$A000  $0D78=$C1 (193)  $0D74=seq  $0D7C=target
           every 4 frames ($0D78 & 3 == 0) the cursor steps one entry of the
           sequence and the icon shown is the low byte of $0D70
    ...    $0D78 counts down 193 -> 0 -> negative
    f2360  $0D78 reaches 0; from here the roulette keeps stepping until the
           shown id == $0D7C (the target) - or $0D78 < -64 forces a stop
    f2377  landed: $0D70 = $8000|id, $0D78 = $40
           64 frames of HOLD, the icon blinking every 8 frames ($0D78 & 8)
    f2442  $0D70 |= $4000: READY
    ...    held indefinitely
    f5730  A pressed: $E0 |= bit(id), $0D70 = 0, the slot draws empty

Pressing the button while the roulette spins (`$81:B3C1`, `bit $0027,x`)
does `$0D78 |= $FFF0`: it jumps the countdown to within 16 frames of the
end, so the roulette stops early **but still on the same target**.

Use is refused (`$81:B3FB`) while `$10 & $0820` (airborne / under the
game's control), while `$72,x != 0`, or when the button is not held.

## 3. Which item — DECODED (`$81:B698`, `$81:B6D1`, `$81:B6F2`)

Chosen the moment the box is hit, from three things:

1. **The track's item block**, `$81:B471[$0D28]`, where `$0D28 =
   $81:8B73[track]` (Mario Circuit 2, Ghost Valley 0, Donut Plains 4,
   Choco Island 6, Vanilla Lake 8, Koopa Beach $A, Bowser Castle and
   Rainbow Road $C):

       $0D28   block      $0D28   block
         0     $81:B5BB     8     $81:B642
         2     $81:B5A0    $A     $81:B627
         4     $81:B5D6    $C     $81:B5F1
         6     $81:B60C    $E     $81:B65D (battle)

   Each block is 3 records of 9 bytes.

2. **Which record**, by lap and rank, `$81:B666[lap*8 + rank]` (GP; the
   2P match race uses `$81:B68E[lap*4 + rank/2]` and `$81:B481`):

       lap 1            everyone            record 1
       laps 2-5         1st                 record 1
                        2nd-4th             record 0
                        5th-8th             record 2

3. **The roll**: `r = $1F26 & $1F`, five random bits. The record's eight
   bytes are thresholds; the id is the first `i` with `r < t[i]` (a zero
   threshold is skipped), and if none matches the id is 8 (lightning) -
   the ninth byte, always ≥ $80, doubles as that catch-all and its low
   nibble names the SEQUENCE the roulette will cycle (`$81:B491[n]`).

So, e.g. Mario Circuit (`$81:B5A0`), record 1 = `02 00 00 0C 14 16 00 20`:
mushroom 2/32, banana 10/32, green 6/32, red 2/32, coin 10/32, and the
leader on lap 2+ never sees a feather, star, Boo or lightning.

The sequences (`$81:B49D..`):

    0: 0 1 2 3 4 5 6 7 8      3: 0 1 2 3 4 5 7 8
    1: 0 2 3 4 5 6 7 8        4: 0 2 3 4 5 7 8
    2: 0 1 2 3 4 5 6          5: 0 1 2 7 8
    6: 10 2 5 8 9   (battle mode only; ids 9 and 10 are battle items)

MEASURED: both of the user's Mario Circuit races used sequence 4
(`$B4C9`) and landed on 5 (red), 0 (mushroom), 3 (banana), 7 (coin) - all
inside record 1's non-zero set. Consistent.

OURS: `$1F26` is the game's own random source and is not reproduced; the
port rolls its own five bits. Ledgered.

## 4. The nine items — DECODED (`$81:B336` bits, `$80:E88B` handlers)

| id | item | `$E0` bit | what the handler does |
|---|---|---|---|
| 0 | mushroom | `$8000` | `$80:B46B`: `$A8=0`, `$A6=$1C`, sound $48, `$E2\|=$80`, drive `$AC=$10` — the boost the port already has (`smk_player_boost`) |
| 1 | feather | `$4000` | `$80:B578`: sound $24, `$26=$1E0` (launch), `$1E=$100`, `$A6=$18` — a tall hop |
| 2 | star | `$2000` | `$80:E9F8`: `$4E\|=$8000` (flashing), `$86=$200` (512 frames); `$80:B4B2`: `$E2\|=2`, `$AC=$12`. While `$E2&2` a hit is ignored (`$80:A957`) |
| 3 | banana | `$0800` | thrown: variant 0 (dropped behind) or 6 with the pad's `$0B00`-mask == `$0800` (thrown ahead) |
| 4 | green shell | `$1000` | thrown: variant 1, or 7 with `$0700`-mask == `$0400` |
| 5 | red shell | `$0400` | thrown: variant 2, homing (`$81:9EC2`) |
| 6 | Boo | `$0200` | `$80:E954`: `$82=$480` (1152 frames invisible), steals the other player's item word (2P); sound $56/$57 |
| 7 | coin | `$0100` | `$80:E9CB`: `$0E00 += 2`, sound $0D |
| 8 | lightning | `$0040` | `$80:EA3B`: every OTHER kart gets `$E2\|=$300`, `$E4=$1000`, `$8C\|=3`, `$84=$440` (shrunk; `$3FF` if already under `$400`), sound $16 |

The star runs `$86` down (`$80:EA09`); at 1 it clears `$4E` bit 15 and
`$E2` down to bit 14 and calls `$80:A631`. While `$68,x` (surface class)
is below `$52` the countdown pauses at 1 - LABELLED, not understood.
CORRECTED by measurement (tools/labs/starlab.py / starlab2.py, NOTES
208): live, `$86` runs straight down at 1 a frame on classes `$40`,
`$4E`, `$54` and `$5A` alike and the star ends at 512 frames on all of
them - whatever that code path guards, it never manifests as a pause.

## 5. Projectiles — MEASURED (`tools/labs/itemfx.py`)

Spawn (`$80:F17A`): the first free block on the list (`$12` bit 15 clear),
owner `$6A = $B4`, `$66 = $3C` if the owner is the human else 0, variant
`$70 = v*2`, `$6E = $80:EED8[v]` (its handler record), `$4E=$4000`,
`$5E=8`, `$12=$8000`, `$6C=5`, and the script chains `$04=$F243`,
`$0C=$F261`. Velocity comes from heading `$2A` and speed `$72` through
the DSP-1 (`$80:F8CB`, command 4).

Handler records (`$80:EED8` → `$80:F6DB..`): `[common $F897, on-road,
off-road, after-hit]`, the common part dispatching on `$42` bit 15 (has
hit) and `$68 & $20` (off the track → `$42=$8000`, `$26=$80`):

| variant | record | on-road handler | reading |
|---|---|---|---|
| 0 banana (dropped) | `$F6E3` | `$F745` = RTS | sits still |
| 1 green | `$F6EB` | `$FE07` | pure integrator: x+=vx, y+=vy, z+=zvel |
| 2 red | `$F6F3` | `$F86C` → `$81:9EC2` | homing, then `$F851` when it has hit |
| 4 banana (thrown) | `$F6DB` | `$F746` | flies `$66` frames, then lands (`$1F=0`) and parks |
| 5, 6 | `$F6FB` | `$F7F1` | ? (timer, `$84:D629`) |
| 3 = 1, 7 = 0 | | | the AI's green / the backward drop |

Red shell homing (`$81:9EC2`): target kart `$64,x`; while `$40,x` counts
down no steering; if the target is on the ground and within `$C0` on
both axes, snap `$2A` to the bearing; else steer `$2A` toward it by
±4 per frame with a dead band of 8 (`$81:9F02`) — units to be confirmed
by measurement (`?`).

MEASURED (`tools/labs/itemfx.py`, the attract race with `$0E50` zeroed -
that flag disables the projectile spawner and the AI's chase row in the
demo, NOTES 185):

- a shell leaves at the kart's heading and **the kart's speed + `$300`**
  (514 → 1282, 530 → 1298), following the kart's heading for three frames,
  then flies straight at that speed, 5 px a frame at 1298;
- a **green** shell bounces: the component that met the wall reflects at
  **7/8** (−1286 → +1125), the other is untouched; it was still alive after
  300 frames and three bounces;
- a **red** shell steers toward its target after an 8-frame delay: snap
  inside `$0800`, else `$0400` a frame (the table at `$81:9F02` read as
  words - the bytes are `00 08 00 F8 00 04 00 FC`); its target was the
  kart one place ahead; and it **dies on a wall** (`$42 = $8000` and a hop
  at a class-`$80` cell) where a green bounces;
- a dropped **banana** sits 7-8 px behind the kart and never expires;
- a shell that hits a kart hops (`$26 = $100`) and is gone when it lands.

OURS, ledgered (S31): the thrown banana's flight (UP did not register
through the oracle's pad), the backward throw's speed, an 8-bounce life
for a green shell, and the 8 px contact box.

## 6. Getting hit — DECODED + MEASURED

The user's race, frame 5596 (a banana on the floor): `$10 |= $5000`,
`$E2 |= $9000`, coins 15 → 11 in one frame. Twelve frames later (the kart
was mid-power-slide) the spin state took over: `$E2 = $0008`, speed
clamped to 768 then −8/frame, `$FA` counting 59, 56, 53...

Decoded (`$81:9967`, then `$80:B49D` → `$80:B443`):

    the projectile:  $E2 |= $1000 on the victim, $8C |= 3, cooldown $5E = 3
                     (a star kart, $10 & $2000, deflects it instead)
    next kart update ($E2 & $1000):
        speed = min(speed, $300)         768
        drive $AC = 0
        $FA = 60                         the spin's countdown
        $A6 = $0C, or $0A if $AA < 0     which way it spins
    each frame in $0A/$0C ($80:A94F / $80:A97A):
        $E2 |= 8                         "spinning out" for the sprite
        if $E2 & 2 (star): straight to $1C
        while $FA > 0: $FA--, pose $AA -= $0A00 ($0A) / += $0A00 ($0C)
        then while speed >= $100: keep turning
        then: speed = 0 (?), $E2 |= $0400, $A6 = $1C (settle), sound $2A
    the coins: four, $85:E4DA (`sbc #4`, clamped at 0), with the spill
               the port already draws (NOTES 183); a kart bump is ONE
               ($85:E4AF).  Both reached through $85:E3E0's dispatch.

`$0A00` per frame is 14° — a full turn every 26 frames, so a 60-frame
spin is about 2⅓ turns. The port's existing oil spin-out is the sibling
states `$0E/$10` (`±$480`/frame, −16 speed/frame); the item spin is
faster and timed.

**The coinless bump (NOTES 187).** A kart-kart bump reads each kart's
"has coins" bit (`$4E` bit 3, mirrored from `$0E00` at `$80:EAE3`); the
kart without gets `$E2 |= $0800` (`$81:9AF5`), which `$80:B49D` routes to
`$80:B435`: state `$0E/$10` by the sign of `$AA`, `$A8 = 0`, nothing
else. The pose turns `$480` a frame and the speed falls 15.5 a frame to
zero (835 → 0 in 56 frames, measured with `tools/labs/bumpspin.py`), then
`$1C`. The port: `smk_player_hit_bump`, and `smk_racer_hit` kind 4.

## 7. HUD — DECODED and FOUND

The slot is NOT sprites: `$81:B31C`'s `$0C26/$0C28/$0C66/$0C68` are
cells of the HUD tilemap (row 0 and row 1 of a 32-wide map, column 19),
which is why three OAM dumps through a roulette showed nothing changing.
The tiles are **2bpp BG3 at VRAM word `$7000`** - the mushroom decodes
there in one look and nowhere else - and their source is the compressed
blob at **`$C1:12F0`** (1792 bytes, 112 tiles; VRAM tile n = blob tile
n − `$80`), found by decompressing every start in banks `$C0-$C7` and
searching for the bytes VRAM held. `$81:B320[id] = (tile, attr)`: the
icon is `t, t+1` over `t+2, t+3` with BG palette `(attr >> 2) & 7`, from
the track's own CGRAM - which the port already loads, so the icons match
the game's pixel for pixel (`tmp/icons_cmp.png`). The blink blank is
`$B0`; the empty box's `$E8/$E9` decoded as noise in that VRAM and the
port draws nothing when nothing is held.

The COLOURS took three tries, and the user's screenshot of a held green
shell (green, white highlights, black inside a light-blue frame) was the
judge: the HUD strip is mode 0, each background owns a 32-colour CGRAM
block, and the item box is on **BG2, block 32** - palette 4 there is
sky/white/green/black. Block 0 gave a dark red shell and BG3's block 64 a
blue one. The frame around the icon is `$D0 $D1 $D1 $D0` over `$D2`/`$D3`
down the sides, h-flipped on the right, palette 7 (light blue), from the
same tilemap dump.

**The projectiles' sprites** were in OAM all along - parked off screen at
x = 319 when unused, which is why a set-difference against a no-item frame
found nothing - and they are the SMALL sprite size, which in this OBSEL is
8x8 (the karts' "16x16" quadrants are the large size). One tile each:
green `$FC`, red `$FE`, from the shared blob at `$C1:0000` (VRAM tile n =
blob tile n − `$EF`), sprite palette 4, decoded pixel-exact. The dropped
banana's sprite is OPEN: it lands behind the kart, off screen, so no dump
caught it live, and the `$F8/$F9` tiles the byte searches turned up are a
post, not a banana. It is a bank-`$85` entity on the road (its four coins
go through `$85:E3E0`'s table), so its art is in an object sheet; the port
draws a labelled yellow disc until it is caught.

## 8. Out of scope for this pass

- **AI weapons** — DONE in NOTES 190: not bank `$85` entities at all but
  the human's own two projectile slots, carried behind the AI kart for 58
  frames and released; one weapon per character; only against the
  player, from lap 2, when near.
- **Boo's theft in 1P** (there is no second human) and the **battle
  items** (ids 9, 10).
- **Sound** — every item names its sound above; nothing plays yet.
- **Boo's invisibility** does nothing to an AI that cannot target you
  yet; the port flickers the player's own sprite (OURS).
- The **star's** flashing (`$4E` bit 15 cycles CGRAM) is OURS: the driver
  palette cycles. The **shrunk** kart (`$84`) is drawn at half size (OURS)
  and accelerates at 2 a frame (MEASURED, 466 → 644 over a hundred frames).

## 9. Port design

- `src/item.c`: the item word state machine, verbatim from §2, with the
  tables read from the ROM (§3) and the port's own RNG for the roll.
- `src/item.c` effects: mushroom → `smk_player_boost` (exists); feather →
  a launch; star/Boo/lightning → timers on the player (`t86/t82/t84`)
  with their flags; coin → `coins += 2`.
- `src/projectile.c`: two slots, variants 0/1/2/4 with MEASURED motion;
  collision with karts → the §6 reaction (which is also what an AI's
  banana does to you, via the existing entity collision).
- `src/player.c`: states `$0A/$0C` beside the existing `$0E/$10`.
- HUD: the slot and roulette, from the dumped art.
- Gates: `tools/itemcheck.c` replays the user's `flag` roulette
  (`$0D70/$0D78` per frame) through `smk_item_step` and diffs; the hit
  reaction against frames 5596-5660 of the same race.
