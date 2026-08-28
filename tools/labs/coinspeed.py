#!/usr/bin/env python3
"""Which row of the ROM's speed table a recorded race was driven on.

    tools/labs/coinspeed.py tmp/flag.csv tmp/cc100.csv

flaglog.lua logs P1's speed and $0E00 every frame of a real run.  The ROM
fixes the top speed completely:

    $81:8000    8 words, the character's base           -> $B4
    $81:F026    the class: 50cc -$80, 100cc +0, 150cc +$A0
    $80:A2xx    $D6 = $B4 + 8 * min(coins, 10)

so a race does not need a curve FITTED to it - it needs a row IDENTIFIED.
This prints the plateau the driver actually sat at and names every
(character, class) the ROM allows for it.  Free-parameter fitting was
tried first and is worse: the driver spends most of a race away from the
cap, and those undershoots pull a blind fit off the real row.

Two things in a race are not the rule, and both are handled by measuring
DWELL rather than a maximum (NOTES 171/173):

  * a TURBO or a mushroom reads far ABOVE the ceiling - the boost start
    alone put 1050 into a 50cc run topping out at 864, and a mushroom put
    2009 into a 100cc one.  A boost decays through each speed in a frame
    or two; the cap is where the kart parks.
  * a count the driver passed through briefly reads BELOW it, top speed
    never having been reached.  Those are reported, and ignored.
"""
import sys, collections

DWELL = 8       # frames at one exact speed before it is a plateau
MINF  = 40      # frames at a coin count before it is judged
NAMES = ["Mario", "Luigi", "Bowser", "Princess", "DK Jr", "Koopa", "Toad", "Yoshi"]
CLASS = [("50cc", -0x80), ("100cc", 0), ("150cc", +0xA0)]
ROM   = "rom/smk_usa.sfc"

def table():
    d = open(ROM, "rb").read()
    if len(d) % 1024 == 512: d = d[512:]
    off = ((0x81 & 0x3F) << 16 | 0x8000) & (len(d) - 1)      # src/rom.c:8
    return [d[off+i*2] | d[off+i*2+1] << 8 for i in range(8)]

def load(path):
    out = []
    for line in open(path):
        if not line[:1].isdigit(): continue
        p = line.split(',')
        try: out.append((int(p[6]), int(p[7])))              # pspd, pcoin
        except (ValueError, IndexError): pass
    return out

def plateau(speeds):
    h = collections.Counter(speeds)
    hits = [s for s, n in h.items() if n >= DWELL]
    return max(hits) if hits else None

def report(path, base):
    rows = load(path)
    if not rows: print(f"{path}: nothing"); return
    by = collections.defaultdict(list)
    for spd, c in rows:
        if c <= 99: by[c].append(spd)
    obs = {c: plateau(v) for c, v in by.items() if len(v) >= MINF}
    obs = {c: v for c, v in obs.items() if v is not None}
    top = max(obs.values()) if obs else 0
    print(f"\n=== {path}   {len(rows)} race frames")
    print("  coins  frames  plateau")
    for c in sorted(by):
        p = obs.get(c)
        print(f"  {c:5d} {len(by[c]):7d} {p if p is not None else '      -':>8}"
              + ("" if p is None else ("   <- the cap" if p >= top-1 else "   below the cap")))
    # every row of the ROM that allows this plateau, +-1 for the last step
    fits = [f"{NAMES[i]} {cn}" for i, b in enumerate(base) for cn, adj in CLASS
            if abs(b + adj + 8*10 - top) <= 1]
    print(f"  highest plateau {top}")
    print(f"  ROM rows that allow it (base + class + 8*10):  "
          + (", ".join(fits) if fits else "NONE - the rule does not hold here"))

if __name__ == "__main__":
    base = table()
    for p in sys.argv[1:] or ["tmp/flag.csv"]:
        report(p, base)
