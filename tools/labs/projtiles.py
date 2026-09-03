#!/usr/bin/env python3
"""Which VRAM tile does each PROJECTILE fly as, and where is it in the ROM?

The SNES cannot scale a sprite and the game keeps no size ladder for
thrown items (NOTES 272): each one is a single 8x8 sprite out of the row
at VRAM $4E0..$4FF.  The shells were pinned by catching them in a
recording; nobody in any recording throws an egg, a fireball or a
poison mushroom, so this has the game throw them itself.

$80:F17A is the spawner every projectile goes through (docs/ITEMS.md
section 5): the variant arrives in Y, the owner's kart block in $B4.
This calls it on the oracle - return address pushed, run to the RTS -
for every variant 0..7, then watches OAM for the 8x8 sprite that was not
there before and says where its tile came from.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1
from smktool.cpu import M_, X_
from spritefind import sprites, char_tiles, Locator

SPAWN, SPAWN_RTS = 0x80F17A, 0x80F17F


def call(L, addr, y, stop):
    c = L.c
    save = (c.PB, c.PC, c.P, c.A, c.X, c.Y, c.D, c.DB, c.S)
    c.P &= ~(M_ | X_)
    c.D, c.DB, c.Y = 0, 0x7E, y
    ret = (stop & 0xFFFF) - 1                  # RTS adds one
    c.push8(ret >> 8); c.push8(ret & 0xFF)
    c.PB, c.PC = addr >> 16, addr & 0xFFFF
    ok = c.run_to(stop, budget=3_000_000)
    c.PB, c.PC, c.P, c.A, c.X, c.Y, c.D, c.DB, c.S = save
    return ok


def small_sprites(L):
    return [s for s in sprites(L.b) if s[5] == 0 and not (s[3] & 0x100) and (s[3] & 0xFF) >= 0xE0]


L = Lab(zero=(0x0E50, 0x0E51))
L.pace(400)
loc = Locator(L.r)
log("race up, speed %d; baseline over 60 frames" % L.speed())
baseline = {}
for f in range(60):
    L.frame(0x80)
    for s in small_sprites(L):
        baseline.setdefault((s[3], s[4]), f)
log("  baseline 8x8 chars: " + " ".join("$%02X/p%d" % (t & 0xFF, p) for (t, p) in sorted(baseline)))

for v in (0, 1, 2, 3, 4, 5, 6, 7):
    L.sw(0x0DFC, 0)
    L.sw(0x00B4, P1)
    ok = call(L, SPAWN, v, SPAWN_RTS)
    kx, ky = 128, 100
    seen = {}
    for f in range(30):
        L.frame(0x80)
        for (i, x, y, t, pal, big, hf, vf) in small_sprites(L):
            key = (t, pal)
            if key in baseline and not (f < 4 and abs(x - kx) < 48 and abs(y - ky) < 40):
                continue
            seen.setdefault(key, (f, x, y))
    log("variant %d (%s):" % (v, "called" if ok else "CALL DID NOT RETURN"))
    for (t, pal), (f, x, y) in sorted(seen.items(), key=lambda kv: kv[1][0]):
        tile = char_tiles(t, 0)[0]
        raw = bytes(L.b.vram[tile * 32:tile * 32 + 32])
        log("    char $%02X pal %d  first f+%d at (%d,%d)  VRAM $%03X <- %s"
            % (t & 0xFF, pal, f, x, y, tile, loc.where(raw)))
    # let it clear before the next one
    for f in range(90):
        L.frame(0x80)
log("done")
