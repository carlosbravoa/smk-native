#!/usr/bin/env python3
"""The jitter a PLAYER sees: an AI kart's drawn screen x, frame by frame.

    tools/labs/aixjit.py <log> [first] [last]

"They jitter a lot in the X axis" is about pixels on the screen, not about
world coordinates, so this measures pixels on the screen.  Both logs -
the running game's (tools/labs/mame/fieldpos.lua) and the port's
(SMK_FIELD_TRACE) - carry every kart's position and the player's, so the
SAME projection can be run over both and the difference that remains is
the simulation's, not the renderer's.

What it reports, per kart, over the frames where the kart is close enough
to see (inside the near range):

  wobble      mean |second difference| of screen x, in SNES pixels: how
              much the kart moves ACROSS the screen in a way that is not
              steady motion.  A kart drifting smoothly scores ~0.
  shakes/s    times a second the screen-x movement reverses by more than
              half a pixel - a reversal you can see.
"""
import sys, math

ONE = 65536.0
PROJ_K, PROJ_H, PROJ_LES, CAM_TRAIL, CAM_LEAD = 4972.0, 20.36, 256.0, 61.0, 0x00C0
W = 256.0                      # SNES pixels across


def load(path):
    rows = []
    for line in open(path):
        if not line.startswith("fp "):
            continue
        v = line[3:].strip().split(",")
        if len(v) < 33:
            continue
        rows.append((int(v[0]),
                     [(int(v[1 + k * 4]) / ONE, int(v[2 + k * 4]) / ONE,
                       int(v[3 + k * 4])) for k in range(8)]))
    return rows


def screen_x(cam_x, cam_y, cam_a, wx, wy):
    """the renderer's own law (src/mode7.c), in SNES pixels"""
    sa, ca = math.sin(cam_a), math.cos(cam_a)
    dx, dy = wx - cam_x, wy - cam_y
    zf = dx * ca + dy * sa
    xr = -dx * sa + dy * ca
    d = zf + CAM_TRAIL
    if d < 12.0:
        return None, None
    return W * 0.5 + xr * PROJ_LES / d, d


def main():
    path = sys.argv[1]
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
    rows = [r for r in load(path) if lo <= r[0] <= hi]
    if len(rows) < 60:
        print("only %d frames in range" % len(rows)); return 2
    print("%s: %d frames" % (path, len(rows)))
    print("  kart   frames in view   wobble px   shakes/s   max step px")
    for k in range(1, 8):
        xs, dep = [], []
        for fr, ks in rows:
            px, py, pa = ks[0]
            # the camera is the player's kart plus the ROM's own $C0 lead
            a = ((pa + CAM_LEAD) & 0xFFFF) * 2.0 * math.pi / 65536.0 - math.pi / 2
            sx, d = screen_x(px, py, a, ks[k][0], ks[k][1])
            xs.append(sx); dep.append(d)
        # only where the kart is close enough to see it move
        run, best = [], []
        for i in range(len(xs)):
            if xs[i] is not None and dep[i] < 220 and 0 < xs[i] < W:
                run.append(i)
            else:
                if len(run) > len(best): best = run
                run = []
        if len(run) > len(best): best = run
        if len(best) < 90:
            continue
        seq = [xs[i] for i in best]
        d1 = [seq[i] - seq[i - 1] for i in range(1, len(seq))]
        d2 = [abs(d1[i] - d1[i - 1]) for i in range(1, len(d1))]
        shakes = 0
        for i in range(1, len(d1)):
            if d1[i] * d1[i - 1] < 0 and abs(d1[i] - d1[i - 1]) > 0.5:
                shakes += 1
        print("   %d        %5d          %7.3f    %7.2f     %7.2f"
              % (k, len(best), sum(d2) / len(d2),
                 60.0 * shakes / len(d1), max(d2)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
