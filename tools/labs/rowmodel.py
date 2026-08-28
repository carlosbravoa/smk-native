#!/usr/bin/env python3
"""$80ADA0, the AI row chooser, checked against the real game frame by frame.

    tools/labs/rowmodel.py tmp/rowlog2.csv

rowlog.lua logs every input the routine reads and the $C8 it produced, for
all eight karts of a recorded race, so the port is not judged by how the
race feels: the model runs on the logged inputs and its answer is compared
with the ROM's.  Where it disagrees is where the reading is still wrong -
which is how three separate misreadings were caught (NOTES 174).

What the routine actually keys on, and none of it was in our port:

  $10 bit 15   IS THIS KART THE HUMAN PLAYER.  Set on kart 0, 100% of a
               recorded race, never on an AI.  Every branch asks it of the
               NEIGHBOUR, so the AI picks its speed row from where the
               player is.  That is the rubber band.
  $010E        T[rank] -> kart block.  $010C,y is therefore the kart AHEAD
               and $0110,y the kart BEHIND (y = rank*2).
  $C1 & 7      a per-kart skill index, NOT the engine class - it read
               5,3,4,4,5,5,5,5 across one race.  It picks the row of
               $80AF0F, which our port indexes by class instead.
  $80AEB9      far behind, a midfield AI ADOPTS THE ROW OF THE KART AHEAD.

$80AF0F holds only five usable rows: row 5 onwards is the code of $80AF5F.
A kart with $C1&7 = 5 therefore reads its threshold out of the table and
gets an effectively infinite one.  That is the original's own overrun and
it is reproduced here by reading the bytes verbatim, not by pretending the
table has eight rows.
"""
import sys, math, collections, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rowparse import load

def catchup_table(path="rom/smk_usa.sfc"):
    d = open(path, "rb").read()
    if len(d) % 1024 == 512: d = d[512:]
    off = (((0x80 & 0x3F) << 16) | 0xAF0F) & (len(d) - 1)
    return [d[off + i] | d[off + i + 1] << 8 for i in range(0, 128, 2)]

CATCH = catchup_table()
def limit(c1, rank2):                      # $80AEFC
    i = ((c1 & 7) << 4) + rank2
    return CATCH[i >> 1] if (i >> 1) < len(CATCH) else 0xFFFF

def run(path):
    hdr, rows = load(path)
    ix = {n: i for i, n in enumerate(hdr)}
    g = lambda r, k, f: r[ix[f'k{k}{f}']]
    start = next(i for i, r in enumerate(rows)
                 if max(g(r, k, 'spd') for k in range(8)) > 0)
    rows = rows[start:]
    agree = 0; total = 0
    bad = collections.Counter()
    for r in rows:
        T = {}                                   # rank -> kart, from $010E
        for k in range(8): T[g(r, k, 'E6') // 2] = k
        dist = lambda a, b: math.hypot(g(r, a, 'x') - g(r, b, 'x'),
                                       g(r, a, 'y') - g(r, b, 'y'))
        isp  = lambda k: bool(g(r, k, '10') & 0x8000)
        s04 = s06 = 0                            # $04/$06 persist across calls
        for k in range(8):
            if isp(k): continue
            got, s04, s06 = row_for(r, k, g, T, dist, isp, s04, s06)
            total += 1
            if got == g(r, k, 'C8'): agree += 1
            else: bad[(g(r, k, 'C8'), got)] += 1
    print(f"  {path}: {agree}/{total} agree ({100*agree/total:.2f}%)")
    for (w, gt), n in bad.most_common(6):
        print(f"    ROM ${w:02X} -> model ${gt:02X}   {n}")
    return agree / total

def row_for(r, k, g, T, dist, isp, s04, s06):
    """$80ADA0.  Returns (row, $04, $06)."""
    if g(r, k, '84') != 0 or (g(r, k, '10') & 0x0020):
        return 0x18, s04, s06                                  # $80ADB0
    y2 = g(r, k, 'E6')                                         # rank * 2
    y  = y2 // 2
    p00 = g(r, k, 'DA')                       # entry A; hypothesis: my $DA
    if g(r, k, 'E2') & 0x0002:                # $80ADC0, never seen in 1P
        return 0x08, s04, s06

    if y == 0:                                                 # $80ADE0
        x2 = T.get(1)
        if x2 is None: return 0x10, s04, s06
        if isp(x2):
            return (0x10 if dist(k, x2) >= 0x140 else 0x00), s04, s06
        if p00 < g(r, x2, 'DA'): return 0x10, s04, s06
        if (g(r, k, 'C1') & 7) == 0: return 0x08, s04, s06
        if dist(k, x2) >= limit(g(r, k, 'C1'), 0): return 0x00, s04, s06
        t2 = T.get(2)
        return (0x08 if (t2 is not None and isp(t2)) else 0x00), s04, s06

    if y == 7:                                                 # $80AE23
        a = T.get(6)
        if a is None: return 0x10, s04, s06
        if isp(a):
            return (0x00 if dist(k, a) < 0x80 else 0x08), s04, s06
        if g(r, a, 'DA') < p00: return 0x08, s04, s06
        return ((0x10 if dist(k, a) < limit(g(r, a, 'C1'), y2) else 0x00),
                s04, s06)

    beh, ah = T.get(y + 1), T.get(y - 1)                       # $80AE4C
    if beh is not None and isp(beh):
        if ah is not None:
            if g(r, ah, 'C8') == 0x10: return 0x10, s04, s06
            if g(r, ah, 'DA') >= p00: return 0x10, s04, s06
        if dist(k, beh) < 0x140: return 0x00, s04, s06
        if s04 >= p00: return 0x00, s04, s06                   # $04 is STALE
        return 0x10, s04, s06
    if beh is None or ah is None: return 0x10, s04, s06
    s06 = g(r, beh, 'DA')                                      # $80AE79
    if isp(ah):
        if p00 < s06: return 0x10, s04, s06
        return (0x08 if dist(k, ah) >= 0x80 else 0x00), s04, s06
    s04 = g(r, ah, 'DA')                                       # $80AE97
    if p00 < s06: return 0x10, s04, s06
    if s04 < p00: return 0x08, s04, s06
    if dist(k, ah) < limit(g(r, ah, 'C1'), y2): return 0x10, s04, s06
    return g(r, ah, 'C8'), s04, s06                            # $80AEB9

if __name__ == "__main__":
    run(sys.argv[1] if len(sys.argv) > 1 else 'tmp/rowlog2.csv')
