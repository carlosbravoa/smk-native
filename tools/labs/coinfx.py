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

# The coin's own state, not its screen position.
#
# The first pass logged OAM and got a beautiful arc that was partly the
# CAMERA: x moved exactly -2 every frame, which is a pan, not a coin.
# $85:E5E3 writes the effect's fields into the slot $0FE2 points at, so
# the world-space motion is readable directly - and that is what a port
# needs, since our camera is not theirs.
SLOT = None
base = oam_set()
log("\n f  coins  $0FE2   slot fields +$00..+$0F")
for f in range(FRAMES):
    L.frame(0x80)
    c = L.w(0x0E00)
    ptr = L.w(0x0FE2)
    if SLOT is None and c < COINS:
        SLOT = ptr
        log("  coin lost at frame %d; effect slot $%04X" % (f, SLOT))
    if SLOT is not None:
        row = " ".join("%04X" % L.w(SLOT + i) for i in range(0, 16, 2))
        log(" %2d %5d  $%04X  %s" % (f, c, ptr, row))
    elif f % 10 == 0:
        log(" %2d %5d  $%04X  (waiting)" % (f, c, ptr))
log("\ncoins after: %d" % L.w(0x0E00))
