#!/usr/bin/env python3
"""What changes when the player crosses the line, from a recorded race.

    tools/labs/finishwhat.py tmp/finish.csv

finishlog.lua dumps $0080-$00FF and the player's block $1000-$10FF every
frame from just before the flag.  This finds the crossing, then reports
which bytes behave differently after it - so the real finish sequence is
read off the game rather than designed (S27, NOTES 178).

The two questions it exists to answer: does the CAMERA leave the kart's
own heading (normally $94 - $A4 == 192 every frame, NOTES 083), and does
the player get a celebration STATE.
"""
import sys

def load(path):
    rows = []
    for line in open(path):
        if not line[:1].isdigit(): continue
        p = line.strip().split(',')
        if len(p) != 4: continue
        f, lap = int(p[0]), int(p[1], 16)
        dp = bytes.fromhex(p[2])        # $0080..$00FF
        pk = bytes.fromhex(p[3])        # $1000..$10FF
        rows.append((f, lap, dp, pk))
    return rows

def main():
    rows = load(sys.argv[1] if len(sys.argv) > 1 else 'tmp/finish.csv')
    if not rows: print("nothing"); return
    print(f"  {len(rows)} frames, lap byte {rows[0][1]:#04x} -> {rows[-1][1]:#04x}")
    cross = next((i for i, r in enumerate(rows) if r[1] >= 0x85), None)
    if cross is None:
        print("  the recording never reaches the last crossing"); return
    print(f"  crossing at frame {rows[cross][0]} (index {cross})")

    w = lambda b, o: b[o] | b[o+1] << 8
    print("\n  camera vs heading   ($94 - $A4 is 192 all through a normal race)")
    for i in range(max(0, cross - 60), min(len(rows), cross + 300), 20):
        f, lap, dp, pk = rows[i]
        cam = w(dp, 0x94 - 0x80)
        a4  = w(pk, 0xA4)
        pose = w(pk, 0x2A)
        spd = w(pk, 0xEA)
        if spd > 32767: spd -= 65536
        print(f"    {'>' if i >= cross else ' '} f{f:5d} lap ${lap:02X}  $94 ${cam:04X}"
              f"  $A4 ${a4:04X}  diff {(cam - a4) & 0xFFFF:5d}"
              f"  pose ${pose:04X}  spd {spd:5d}")

    # which bytes are steady before and different after
    print("\n  player-block bytes that only start moving AFTER the crossing:")
    def spread(o, a, b, which):
        vals = set(r[which][o] for r in rows[a:b])
        return vals
    for o in range(0x100):
        before = spread(o, max(0, cross - 90), cross, 3)
        after  = spread(o, cross, min(len(rows), cross + 240), 3)
        if len(before) == 1 and len(after) > 1:
            seq = [rows[i][3][o] for i in range(cross, min(len(rows), cross + 240), 12)]
            print(f"    $10{o:02X}  was ${next(iter(before)):02X}, then "
                  + " ".join(f"{v:02X}" for v in seq[:14]))
    print("\n  direct-page bytes that only start moving AFTER the crossing:")
    for o in range(0x80):
        before = spread(o, max(0, cross - 90), cross, 2)
        after  = spread(o, cross, min(len(rows), cross + 240), 2)
        if len(before) == 1 and len(after) > 1:
            seq = [rows[i][2][o] for i in range(cross, min(len(rows), cross + 240), 12)]
            print(f"    $00{0x80+o:02X}  was ${next(iter(before)):02X}, then "
                  + " ".join(f"{v:02X}" for v in seq[:14]))

main()
