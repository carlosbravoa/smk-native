#!/usr/bin/env python3
"""Where does a driver's speed actually go?

    tools/labs/speedloss.py <demolog.csv> [kart-block]

The input is `tools/labs/mame/demolog.lua`'s per-frame log, which carries
everything needed to attribute a loss without guessing: the pad word $C4,
the slide state $A6, the drive state $AC, the surface TYPE $B0 the game
itself read, the flags $10 (bit 12 = hit a wall) and the target $D6.

It answers the question "why is top speed hard to hold" with numbers: how
much of the race is spent at the ceiling, how many separate losses there
are, what each one cost, and how long the kart then spends climbing back.
"""
import sys, collections

def load(path, block="1000"):
    rows = []
    with open(path) as f:
        hdr = f.readline().strip().split(",")
        ix = {n: i for i, n in enumerate(hdr)}
        for line in f:
            v = line.strip().split(",")
            if len(v) != len(hdr):
                continue
            if v[ix["kart"]].lower() != block.lower():
                continue
            rows.append({n: int(v[i]) for n, i in ix.items() if n != "kart"})
    return rows

def s16(v):
    return v - 65536 if v > 32767 else v

TYPE_NAME = {0: "road", 1: "road", 2: "road", 3: "road", 4: "road", 5: "road",
             6: "road", 7: "road", 8: "road", 9: "road",
             10: "capped-10", 11: "capped-11", 12: "capped-12",
             13: "capped-13", 14: "capped-14", 15: "capped-15"}

def cause(r):
    a6, ac, c4 = r["fA6"], r["fAC"], r["fC4"]
    # a mushroom pins speed at $7E0 = 2016 and then bleeds it back down at
    # ~56/frame: that fall is the boost ending, not the driver lifting
    if r["fEA"] > r["fD6"] * 1.05 or ac == 0x10: return "mushroom decay"
    if a6 in (0x0A, 0x0C):  return "banana spin"
    if a6 == 0x1A:          return "shell/lightning"
    if a6 in (0x0E, 0x10):  return "SPIN-OUT (slide overrun)"
    if ac == 0x14:          return "hit (wall or kart)"
    if ac == 0x16:          return "crash decel"
    if r["f10"] & 0x1000:   return "wall contact"
    if (r["fB0"] & 0xF) >= 10: return "off-road (%s)" % TYPE_NAME.get(r["fB0"] & 0xF)
    if c4 & 0x4000:         return "braking (Y)"
    if not (c4 & 0x8000):   return "throttle released"
    return "unexplained"

def main():
    path = sys.argv[1]
    block = sys.argv[2] if len(sys.argv) > 2 else "1000"
    rows = load(path, block)
    if not rows:
        print("no rows for kart block", block); return 2
    # the race proper: from the first frame the kart moves
    i0 = next((i for i, r in enumerate(rows) if r["fEA"] > 0), 0)
    rows = rows[i0:]
    n = len(rows)
    spd = [r["fEA"] for r in rows]
    tgt = [r["fD6"] for r in rows]
    print("%s kart %s: %d frames (%.1f s) from frame %d" % (path, block, n, n / 60.0, i0))
    print("  base top $B4 %d..%d   target $D6 %d..%d   class $30 %d"
          % (min(r["fB4"] for r in rows), max(r["fB4"] for r in rows),
             min(tgt), max(tgt), rows[0]["g30"]))
    s = sorted(spd)
    print("  speed  p10 %d  p25 %d  p50 %d  p75 %d  p90 %d  p99 %d  max %d  mean %.1f"
          % (s[n//10], s[n//4], s[n//2], s[3*n//4], s[9*n//10], s[99*n//100], s[-1],
             sum(s)/n))
    at  = sum(1 for i in range(n) if spd[i] >= tgt[i])
    n95 = sum(1 for i in range(n) if spd[i] >= tgt[i] * 0.95)
    n90 = sum(1 for i in range(n) if spd[i] >= tgt[i] * 0.90)
    print("  at/above the target: %d (%.1f%%)   >=95%%: %d (%.1f%%)   >=90%%: %d (%.1f%%)"
          % (at, 100.0*at/n, n95, 100.0*n95/n, n90, 100.0*n90/n))

    # how long does the kart hold the ceiling at a stretch?
    runs, cur = [], 0
    for i in range(n):
        if spd[i] >= tgt[i] * 0.98:
            cur += 1
        elif cur:
            runs.append(cur); cur = 0
    if cur: runs.append(cur)
    runs.sort(reverse=True)
    print("  stretches at >=98%% of the target: %d, longest %s frames (%.1f s), "
          "median %s" % (len(runs), runs[0] if runs else 0,
                         (runs[0] if runs else 0)/60.0,
                         runs[len(runs)//2] if runs else 0))

    # every loss episode: a fall of 40+ units, tagged by what the game was
    # doing at the steepest frame
    tally = collections.Counter()
    cost  = collections.Counter()
    episodes = []
    i = 1
    while i < n:
        if spd[i] < spd[i-1]:
            j, lo = i, spd[i]
            while j + 1 < n and spd[j+1] <= spd[j]:
                j += 1; lo = spd[j]
            drop = spd[i-1] - lo
            if drop >= 40:
                worst = max(range(i, j+1), key=lambda t: spd[t-1] - spd[t])
                c = cause(rows[worst])
                tally[c] += 1; cost[c] += drop
                # recovery: frames until back within 2% of target
                rec = 0
                t = j
                while t < n and spd[t] < tgt[t] * 0.98:
                    rec += 1; t += 1
                episodes.append((i, drop, c, rec))
            i = j + 1
        else:
            i += 1
    print("  losses of 40+ units: %d, %d units total, %.1f%% of the race spent below 98%%"
          % (len(episodes), sum(e[1] for e in episodes),
             100.0 * sum(1 for i in range(n) if spd[i] < tgt[i]*0.98) / n))
    print("  by cause:")
    for c, k in tally.most_common():
        ep = [e for e in episodes if e[2] == c]
        print("    %-26s %3d losses, %6d units, mean %5.1f, mean recovery %5.1f frames"
              % (c, k, cost[c], cost[c]/k, sum(e[3] for e in ep)/len(ep)))
    print("  the ten biggest:")
    for f, d, c, rec in sorted(episodes, key=lambda e: -e[1])[:10]:
        print("    f%-6d -%4d  %-26s recovery %4d frames" % (f, d, c, rec))

    # what fraction of frames is the kart in each state at all?
    st = collections.Counter("%02X" % r["fA6"] for r in rows)
    print("  slide state $A6: " + " ".join("%s:%d(%.1f%%)" % (k, v, 100.0*v/n)
                                           for k, v in st.most_common()))
    ty = collections.Counter(r["fB0"] & 0xF for r in rows)
    print("  surface type $B0: " + " ".join("%d:%d(%.1f%%)" % (k, v, 100.0*v/n)
                                            for k, v in ty.most_common()))
    lag = [abs(s16(r["fA8"])) for r in rows]
    print("  |$A8| (velocity lag): nonzero on %d frames (%.1f%%), max %d (%.1f deg), "
          "mean %.0f" % (sum(1 for v in lag if v), 100.0*sum(1 for v in lag if v)/n,
                         max(lag), max(lag)*360.0/65536, sum(lag)/n))
    return 0

if __name__ == "__main__":
    sys.exit(main())
