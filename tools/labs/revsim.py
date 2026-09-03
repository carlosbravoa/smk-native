#!/usr/bin/env python3
"""$80:B121, the IN-RACE engine rev, re-simulated against the real game.

    tools/labs/revsim.py tools/labs/mame/gv_demolog.csv

The rev $C2 is what the engine note is made of: $80:9643 sends $42 to the
APU every frame and $42 is $C2 >> 8 (measured, NOTES 265).  The port used
a FIT from speed instead - 20 + speed*0.048, clamped at 63 - which is
flat once the kart is fast, so our engine stops climbing exactly where
the user says the original keeps climbing.

This runs the disassembled law on the game's own logged inputs (the pad
$C4, the surface type $B0, the flags $E2) and diffs the result against
the game's own $C2, frame by frame.  A model that matches here is the
game's; anything else is a fit.

    $80B121  LDA #$4000 / BIT $C4,x       Y held?  -> $0E26
             BMI ...                      B held?  -> the ramp below
                                          neither  -> $0E26
    $80B14C  type >= $14                  -> $1000 gate, then $0E28
             $E2 & 4 (drifting)           -> (ceiling-$1800) gate, $0E2A
    $80B15C  rev < $2000 -> $0E22 else $0E24
    $80B169  rev += d, floor $0100, ceiling $0E20
"""
import sys, csv, collections

# $81:EFE7, by engine class: $0030 == 0 (50cc) takes the first row, every
# other class the second ($81:EE07 chooses).  ceiling, +under, +over,
# coast, off-road, drift.
ROWS = {
    0: (0x3FFF,  0x0120,  0x0080, -0x0200, -0x0300, -0x0100),
    1: (0x5FFF,  0x0200,  0x0040, -0x0280, -0x0380, -0x0180),
}


def step(rev, pad, typ, e2, row):
    ceil, up_lo, up_hi, coast, off, drift = row
    def ramp(r):
        return up_lo if r < 0x2000 else up_hi
    if pad & 0x4000:                       # $80B124: Y held
        d = coast
    elif pad & 0x8000:                     # B held
        if typ >= 0x14:                    # $80B14C -> $80B140
            d = ramp(rev) if rev < 0x1000 else off
        elif e2 & 0x0004:                  # $80B153 -> $80B12F
            d = ramp(rev) if (ceil - 0x1800) >= rev else drift
        else:
            d = ramp(rev)
    else:
        d = coast
    v = rev + d
    if v < 0x0100:
        v = 0x0100                         # $80B173
    if v >= ceil:
        v = ceil                           # $80B17B
    return v


def main():
    path = sys.argv[1]
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            if r["kart"].lower() != "1000":
                continue
            rows.append(r)
    if not rows:
        print("no P1 rows in", path); return 2
    cls = int(rows[0]["g30"]) // 2
    row = ROWS[0 if cls == 0 else 1]
    print("%s: %d frames, engine class %d -> row %s"
          % (path, len(rows), cls, " ".join("$%04X" % (v & 0xFFFF) for v in row)))

    # THE CADENCE, measured: $C2 moves once every EIGHT frames, on one
    # phase - 707 of 763 changes in this run are exactly 8 apart and 721
    # of them land on frames = 1 (mod 8).  The game walks its eight kart
    # blocks one per frame, so each kart's rev is built every eighth.
    # (The gaps of 2 are the countdown's own routine, $80:95BB.)
    ph = collections.Counter()
    for i in range(1, len(rows)):
        if int(rows[i]["fC2"]) != int(rows[i - 1]["fC2"]):
            ph[int(rows[i]["frame"]) % 8] += 1
    phase = ph.most_common(1)[0][0]
    print("  cadence: every 8 frames, on frame %% 8 == %d" % phase)

    # the RACE only: the countdown has its own routine ($80:95BB, every
    # SECOND frame at +$C0), which this is not
    i0 = next(i for i, r in enumerate(rows) if int(r["fEA"]) > 0)
    rows = rows[i0:]
    print("  race from log index %d" % i0)

    # start where the game's own rev starts, and walk its inputs
    rev = int(rows[0]["fC2"])
    diff = collections.Counter()
    worst = (0, 0, 0)
    for i in range(1, len(rows)):
        cur = rows[i]
        if int(cur["frame"]) % 8 == phase:
            # THIS frame's inputs, not the previous one's: the rev is
            # built after the control stage has composed $C4
            rev = step(rev, int(cur["fC4"]), int(cur["fB0"]), int(cur["fE2"]), row)
        want = int(cur["fC2"])
        d = rev - want
        diff[d] += 1
        if abs(d) > abs(worst[0]):
            worst = (d, int(cur["frame"]), want)
    n = sum(diff.values())
    exact = diff[0]
    print("  exact on %d of %d frames (%.1f%%)" % (exact, n, 100.0 * exact / n))
    near = sum(v for k, v in diff.items() if abs(k) <= 0x100)
    print("  within $0100 on %d (%.1f%%)" % (near, 100.0 * near / n))
    print("  worst: %+d at frame %d (game had $%04X)" % (worst[0], worst[1], worst[2]))
    top = diff.most_common(6)
    print("  commonest differences: " + " ".join("%+d:%d" % kv for kv in top))
    # and what the note itself does
    g = [int(r["fC2"]) >> 8 for r in rows]
    print("  the game's own note ($C2 >> 8): min %d max %d" % (min(g), max(g)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
