#!/usr/bin/env python3
"""Read the forced-projectile dumps (tools/labs/mame/forceproj*.lua): for
one tag, the sprites that are not karts, HUD, shadow or dust, frame by
frame, and where each of their tiles is in the ROM.

    tools/labs/projfind.py tmp/fp v5_o11 [tmp/fp.cgram]
"""
import sys, os, glob
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from smktool.rom import Rom
from spritefind import char_tiles, Locator
ROOT = os.path.dirname(os.path.dirname(HERE))
pre, tag = sys.argv[1], sys.argv[2]
rom = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
loc = Locator(rom)
seen = {}
for f in sorted(glob.glob("%s.%s.*.obj" % (pre, tag))):
    k = int(f.split(".")[-2]); vram = open(f[:-4] + ".vram", "rb").read()
    for l in open(f):
        if l.startswith("#"): continue
        i, x, y, ch, pal, size, nsel, hf, vf, _ = [int(v) for v in l.split()]
        if y <= 0 or y >= 225 or x >= 300: continue
        if y < 20 and (x < 70 or x > 200): continue                   # HUD
        if nsel == 1 and ch >= 0x80: continue                          # karts
        if nsel == 1 and ch < 0x20: continue                           # dust / sparkle rows
        if nsel == 0 and 0xEB <= ch <= 0xED: continue                  # the shadow
        c9 = (nsel << 8) | ch
        tl = char_tiles(c9, size)
        raw = [bytes(vram[t * 32:t * 32 + 32]) for t in tl]
        key = (c9, pal, size)
        if key not in seen:
            seen[key] = (k, x, y, tl, [loc.where(r) for r in raw])
for (c9, pal, size), (k, x, y, tl, where) in sorted(seen.items(), key=lambda kv: kv[1][0]):
    print("  +%2d  char $%03X pal %d %s at (%3d,%3d)  tiles %s" % (k, c9, pal, "16x16" if size else "8x8", x, y,
          " ".join("$%03X" % t for t in tl)))
    for t, w in zip(tl, where):
        if w != "blank": print("        $%03X <- %s" % (t, w))
