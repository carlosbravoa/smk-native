"""S10: the constant behind an object's projected scale (+$06).

$80C879 stores the DSP-1 projection's THIRD output in +$06, and $84DA18
picks the drawing by which band it falls in - thresholds $C0 $60 $30 at
$84DA3C.  So the only number the port is missing is what ties +$06 to
distance.  Drive, and fit it.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1

L = Lab(settle=60)
w = L.b.wram
rows = []
for f in range(600):
    L.flow(1)
    kx, ky = L.s16(L.w(P1 + 0x18)), L.s16(L.w(P1 + 0x1C))
    for e in (0x1800, 0x1880):
        ox = w[e+0x18] | w[e+0x19] << 8
        oy = w[e+0x1C] | w[e+0x1D] << 8
        s  = w[e+6] | w[e+7] << 8
        if not (ox or oy) or s == 0 or s > 0x300:
            continue
        a = L.w(P1 + 0x2A) * 2 * math.pi / 65536.0
        dx, dy = ox - kx, oy - ky
        zf = dx * math.sin(a) + dy * -math.cos(a)      # ahead of the kart
        rows.append((math.hypot(dx, dy), zf, s))
log("samples: %d" % len(rows))
def fit(name, den):
    ks = sorted(den(r) * r[2] for r in rows if den(r) > 1)
    if not ks:
        return
    med = ks[len(ks)//2]
    n = len(ks)
    rel = sorted(abs(den(r) * r[2] - med) / med for r in rows if den(r) > 1)
    log("%-28s n=%4d  const median %6.0f ($%04X)  spread %.0f..%.0f  rel err mean %.3f p90 %.3f"
        % (name, n, med, int(med), ks[0], ks[-1],
           sum(rel)/len(rel), rel[int(len(rel)*0.9)]))

for trail in (0, 61):
    fit("euclid from kart + %d" % trail, lambda r, t=trail: r[0] + t)
    fit("along-axis depth + %d" % trail, lambda r, t=trail: r[1] + t)
