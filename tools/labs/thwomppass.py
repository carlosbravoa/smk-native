#!/usr/bin/env python3
"""At what Thwomp height did the PLAYER pass, and at what height did they hit?

    tools/labs/thwomppass.py tmp/thwomp_run.csv

Seven teleport rigs failed to answer this (NOTES 176).  A person driving
answers it in one lap: thwomplog.lua logs all four object blocks' height
and the kart's own impact state every frame, so the two populations -
"was close to a Thwomp and crashed" and "was close to a Thwomp and drove
on" - separate by height with no rig in the way.

The crash is $10 bit $0002 with $AC = $16 (NOTES 072).
"""
import sys, math, collections

NEAR = int(sys.argv[2]) if len(sys.argv) > 2 else 24   # world units = "close"

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'tmp/thwomp_run.csv'
    rows = []
    for line in open(path):
        if not line[:1].isdigit(): continue
        p = line.split(',')
        if len(p) != 19: continue
        try:
            f, px, py, spd = int(p[0]), int(p[1]), int(p[2]), int(p[3])
            p10, pac, pa0 = int(p[4], 16), int(p[5], 16), int(p[6], 16)
        except ValueError:
            continue
        objs = []
        for i in range(4):
            objs.append(tuple(int(v) for v in p[7 + i*3: 10 + i*3]))
        rows.append((f, px, py, spd, p10, pac, pa0, objs))
    if not rows:
        print("no race frames in", path); return
    print(f"  {len(rows)} race frames")

    hit_z, pass_z = [], []
    for i, (f, px, py, spd, p10, pac, pa0, objs) in enumerate(rows):
        crashing = bool(p10 & 0x0002) or pac == 0x16
        for (ox, oy, oz) in objs:
            if ox == 0 and oy == 0: continue
            d = math.hypot(px - ox, py - oy)
            if d > NEAR: continue
            (hit_z if crashing else pass_z).append(oz)
    hit_z.sort(); pass_z.sort()
    print(f"  within {NEAR} units of a Thwomp: {len(hit_z)} crashing frames,"
          f" {len(pass_z)} clear frames")
    if hit_z:
        print(f"    CRASHED at z {hit_z[0]}..{hit_z[-1]}  median {hit_z[len(hit_z)//2]}")
    if pass_z:
        print(f"    PASSED  at z {pass_z[0]}..{pass_z[-1]}  median {pass_z[len(pass_z)//2]}")
    if hit_z and pass_z:
        best = None
        for th in range(0, 13000, 32):
            err = sum(1 for z in hit_z if z >= th) + sum(1 for z in pass_z if z < th)
            if best is None or err < best[0]: best = (err, th)
        err, th = best
        n = len(hit_z) + len(pass_z)
        print(f"\n  the height that best separates them: {th}"
              f"   ({n - err}/{n} frames on the right side)")
        print(f"  one screen pixel is ~97.8 of these units, so that is"
              f" ~{th/97.8:.0f} px; our port uses 2048 (~21 px)")

main()
