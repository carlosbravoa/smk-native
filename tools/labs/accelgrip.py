"""Acceleration and grip, measured on the ROM.

The question (the user's): in the original it is HARD to hold top speed for
long; in our port it is easy.  Is that acceleration (slow to get back to
the top after any loss) or grip (the top speed itself is unstable when you
steer)?  Both halves are measurable directly - the ROM is driven here with
a forced pad word on a surface we choose, so nothing else can contribute.

Every battery prints a CSV-ish table so the port's twin lab
(`tools/labs/accelgrip.c`) can be diffed against it line for line.

Pad word: $4219 = B $80, Left $02, Right $01;  $4218 = R $10, L $20, Y $40.
"""
import math, os, sys
from lab import Lab, log, P1

ONLY = os.environ.get("ONLY", "")          # "C" runs just the on-track battery
L = Lab()
snap = L.surface_snapshot()

CH = L.b.wram[0x1000 + 0xC0] if False else None


def st(a):
    return L.s16(L.w(P1 + a))


def fill_all(cls):
    """every tile class, walls included, becomes `cls` - a featureless plain"""
    for i in range(0xC0):
        L.b.wram[0x0B00 + i] = cls


def park(x=512, y=512):
    L.sw(P1 + 0x18, x); L.sw(P1 + 0x1A, 0)
    L.sw(P1 + 0x1C, y); L.sw(P1 + 0x1E, 0)


def zero_speed():
    L.sw(P1 + 0xE8, 0); L.sw(P1 + 0xEA, 0)
    L.sw(P1 + 0xEC, 0); L.sw(P1 + 0xEE, 0)


def state():
    return dict(spd=st(0xEA), a6=L.w(P1 + 0xA6), a8=st(0xA8), aa=st(0xAA),
                fa=st(0xFA), b2=st(0xB2), a4=L.w(P1 + 0xA4), ee=st(0xEE),
                ac=L.w(P1 + 0xAC), e2=L.w(P1 + 0xE2), typ=L.w(P1 + 0xB0))


log("character $C0 %02X   base top $B4 %d   target $D6 %d   coins %d   class $30 %d"
    % (L.b.wram[P1 + 0xC0], st(0xB4), st(0xD6),
       L.b.wram[0x0E00] | L.b.wram[0x0E01] << 8, L.w(0x0030)))

if ONLY == "C":
    exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "accelgrip_c.py")).read())
    sys.exit(0)

# ---------------------------------------------------------------- A
log("")
log("=== A. acceleration from a standstill, straight, plain road ($40) ===")
fill_all(0x40)
park()
zero_speed()
L.sw(P1 + 0xA8, 0); L.sw(P1 + 0xAA, 0); L.sw(P1 + 0xA6, 0)
accel_curve = []
for f in range(700):
    park()                       # position cannot matter on a featureless plain
    L.frame(0x80)                # B only
    accel_curve.append(st(0xEA))
top = max(accel_curve)
log("  top reached %d ($B4 %d, $D6 %d)" % (top, st(0xB4), st(0xD6)))
for frac in (0.5, 0.75, 0.9, 0.95, 0.99, 1.0):
    want = top * frac
    fr = next((i for i, s in enumerate(accel_curve) if s >= want), None)
    log("  %5.0f%% of top (%4.0f): frame %s" % (frac * 100, want, fr))
log("  per-frame gain by speed band:")
band = {}
prev = 0
for i, s in enumerate(accel_curve):
    band.setdefault(prev >> 6, []).append(s - prev)
    prev = s
for k in sorted(band):
    v = band[k]
    log("    speed %4d-%4d: gain %5.2f/frame over %3d frames"
        % (k * 64, k * 64 + 63, sum(v) / len(v), len(v)))
log("  trace: " + " ".join("%d:%d" % (i, accel_curve[i])
                           for i in (0, 10, 30, 60, 120, 180, 240, 300, 400, 500, 699)))

# ---------------------------------------------------------------- A2
log("")
log("=== A2. recovery: how long to climb back to the top from a loss ===")
for lost in (100, 200, 300, 400, 600):
    park(); zero_speed()
    L.sw(P1 + 0xEA, top - lost)
    L.sw(P1 + 0xA8, 0); L.sw(P1 + 0xAA, 0); L.sw(P1 + 0xA6, 0); L.sw(P1 + 0xFA, 0)
    n = None
    for f in range(900):
        park()
        L.frame(0x80)
        if st(0xEA) >= top and n is None:
            n = f
            break
    log("  from %4d (top-%3d): back to %d in %s frames (%.2f s)"
        % (top - lost, lost, top, n, (n or 0) / 60.0))

# ---------------------------------------------------------------- B
log("")
log("=== B. hold the top speed while STEERING (full lock, plain road) ===")


