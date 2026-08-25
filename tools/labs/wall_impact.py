"""What a wall really does to a moving kart.

Earlier attempts failed two ways: filling the whole map with wall put
the kart INSIDE a solid, and aiming the kart by writing its heading did
not stick, because under player control the game rewrites the target
angle from the pad every frame (NOTES 044).

So: leave the kart driving normally, and paint a WALL TILE into the
tilemap cells directly ahead of it.  Tile 240 has class $80 on this
track (the barrier blocks); the surface table is untouched.
"""
import math
from lab import Lab, log, P1

L = Lab()
if not L.pace():
    log("could not reach pace (%d)" % L.speed())
    raise SystemExit

x, y = L.pos()
a = L.heading() * 2 * math.pi / 65536.0
dx, dy = math.sin(a), -math.cos(a)
log("kart at (%d,%d) heading %04X speed %d" % (x, y, L.heading(), L.speed()))

# class of tile 240, to confirm it is the barrier class
log("tile 240 class = $%02X" % L.b.wram[0x0B00 + 240])

# paint a wall band across the path, 70..110 px ahead, 5 cells wide
saved = {}
for d in range(70, 110, 8):
    for w in range(-3, 4):
        px = int(x + dx * d - dy * w * 8)
        py = int(y + dy * d + dx * w * 8)
        if not (0 <= px < 1024 and 0 <= py < 1024):
            continue
        idx = 0x10000 + (py // 8) * 128 + (px // 8)
        saved[idx] = L.b.wram[idx]
        L.b.wram[idx] = 240
log("painted %d wall cells across the path" % len(saved))

x0, y0 = L.pos()
log("    f   spd     vx     vy    fwd    lat  state")
for f in range(45):
    L.frame(0x80)                      # straight, throttle held
    cx, cy = L.pos()
    fwd = (cx - x0) * dx + (cy - y0) * dy
    lat = (cx - x0) * dy - (cy - y0) * dx
    log("  %3d  %4d %6d %6d %6.1f %6.1f  %04X"
        % (f, L.speed(), L.s16(L.w(P1 + 0x22)), L.s16(L.w(P1 + 0x24)),
           fwd, lat, L.w(P1 + 0x10)))

for idx, v in saved.items():
    L.b.wram[idx] = v
