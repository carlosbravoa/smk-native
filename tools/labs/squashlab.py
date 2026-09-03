#!/usr/bin/env python3
"""FORCE the squash no recording contains, and read the sprite off OAM.

The flatten is the hit-while-small path (NOTES 204/272): a shrunken kart
run over by a full-size one.  The lightning handler at $80:EA3B shrinks a
kart with `$E2|=$300, $E4=$1000, $8C|=3, $84=$440` (docs/ITEMS.md) - so
do exactly that to player 1, then stand it on the nearest AI kart every
frame until something changes.  Whatever the game then draws for the
player's kart is the squashed racer, and every one of its VRAM tiles is
looked up in the ROM.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1
from spritefind import sprites, char_tiles, Locator

L = Lab()
L.pace(400)
loc = Locator(L.r)
s16 = L.s16


def kart_sprites():
    """the player's kart: 16x16 sprites from name table 1, low on screen"""
    return [s for s in sprites(L.b) if s[5] == 1 and (s[3] & 0x100) and 40 <= s[2] <= 150]


def sig():
    return tuple(sorted((s[3], s[6], s[7]) for s in kart_sprites()))


def dump(tag):
    ks = kart_sprites()
    log("  %s: %d kart sprites" % (tag, len(ks)))
    tiles = {}
    for (i, x, y, t, pal, big, hf, vf) in sorted(ks, key=lambda s: (s[2], s[1])):
        tl = char_tiles(t, 1)
        log("    obj%-3d x=%-3d y=%-3d char $%03X pal %d hf=%d vf=%d  tiles %s"
            % (i, x, y, t, pal, hf, vf, " ".join("$%03X" % q for q in tl)))
        for q in tl:
            tiles.setdefault(q, bytes(L.b.vram[q * 32:q * 32 + 32]))
    for q, raw in sorted(tiles.items()):
        log("    VRAM $%03X <- %s" % (q, loc.where(raw)))


def kpos(k):
    return s16(L.w(k + 0x18)), s16(L.w(k + 0x1C))


log("race up at speed %d, P1 at %s state $%02X size $%04X" % (L.speed(), L.pos(), L.w(P1 + 0xA6), L.w(P1 + 0x84)))
dump("before")
base = sig()
# the lightning handler's own writes, on the player
L.sw(P1 + 0x84, 0x440)
L.sw(P1 + 0x8C, L.w(P1 + 0x8C) | 3)
L.sw(P1 + 0xE2, L.w(P1 + 0xE2) | 0x300)
L.sw(P1 + 0xE4, 0x1000)
for f in range(20):
    L.frame(0x80)
log("shrunk: state $%02X size $%04X" % (L.w(P1 + 0xA6), L.w(P1 + 0x84)))
dump("small")
base_small = sig()
prev_state = L.w(P1 + 0xA6)
shown = 0
for f in range(400):
    px, py = L.pos()
    best, bd = None, 1 << 30
    for k in range(1, 8):
        b = 0x1000 + k * 0x100
        if abs(s16(L.w(b + 0xEA))) < 32:
            continue
        x, y = kpos(b)
        d = (x - px) ** 2 + (y - py) ** 2
        if d < bd:
            bd, best = d, b
    if best is not None:
        x, y = kpos(best)
        L.sw(P1 + 0x18, x & 0xFFFF); L.sw(P1 + 0x16, L.w(best + 0x16))
        L.sw(P1 + 0x1C, y & 0xFFFF); L.sw(P1 + 0x1A, L.w(best + 0x1A))
    L.frame(0x80)
    st = L.w(P1 + 0xA6)
    if st != prev_state:
        log("f%d: P1 state $%02X -> $%02X  size $%04X  $E2 $%04X  on kart $%04X" % (f, prev_state, st, L.w(P1 + 0x84), L.w(P1 + 0xE2), best or 0))
        prev_state = st
    if sig() not in (base, base_small) and shown < 4:
        shown += 1
        dump("f%d NEW POSE (state $%02X size $%04X)" % (f, st, L.w(P1 + 0x84)))
        base_small = sig()
log("done")
