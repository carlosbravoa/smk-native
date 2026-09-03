#!/usr/bin/env python3
"""Which ROM stream is each live sprite tile from?

    tools/labs/spritesrc.py <grab.vram> [--from $440] [--to $7FF] [--png DIR]

The chain NOTES 271 asked for, closed and made routine:

    a sprite on screen
      -> the OBJ characters its OAM entries name   (spritegrab.lua)
      -> the VRAM tiles those characters address
      -> the ROM stream whose decompressed bytes ARE those tiles  (here)

Every graphics stream the game loads is found statically by
tools/labs/decompsites.py - the three immediate loads before each
JSL $84:E09E - so the candidate set is exactly the streams some caller
actually decompresses, not every byte range that happens to decode.

This is what tools/labs/findart.py should have been.  That one searched
banks $C0-$CF only and stepped two bytes at a time from even offsets, so
it could not have found either of the two sheets that matter: the ground
effects live at $84:9C19 (wrong bank, and an odd address) and the moles
at $C1:0000 (right bank, but reported as unreachable because the search
compared against a twelve-frame VRAM diff full of kart re-uploads).

Output is one line per contiguous run of tiles that map to one stream at
a constant offset, which is how the game uploads them:

    VRAM $440..$4BF  <- $C1:0000 tiles 16..143   (identity + 16)
"""
import sys, os, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from smktool.rom import Rom
from smktool.compress import decompress
from decompsites import sites

ROOT = os.path.dirname(os.path.dirname(HERE))


def streams(rom, data):
    """{source snes addr: decompressed bytes} for every static call site"""
    out = {}
    for _call, src, _dst, _wb in sites(data, rom):
        if (src >> 16) == 0x7F or src in out:
            continue
        try:
            buf, _ = decompress(data, rom.snes_to_pc(src), max_out=0x20000)
        except Exception:
            continue
        out[src] = bytes(buf)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vram")
    ap.add_argument("--rom", default=os.path.join(ROOT, "rom", "smk_usa.sfc"))
    ap.add_argument("--first", default="0x400")
    ap.add_argument("--last", default="0x7FF")
    args = ap.parse_args()

    vram = open(args.vram, "rb").read()
    rom = Rom.load(args.rom)
    data = bytes(rom.data)
    st = streams(rom, data)
    print("%s: %d static streams" % (args.vram, len(st)))

    # index every stream's tiles once
    index = {}
    for src, buf in st.items():
        for i in range(len(buf) // 32):
            index.setdefault(buf[i * 32:i * 32 + 32], []).append((src, i))

    lo, hi = int(args.first, 0), int(args.last, 0)
    rows = []
    for t in range(lo, hi + 1):
        b = vram[t * 32:t * 32 + 32]
        if len(b) < 32 or not any(b):
            rows.append((t, None))
            continue
        rows.append((t, index.get(b)))

    # a tile can appear in several streams; prefer the one that keeps the
    # current run going, so a shared blank-ish tile does not split a sheet
    cur = None          # (src, delta, first_tile, last_tile)
    out = []
    for t, cands in rows:
        pick = None
        if cands:
            if cur:
                for src, i in cands:
                    if src == cur[0] and i - t == cur[1]:
                        pick = (src, i); break
            if pick is None:
                pick = cands[0]
        if pick is None:
            if cur: out.append(cur); cur = None
            continue
        src, i = pick
        if cur and src == cur[0] and i - t == cur[1]:
            cur = (cur[0], cur[1], cur[2], t)
        else:
            if cur: out.append(cur)
            cur = (src, i - t, t, t)
    if cur: out.append(cur)

    named = 0
    for src, delta, t0, t1 in out:
        if t1 - t0 < 1:
            continue
        named += t1 - t0 + 1
        print("  VRAM $%03X..$%03X  <- $%02X:%04X tiles %d..%d"
              % (t0, t1, src >> 16, src & 0xFFFF, t0 + delta, t1 + delta))
    live = sum(1 for _, c in rows if c is not None)
    blank = sum(1 for _, c in rows if c is None)
    print("  %d of %d live tiles placed in a run (%d blank)"
          % (named, live + blank - blank, blank))
    return 0


if __name__ == "__main__":
    sys.exit(main())
