#!/usr/bin/env python3
"""How much does a kart WEAVE?

    tools/labs/aiweave.py <log> [first-frame] [last-frame]

The log is a stream of `fp <frame>,x,y,angle,x,y,angle,...` rows - eight
karts, 16.16 positions - which both sides produce: the running game
through `tools/labs/mame/fieldpos.lua`, and the port under
SMK_FIELD_TRACE.  Same shape, same columns, so the answer is a
comparison rather than an impression.

"Jitter in X" is a wobble about the line the kart is actually taking, so
that is what is measured: the direction of travel from consecutive
positions, how much it changes per frame, how often it CHANGES SIGN (a
weave is a reversal, a corner is not), and the lateral distance from the
kart's own smoothed path.
"""
import sys, math

ONE = 65536.0


def load(path):
    rows = []
    with open(path) as f:
        for line in f:
            if not line.startswith("fp "):
                continue
            v = line[3:].strip().split(",")
            if len(v) < 33:
                continue
            fr = int(v[0])
            karts = []
            for k in range(8):
                x = int(v[1 + k * 4]) / ONE
                y = int(v[2 + k * 4]) / ONE
                a = int(v[3 + k * 4])
                t = int(v[4 + k * 4])
                karts.append((x, y, a, t))
            rows.append((fr, karts))
    return rows


def weave(track):
    """track: [(x, y)] for one kart. Returns the stats."""
    # direction of travel, frame to frame
    dirs = []
    for i in range(1, len(track)):
        dx = track[i][0] - track[i - 1][0]
        dy = track[i][1] - track[i - 1][1]
        if dx * dx + dy * dy < 1e-6:
            dirs.append(None)
            continue
        dirs.append(math.atan2(dy, dx))
    turn = []
    for i in range(1, len(dirs)):
        if dirs[i] is None or dirs[i - 1] is None:
            turn.append(0.0)
            continue
        d = dirs[i] - dirs[i - 1]
        while d > math.pi:  d -= 2 * math.pi
        while d < -math.pi: d += 2 * math.pi
        turn.append(math.degrees(d))
    if not turn:
        return None
    flips = 0
    for i in range(1, len(turn)):
        if turn[i] * turn[i - 1] < 0 and abs(turn[i]) > 0.05 and abs(turn[i - 1]) > 0.05:
            flips += 1
    mean = sum(abs(t) for t in turn) / len(turn)
    rms = math.sqrt(sum(t * t for t in turn) / len(turn))
    # lateral wobble: distance from a 15-frame moving average of the path
    W = 15
    lat = []
    for i in range(W, len(track) - W):
        ax = sum(p[0] for p in track[i - W:i + W + 1]) / (2 * W + 1)
        ay = sum(p[1] for p in track[i - W:i + W + 1]) / (2 * W + 1)
        lat.append(math.hypot(track[i][0] - ax, track[i][1] - ay))
    wob = sum(lat) / len(lat) if lat else 0.0
    peak = max(lat) if lat else 0.0
    return dict(mean=mean, rms=rms, flips=100.0 * flips / len(turn),
                wobble=wob, peak=peak, n=len(turn))


def main():
    path = sys.argv[1]
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
    rows = [r for r in load(path) if lo <= r[0] <= hi]
    if len(rows) < 60:
        print("only %d frames in range" % len(rows)); return 2
    print("%s: %d frames (%d..%d)" % (path, len(rows), rows[0][0], rows[-1][0]))
    print("  kart   turn/frame   rms    reversals/100f   wobble px   peak px   "
          "target moves  mean step  past snap")
    for k in range(8):
        tr = [(r[1][k][0], r[1][k][1]) for r in rows]
        if max(p[0] for p in tr) - min(p[0] for p in tr) < 4 and \
           max(p[1] for p in tr) - min(p[1] for p in tr) < 4:
            continue                        # parked: nothing to say
        w = weave(tr)
        if not w:
            continue
        # and the TARGET the steering is chasing: how often it moves, and
        # how far, is what decides whether the kart can hold a line
        tg = [r[1][k][3] for r in rows]
        moves, step, big = 0, 0.0, 0
        for i in range(1, len(tg)):
            d = (tg[i] - tg[i - 1]) & 0xFFFF
            if d > 32768: d -= 65536
            if d:
                moves += 1
                step += abs(d)
                if abs(d) >= 0x200: big += 1
        print("   %d     %7.3f  %7.3f      %8.1f      %7.3f   %7.3f   "
              "%6.1f%%  %6.2f  %5.1f%%"
              % (k, w['mean'], w['rms'], w['flips'], w['wobble'], w['peak'],
                 100.0 * moves / max(1, len(tg) - 1),
                 (step / moves * 360.0 / 65536.0) if moves else 0.0,
                 100.0 * big / max(1, len(tg) - 1)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
