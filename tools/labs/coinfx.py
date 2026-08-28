#!/usr/bin/env python3
"""What the game DRAWS when a kart loses coins.

    tools/labs/coinfx.py [victim]        victim = kart block 1..7

The user wants the coin animation, both ends: the pickup and the spill
when you are hit.  The ROM side is decoded - $85:E4B2 takes ONE coin,
$85:E4E5 takes FOUR (the banana), and both fall into $85:E5E3, which
spawns effect objects from records at $85:E3E0 into a slot pointed at by
$0FE2.  What none of that says is what the effect LOOKS like.

So ask the running machine, which is the lesson of S28: give the player
coins, shove him into an AI kart to trigger the loss, and log OAM every
frame.  New sprites, their tiles, sizes, palettes and how they move ARE
the animation.  The oracle exposes OAM; MAME does not, which is why this
cannot be read off the user's recordings.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

P1 = 0x1000
VICTIM = int(sys.argv[1]) if len(sys.argv) > 1 else 1
COINS  = int(os.environ.get("COINS", "8"))
FRAMES = int(os.environ.get("FRAMES", "90"))

L = Lab(settle=120)
w = L.b.wram

def oam_set():
    """every visible sprite as (x, y, tile, attr)"""
    o, out = L.b.oam, {}
    for k in range(128):
        x, y, t, a = o[k*4], o[k*4+1], o[k*4+2], o[k*4+3]
        hi = o[512 + (k >> 2)]
        big = (hi >> ((k & 3) * 2 + 1)) & 1
        x |= ((hi >> ((k & 3) * 2)) & 1) << 8
        if y in (0, 0xF0, 0xE0): continue
        out[k] = (x, y, t, a, big)
    return out

log("coins before: P1 $%04X" % L.w(0x0E00))
L.sw(0x0E00, COINS)
log("coins set to %d" % COINS)

vb = 0x1000 + VICTIM * 0x100
log("shoving P1 onto kart %d at (%d,%d)"
    % (VICTIM, L.w(vb + 0x18), L.w(vb + 0x1C)))
L.sw(P1 + 0x18, L.w(vb + 0x18))
L.sw(P1 + 0x1C, L.w(vb + 0x1C))

base = oam_set()
log("\n f  coins  new sprites (slot: x,y tile attr size)")
track, dumped = {}, False
for f in range(FRAMES):
    L.frame(0x80)
    now = oam_set()
    c = L.w(0x0E00)
    fresh = {k: v for k, v in now.items() if k not in base}
    for k in fresh:
        if k not in track: track[k] = []
    # follow every slot that appeared, FULLY, so the arc can be fitted
    for k in list(track):
        if k in now: track[k].append((f, now[k]))
    if fresh:
        log(" %2d %5d  %s" % (f, c, " ".join(
            "%d:(%d,%d t$%02X a$%02X)" % (k, v[0], v[1], v[2], v[3])
            for k, v in sorted(fresh.items())[:5])))
    if c < COINS and not dumped:
        dumped = True
        open("tmp/coin_vram.bin", "wb").write(bytes(L.b.vram))
        open("tmp/coin_cgram.bin", "wb").write(bytes(L.b.cgram))
        log("  (VRAM and CGRAM dumped at the loss, frame %d)" % f)
    base = now

log("\ncoins after: %d" % L.w(0x0E00))
log("\nfull trajectories of the spawned sprites:")
for k, rows in sorted(track.items()):
    if len(rows) < 6: continue
    log("  slot %d, %d frames:" % (k, len(rows)))
    prev = None
    for f, (x, y, t, a, big) in rows[:40]:
        d = "" if prev is None else "  d(%+d,%+d)" % (x - prev[0], y - prev[1])
        log("    f%-3d (%3d,%3d) tile $%02X attr $%02X %s%s"
            % (f, x, y, t, a, "16x16" if big else "8x8", d))
        prev = (x, y)
