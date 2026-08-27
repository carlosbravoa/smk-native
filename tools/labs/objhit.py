"""What the GAME does when the kart hits a track object.

The user drove into a Thwomp on Rainbow Road and could not get out; in the
real game a few shoves while holding a direction frees you.  Our port
reflects the velocity and leaves the HEADING alone, so the kart bounces
back, control returns still pointing at the object, and it drives straight
back in - for ever.  NOTES 072 measured the pipe response but pinned the
heading, so the one thing that matters here was never measured.

This teleports the kart just short of a LIVE object block, holds the
throttle (and optionally a direction), and logs the fields the crash
touches across the impact and for a while after.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

P1 = 0x1000
STEER = int(os.environ.get("STEER", "0"))      # -1 left, +1 right, 0 none
SPEED = int(os.environ.get("SPEED", "480"))
BACK  = int(os.environ.get("BACK", "26"))      # px short of the object
FRAMES= int(os.environ.get("FRAMES", "150"))

L = Lab(settle=60)
w = L.b.wram

def s16(v): return v - 65536 if v > 32767 else v
def rd(a): return w[a] | w[a+1] << 8

# ---- find a live object block -------------------------------------------
# $1800.. holds the spawned slots; +$00/+$02 read as world x/y.
# the stride is not assumed: dump every coordinate-like word pair and let
# the ones that sit near the kart's own track identify themselves
raw = []
for a in range(0x1800, 0x2000, 2):
    x, y = rd(a), rd(a + 2)
    if 0 < x < 1024 and 0 < y < 1024:
        raw.append((a, x, y))
log("coordinate-like word pairs in $1800-$2000: %d" % len(raw))
for a, x, y in raw[:40]:
    log("   $%04X  (%4d,%4d)" % (a, x, y))
cands = raw
if not cands:
    log("no object block found - nothing to hit"); sys.exit(1)

# take the one nearest the kart so the teleport stays on the road
kx, ky = L.pos()
base, ox, oy = min(cands, key=lambda c: (c[1]-kx)**2 + (c[2]-ky)**2)
log("hitting the block at $%04X, world (%d,%d); kart was at (%d,%d)"
    % (base, ox, oy, kx, ky))

# ---- place the kart north of it, pointing south, at SPEED ---------------
HEAD = 0x8000                        # 0 = north, so $8000 = south
L.sw(P1 + 0x18, ox); w[P1 + 0x16] = 0; w[P1 + 0x17] = 0
L.sw(P1 + 0x1C, oy - BACK); w[P1 + 0x1A] = 0; w[P1 + 0x1B] = 0
L.sw(P1 + 0xA4, HEAD); L.sw(P1 + 0xA2, HEAD); L.sw(P1 + 0x2A, HEAD)
L.sw(P1 + 0xA8, 0)                   # no slip
L.sw(P1 + 0xEA, SPEED)
L.sw(P1 + 0x22, 0); L.sw(P1 + 0x24, SPEED)

pad = 0x80                            # B held
if STEER < 0: pad |= 0x02
if STEER > 0: pad |= 0x01
log("steer %d, speed %d, %d px back\n" % (STEER, SPEED, BACK))
log(" f    x    y  dist  spd    vx    vy   head  vang  vlag  $AC $A6  $10   $42 $5C")
for f in range(FRAMES):
    x, y = L.pos()
    dx, dy = x - ox, y - oy
    d = (dx*dx + dy*dy) ** 0.5
    log("%2d %4d %4d %5.1f %4d %5d %5d  %04X  %04X %5d   %02X  %02X %04X  %3d %2d"
        % (f, x, y, d, L.speed(), s16(rd(P1+0x22)), s16(rd(P1+0x24)),
           rd(P1+0xA4), rd(P1+0xA2), s16(rd(P1+0xA8)),
           w[P1+0xAC], w[P1+0xA6], rd(P1+0x10), w[P1+0x42], w[P1+0x5C]))
    L.frame(pad)
