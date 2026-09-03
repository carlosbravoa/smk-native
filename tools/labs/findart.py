#!/usr/bin/env python3
"""Where in the ROM does an entity's art live?

    tools/labs/findart.py <vram.bin> [--sprite-only]

The port carries five sprite tables as ripped pixel data (src/*art.inc)
because their ROM source was never located - which is also what stops the
repository from shipping only tools.  This closes the loop the honest way:
grab the LIVE VRAM while the thing is on screen
(tools/labs/mame/vramgrab.lua), take signatures out of it, and search
every decompressible stream in the graphics banks for them.

It is find_entity_gfx.py generalised: that one carried three hard-coded
pipe signatures, this one takes them from a dump.
"""
import sys, os, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from smktool.rom import Rom
from smktool.compress import decompress

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def tiles_of(vram, lo=0, hi=0x10000):
    """every distinct non-blank 4bpp tile in the dump, by VRAM tile number"""
    out = {}
    for t in range(lo // 32, hi // 32):
        b = bytes(vram[t * 32:t * 32 + 32])
        if len(b) < 32 or not any(b):
            continue
        out[t] = b
    return out


def main():
    vpath = sys.argv[1]
    vram = open(vpath, "rb").read()
    r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
    data = bytes(r.data)

    live = tiles_of(vram)
    print("%s: %d non-blank 4bpp tiles in VRAM" % (vpath, len(live)))

    # decompress every plausible stream once, then ask which live tiles it holds
    streams = []
    for bank in range(0xC0, 0xD0):
        try:
            start = r.snes_to_pc(bank << 16)
        except Exception:
            continue
        if start + 0x10000 > len(data):
            continue
        off = 0
        while off < 0x10000:
            try:
                out, used = decompress(data, start + off, max_out=0x8000)
            except Exception:
                off += 2
                continue
            if len(out) >= 512:
                streams.append((bank, off, bytes(out)))
            off += max(2, used)
    print("  %d decompressible streams of 512+ bytes in banks $C0-$CF" % len(streams))

    hits = collections.defaultdict(list)
    for bank, off, out in streams:
        for tn, sig in live.items():
            i = out.find(sig)
            if i >= 0 and i % 32 == 0:
                hits[(bank, off, len(out))].append((tn, i // 32))
    if not hits:
        print("  NO stream holds any live tile - the art is built at runtime,"
              " or lives outside $C0-$CF")
        return 1
    for (bank, off, n), lst in sorted(hits.items(), key=lambda kv: -len(kv[1])):
        lst.sort()
        print("  $%02X:%04X (%d bytes, %d tiles): %d live tiles, VRAM $%03X..$%03X"
              " at stream tiles %d..%d"
              % (bank, off, n, n // 32, len(lst), lst[0][0], lst[-1][0],
                 lst[0][1], lst[-1][1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