def corner(label, hi, lo=0, frames=400, spd=None):
    park(); zero_speed()
    L.sw(P1 + 0xA8, 0); L.sw(P1 + 0xAA, 0); L.sw(P1 + 0xA6, 0); L.sw(P1 + 0xFA, 0)
    L.sw(P1 + 0xEA, spd if spd is not None else top)
    rows = []
    for f in range(frames):
        park()
        L.frame(hi, lo)
        s = state()
        s['f'] = f
        rows.append(s)
    log("  -- %s" % label)
    log("      f   spd  $A6  $A8(deg)  $AA(deg)   $FA   $B2   $EE")
    for f in (0, 1, 2, 5, 10, 20, 40, 60, 90, 120, 180, 240, 300, frames - 1):
        if f >= len(rows):
            continue
        r = rows[f]
        log("    %3d  %4d   %02X  %6d(%5.1f) %6d(%5.1f) %6d %5d %5d"
            % (r['f'], r['spd'], r['a6'], r['a8'], r['a8'] * 360.0 / 65536,
               r['aa'], r['aa'] * 360.0 / 65536, r['fa'], r['b2'], r['ee']))
    sp = [r['spd'] for r in rows]
    log("     speed: start %d  min %d  final %d  mean %.1f   states seen %s"
        % (sp[0], min(sp), sp[-1], sum(sp) / len(sp),
           sorted(set("%02X" % r['a6'] for r in rows))))
    return rows


corner("B held + LEFT held, 400 frames", 0x82)
corner("B + LEFT + shoulder R held (power slide row)", 0x82, 0x10)
corner("B held, steering RELEASED (control)", 0x80)

log("")
log("=== B2. a human corner: hold left N frames, release N, repeat ===")
for hold in (10, 20, 40):
    park(); zero_speed()
    L.sw(P1 + 0xA8, 0); L.sw(P1 + 0xAA, 0); L.sw(P1 + 0xA6, 0); L.sw(P1 + 0xFA, 0)
    L.sw(P1 + 0xEA, top)
    sp = []
    for f in range(360):
        park()
        L.frame(0x80 | (0x02 if (f // hold) % 2 == 0 else 0))
        sp.append(st(0xEA))
    log("  hold/release %2d: min %4d  final %4d  mean %6.1f  frames at top %d/%d"
        % (hold, min(sp), sp[-1], sum(sp) / len(sp), sum(1 for s in sp if s >= top), len(sp)))

# ---------------------------------------------------------------- C
log("")
log("=== C. the real track: hold the throttle and steer down the flow field ===")
L.surface_restore(snap)
park(952, 756)
zero_speed()
L.sw(P1 + 0xA8, 0); L.sw(P1 + 0xAA, 0); L.sw(P1 + 0xA6, 0); L.sw(P1 + 0xFA, 0)
rows = []
for f in range(1800):
    x, y = L.pos()
    cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
    want = L.b.wram[0x14000 + cell] << 8
    d = (want - L.heading()) & 0xFFFF
    if d > 32768:
        d -= 65536
    pad = 0x80
    if d < -0x300:
        pad |= 0x02
    elif d > 0x300:
        pad |= 0x01
    L.frame(pad)
    x, y = L.pos()
    tile = L.b.wram[0x10000 + ((y >> 3) & 127) * 128 + ((x >> 3) & 127)]
    rows.append((st(0xEA), L.b.wram[0x0B00 + tile], L.w(P1 + 0xA6), st(0xA8)))
sp = [r[0] for r in rows]
sp.sort()
tgt = st(0xD6)
log("  frames %d   target $D6 %d" % (len(rows), tgt))
log("  speed percentiles: p10 %d  p25 %d  p50 %d  p75 %d  p90 %d  p99 %d  max %d"
    % tuple(sp[int(len(sp) * q)] for q in (.1, .25, .5, .75, .9, .99)) + (sp[-1],))
log("  frames at/above the target: %d (%.1f%%)   above 95%% of it: %d (%.1f%%)"
    % (sum(1 for s in sp if s >= tgt), 100.0 * sum(1 for s in sp if s >= tgt) / len(sp),
       sum(1 for s in sp if s >= tgt * .95), 100.0 * sum(1 for s in sp if s >= tgt * .95) / len(sp)))
cls = {}
for r in rows:
    cls[r[1]] = cls.get(r[1], 0) + 1
log("  surface class under the kart: " +
    " ".join("$%02X:%d" % (k, v) for k, v in sorted(cls.items(), key=lambda kv: -kv[1])))
sl = {}
for r in rows:
    sl["%02X" % r[2]] = sl.get("%02X" % r[2], 0) + 1
log("  slide state $A6: " + " ".join("%s:%d" % (k, v) for k, v in sorted(sl.items())))
log("  |$A8| over 0 on %d frames, over $200 on %d"
    % (sum(1 for r in rows if r[3]), sum(1 for r in rows if abs(r[3]) > 0x200)))
