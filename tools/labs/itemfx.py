#!/usr/bin/env python3
"""What each ITEM does, measured on the running game.

    tools/labs/itemfx.py <bit>        e.g. 1000 (green shell), 0800 (banana)
    tools/labs/itemfx.py roulette     start a roulette and dump OAM/VRAM/CGRAM

$E0,x is the kart's item-effect word: using an item ORs one bit into it
($81B41F, from the table at $81B336), and the game's own DEBUG cheat at
$80E8B3 sets those bits straight from the pad - so writing $E0 is the
sanctioned way to fire an item with no roulette and no box.

  $8000 mushroom  $4000 feather  $2000 star  $0800 banana  $1000 green
  $0400 red shell $0200 boo      $0100 coin  $0040 lightning

Per frame: the player's kart state, and every ACTIVE projectile block on
the $0DFA list ($12 bit 15 = live) with its position, height, velocity,
owner and variant - the projectile physics read off the machine rather
than off $80F243's handlers.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

P1 = 0x1000
TMP = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tmp")
arg = sys.argv[1] if len(sys.argv) > 1 else "1000"
FRAMES = int(os.environ.get("FRAMES", "300"))
PAD = int(os.environ.get("PAD", "0x80"), 0)          # B held; add 0x08 UP / 0x04 DOWN etc.

L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, sw, s16 = L.w, L.sw, L.s16
# get moving on the road first
for _ in range(240):
    L.flow(1)
    if L.speed() > 500: break
log("moving at %d, at (%d,%d)" % (L.speed(), *L.pos()))
log("mode $2C=%d  $B6=%d  $015A=$%04X  $0DFA=$%04X  $0DFC=$%04X  $E0=$%04X  $B4=$%04X $B8=$%04X"
    % (w(0x2C), w(0xB6), w(0x015A), w(0x0DFA), w(0x0DFC), w(P1+0xE0), w(0xB4), w(0xB8)))
if w(0x0DFC):
    log("$0DFC is non-zero: the projectile spawner refuses ($80F17A) - item runs are VOID here")

def kart():
    return dict(spd=L.speed(), f10=w(P1+0x10), ac=L.b.wram[P1+0xAC], e2=w(P1+0xE2),
                ee=s16(w(P1+0xEE)), fa=s16(w(P1+0xFA)), aa=s16(w(P1+0xAA)),
                a8=s16(w(P1+0xA8)), a4=w(P1+0xA4), pose=w(P1+0x2A),
                z=s16(w(P1+0x1F)), t86=w(P1+0x86), t84=w(P1+0x84), t82=w(P1+0x82),
                coins=w(0x0E00), e0=w(P1+0xE0), x=w(P1+0x18), y=w(P1+0x1C),
                e4=w(P1+0xE4), c5e=w(P1+0x5E), c8c=w(P1+0x8C), c26=s16(w(P1+0x26)), c1e=s16(w(P1+0x1E)))

# The projectile blocks.  $0DFA holds a ROM address ($80F174 for one
# player): the LIST lives in ROM, the blocks it names live in WRAM.  The
# first version of this lab read the list out of WRAM at that address
# and walked garbage - every "object" it printed was noise.
def block_list():
    a = w(0x0DFA)
    if a < 0x8000: return []
    out = []
    for i in range(24):
        b = L.r.data[a + 2*i] | L.r.data[a + 2*i + 1] << 8
        if b == 0: break
        out.append(b)
    return out
BLOCKS = block_list()
log("projectile blocks from ROM list $%04X: %s" % (w(0x0DFA), " ".join("$%04X" % b for b in BLOCKS)))

def objects():
    out = []
    for b in BLOCKS:
        if w(b + 0x12) & 0x8000:
            out.append((b, w(b+0x18), w(b+0x1C), s16(w(b+0x1F)),
                        s16(w(b+0x22)), s16(w(b+0x24)), w(b+0x6A), w(b+0x70),
                        w(b+0x2A), w(b+0x72), w(b+0x64), w(b+0x40), w(b+0x66), w(b+0x14), w(b+0x42),
                        w(w(b+0x64)+0x18) if 0x1000 <= w(b+0x64) < 0x1800 else 0,
                        w(w(b+0x64)+0x1C) if 0x1000 <= w(b+0x64) < 0x1800 else 0))
    return out

if arg == "roulette":
    sw(0x0D70, 0xA000); sw(0x0D78, 0xC1); sw(0x0D74, 0xB49D); sw(0x0D7C, 3)
    for f in range(40):
        L.frame(PAD)
        if f in (2, 10, 30):
            for nm, buf in (("vram", L.b.vram), ("cgram", L.b.cgram), ("oam", L.b.oam)):
                open(os.path.join(TMP, "roul_%s_%d.bin" % (nm, f)), "wb").write(bytes(buf))
            log("  dumped at roulette frame %d: $0D70=$%04X" % (f, w(0x0D70)))
    sys.exit(0)

bit = int(arg, 16)
before = kart()
log("before: %s" % before)
# $B4 read $1100 and $B8 read 2 at this point in an earlier run, so which
# block is the human is not certain - fire on both and see which consumes
sw(P1 + 0xE0, w(P1 + 0xE0) | bit)
sw(0x1100 + 0xE0, w(0x1100 + 0xE0) | bit)
log("fired $E0 |= $%04X on $1000 AND $1100" % bit)
log(" f  spd  $10  $AC  $E2   $EE   $FA   $AA   $A8  $A4  pose   z  $86  $84 $82 coins | objects (blk x,y,z v(vx,vy) o=owner #var h=$2A s=$72 t=$64 d=$40 T=$66 $14 $42)")
seen = set()
for f in range(FRAMES):
    L.frame(PAD)
    k = kart(); objs = objects()
    if f in (20, 21, 22):            # three consecutive frames: the object pipeline runs at 30 Hz
        for nm, buf in (("vram", L.b.vram), ("cgram", L.b.cgram), ("oam", L.b.oam)):
            open(os.path.join(TMP, "item%s_%s_%d.bin" % (arg, nm, f)), "wb").write(bytes(buf))
        log("  (dumped vram/cgram/oam at frame %d)" % f)
    for o in objs: seen.add(o[0])
    line = " %3d %4d %04X %02X %04X %5d %6d %5d %5d %04X %04X %5d %4d %4d %3d %2d e0=%04X" % (
        f, k['spd'], k['f10'], k['ac'], k['e2'], k['ee'], k['fa'], k['aa'], k['a8'],
        k['a4'], k['pose'], k['z'], k['t86'], k['t84'], k['t82'], k['coins'], k['e0'])
    if objs:
        line += " | " + " ".join("$%04X(%d,%d,%d v%d,%d o$%04X #%d h%04X s%d t%04X d%d T%d %04X %04X tx=%d,%d)" % o for o in objs[:2])
    line += "  e0'=%04X e4=%04X 5E=%d 8C=%04X zv=%d 1E=%d" % (w(0x1100 + 0xE0), k['e4'], k['c5e'], k['c8c'], k['c26'], k['c1e'])
    if f < 40 or f % 5 == 0 or objs and f % 2 == 0:
        log(line)
log("blocks seen: %s" % " ".join("$%04X" % b for b in sorted(seen)))
