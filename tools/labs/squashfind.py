#!/usr/bin/env python3
"""Read the forced-squash dumps (tools/labs/mame/forcesquash.lua): at which
frame did the shrunken kart's sprites change, what did they become, and
where in the ROM is every tile they were built from.

    tools/labs/squashfind.py tmp/fsq_player [kart]      kart 0 = P1 (default), 1 = AI 1

The kart's sprites are the 16x16 ones from name table 1 - the player's
sit at y 60..130; an AI kart is wherever it projects, so for kart 1 every
table-1 16x16 sprite is reported and the tile lookup sorts it out.
"""
import sys, os, glob, re
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from smktool.rom import Rom
from spritefind import char_tiles, Locator

ROOT = os.path.dirname(os.path.dirname(HERE))
pre = sys.argv[1]
kart = int(sys.argv[2]) if len(sys.argv) > 2 else 0
lo = int(sys.argv[3]) if len(sys.argv) > 3 else 0          # optional frame window
hi = int(sys.argv[4]) if len(sys.argv) > 4 else 1 << 30
rom = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
loc = Locator(rom)


def objs(path):
    out = []
    for l in open(path):
        if l.startswith("#"): continue
        i, x, y, ch, pal, size, nsel, hf, vf, _ = [int(v) for v in l.split()]
        if y <= 0 or y >= 225 or size != 1 or nsel != 1: continue
        if kart == 0 and not (60 <= y <= 130 and 90 <= x <= 160): continue
        out.append((i, x, y, ch, pal, hf, vf))
    return out


def sig(os_, vram):
    """the kart's look: its tiles' bytes, not just their numbers - the game
    streams different art into the same slots"""
    s = []
    for (i, x, y, ch, pal, hf, vf) in os_:
        for t in char_tiles(0x100 | ch, 1):
            s.append(bytes(vram[t * 32:t * 32 + 32]))
    return tuple(sorted(s))


files = sorted(glob.glob(pre + ".*.obj"))
prev = None
shown = 0
for f in files:
    fr = int(f.split(".")[-2])
    if not (lo <= fr <= hi): continue
    vram = open(f[:-4] + ".vram", "rb").read()
    os_ = objs(f)
    s = sig(os_, vram)
    if s == prev:
        continue
    prev = s
    print("f%d: %d sprites" % (fr, len(os_)))
    if shown >= 12 and hi == 1 << 30:
        continue
    shown += 1
    seen = {}
    for (i, x, y, ch, pal, hf, vf) in sorted(os_, key=lambda o: (o[2], o[1])):
        tl = char_tiles(0x100 | ch, 1)
        print("   obj%-3d x=%-3d y=%-3d char $%02X pal %d hf=%d vf=%d  tiles %s"
              % (i, x, y, ch, pal, hf, vf, " ".join("$%03X" % q for q in tl)))
        for q in tl:
            seen.setdefault(q, bytes(vram[q * 32:q * 32 + 32]))
    for q, raw in sorted(seen.items()):
        print("   VRAM $%03X <- %s" % (q, loc.where(raw)))
