#!/usr/bin/env python3
"""The coin item, part two: the sprite SIZE and where the player's kart
actually sits, so "centred on the kart" is a number.  Same poke as
coinitem.py, but it dumps every sprite once and then the coins with their
size flag."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

COIN_TILES = (0x86, 0xA2, 0x60)

def sprites(L):
    out = []
    o = L.b.oam
    for k in range(128):
        y = o[k * 4 + 1]
        if y in (0, 0xF0, 0xE0):
            continue
        x, t, a = o[k * 4], o[k * 4 + 2], o[k * 4 + 3]
        hi = o[512 + (k >> 2)]
        big = (hi >> ((k & 3) * 2 + 1)) & 1
        x |= ((hi >> ((k & 3) * 2)) & 1) << 8
        t |= (a & 1) << 8
        if x < 256 and y < 224:
            out.append((k, x, y, t, (a >> 1) & 7, big))
    return out

L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
L.pace(400)
for _ in range(40):
    L.frame(0x80)
log("every sprite on screen, before the item:")
for k, x, y, t, p, b in sprites(L):
    log("   s%-3d x%3d y%3d tile $%03X pal %d %s" % (k, x, y, t, p, "16x16" if b else "8x8"))
L.b.wram[0x0D70] = 0x07; L.b.wram[0x0D71] = 0xC0
L.frame(0x80, 0x80)
for f in range(14):
    cs = [s for s in sprites(L) if s[3] in COIN_TILES]
    log("  +%2d | %s" % (f, "  ".join("s%d x%d y%d $%02X %s" % (k, x, y, t, "16x16" if b else "8x8")
                                      for k, x, y, t, p, b in cs)))
    L.frame(0x80)

