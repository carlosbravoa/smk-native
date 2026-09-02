#!/usr/bin/env python3
"""Where the ROM puts its own HUD.

The dashboard is meant to be the game's, in the game's places
(ROADMAP item 11), and the port had grown a layout of its own.  So read
the real one: boot a race, let the HUD settle, and dump the OAM shadow -
tile number, screen position and size for every sprite the game is
showing.  The HUD tiles are $40..$BF (SMK_HUD_TILE0/SMK_HUD_TILES), and
the digits are $A7..$AB then $B7..$BB (smk_hud_digit).

    tools/labs/hudlayout.py            # FRAMES=900 COINS=7 LAP=2
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

FRAMES = int(os.environ.get("FRAMES", "900"))
COINS  = int(os.environ.get("COINS", "7"))
LAP    = int(os.environ.get("LAP", "2"))

def digit_of(t):
    if 0xA7 <= t <= 0xAB: return t - 0xA7
    if 0xB7 <= t <= 0xBB: return t - 0xB7 + 5
    return None

L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, wram = L.w, L.b.wram
# a lap and some coins, so the counters have something to show
wram[0x10C1] = 0x7F + LAP
L.sw(0x0E00, COINS)
L.flow(FRAMES)
log("after %d frames: lap byte $%02X, coins $0E00 = %d" % (FRAMES, wram[0x10C1], w(0x0E00)))

oam = bytes(L.b.oam[:544])
log("OAM sprites on screen (ALL=1 for every tile, else $40..$BF):")
rows = []
for i in range(128):
    x, y, t, a = oam[i*4], oam[i*4+1], oam[i*4+2], oam[i*4+3]
    hi = oam[512 + (i >> 2)]
    xb = (hi >> ((i & 3) * 2)) & 1
    big = (hi >> ((i & 3) * 2 + 1)) & 1
    if y >= 0xE0:                      # parked off-screen
        continue
    tile = t | ((a & 1) << 8)
    if os.environ.get('ALL') is None and not (0x40 <= tile < 0x40 + 128):
        continue
    rows.append((y, x + (xb << 8), tile, a, big, i))
for y, x, tile, a, big, i in sorted(rows):
    d = digit_of(tile)
    log("  slot %3d  x %3d  y %3d  tile $%02X  attr $%02X %s%s"
        % (i, x, y, tile, a, "BIG " if big else "", ("digit %d" % d) if d is not None else ""))
log("%d HUD sprites on screen" % len(rows))
