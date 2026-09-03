#!/usr/bin/env python3
"""The COIN ITEM's own animation, from the game.

The user: "it is EXACTLY the same as picking one coin.  But it is two
instead and one after the other.  Both centered at the kart driver's
head."  That is a statement about the game, so it gets measured against
the game rather than argued about.

Poke a READY coin item ($0D70 = $C007), press A, and log every coin
sprite in OAM ($86/$A2/$60) frame by frame - absolute screen x and y, so
it can be put beside the ROAD pickup that tools/labs/coinpick.py logs in
exactly the same units.  Also logs the player's own kart sprite, so
"centred on the kart" is a number and not an impression.
"""
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


def coin_sprites(L):
    return [s for s in sprites(L) if s[3] in COIN_TILES]


def kart_box(L):
    """the player's own kart: the LARGE sprites low on the screen"""
    big = [s for s in sprites(L) if s[5] and s[2] > 120]
    if not big:
        return None
    x0 = min(s[1] for s in big); x1 = max(s[1] for s in big) + 16
    y0 = min(s[2] for s in big)
    return x0, x1, y0


L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
L.pace(400)
# let the road coins the flow field ran over settle out of OAM first
for _ in range(40):
    L.frame(0x80)

kb = kart_box(L)
log("kart sprite box before: %s   coins %d" % (kb, L.w(0x0E00)))
log("poking a READY coin item and pressing A")
L.b.wram[0x0D70] = 0x07; L.b.wram[0x0D71] = 0xC0      # READY coin ($0D70 = $C007)
L.frame(0x80, 0x80)                                    # A: use it
log("  item word now %04X, coins %d" % (L.w(0x0D70), L.w(0x0E00)))

for f in range(60):
    cs = coin_sprites(L)
    kb = kart_box(L)
    kcx = (kb[0] + kb[1]) // 2 if kb else None
    ktop = kb[2] if kb else None
    if cs:
        log("  +%2d coins %d kart cx %s top %s | %s"
            % (f, L.w(0x0E00), kcx, ktop,
               "  ".join("s%d x%d y%d $%02X%s"
                         % (k, x, y, t, "  dx %+d dy %+d" % (x + 8 - kcx, y - ktop)
                            if kcx is not None else "")
                         for k, x, y, t, p, b in cs)))
    else:
        log("  +%2d coins %d kart cx %s top %s | -" % (f, L.w(0x0E00), kcx, ktop))
    L.frame(0x80)
log("done")
