"""Lakitu's OTHER three jobs: the lap sign, the chequered flag, and the
rescue.

Same method as NOTES 162's start capture - drive the game and read its
own OAM - because none of it is reachable any other way.  This one drives
P1 along the flow field until the lap word $C0 rolls over, recording the
OAM through the crossing.

    python3 tools/labs/lakitu_lap.py
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

TMP = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "tmp")

L = Lab(settle=0)
log("race reached: track %d" % L.w(0x0124))

def sprites():
    out = []
    for k in range(128):
        y = L.b.oam[k * 4 + 1]
        if y in (0, 0xF0, 0xE0): continue
        x = L.b.oam[k * 4]
        t = L.b.oam[k * 4 + 2]
        at = L.b.oam[k * 4 + 3]
        hi = L.b.oam[0x200 + (k >> 2)]
        sh = (hi >> ((k & 3) * 2)) & 3
        out.append((k, x | ((sh & 1) << 8), y, t | ((at & 1) << 8), at,
                    (sh >> 1) & 1))
    return out

def kart_tile(t):
    """the eight karts own $C0-$DF; the HUD's own furniture is $140+"""
    return 0xC0 <= (t & 0xFF) <= 0xDF

lap = L.w(0x10C0) >> 8
log("starting lap word $%04X" % L.w(0x10C0))
# $7F -> $80 is the GRID crossing and shows no sign (NOTES 052); the sign
# belongs to the crossing that completes a lap, so wait for the one after.
want = 0x81
seen = {}
f = 0
t0 = time.time()
while time.time() - t0 < 1500:
    L.flow(1)
    f += 1
    now = L.w(0x10C0) >> 8
    if now != lap:
        log("lap word %d -> %d at f%d" % (lap, now, f))
        lap = now
    if now >= want:
        log("LAP COMPLETED at f%d" % f)
        # The sign's art is STREAMED - $100+ holds kart tiles during the
        # countdown - so VRAM has to be dumped here, not borrowed from
        # the start capture.
        for g in range(200):
            if g in (0, 40, 80, 120):
                open(os.path.join(TMP, "lap_vram_%d.bin" % g), "wb").write(bytes(L.b.vram))
                open(os.path.join(TMP, "lap_cgram_%d.bin" % g), "wb").write(bytes(L.b.cgram))
                open(os.path.join(TMP, "lap_oam_%d.bin" % g), "wb").write(bytes(L.b.oam))
                log("dumped vram/cgram/oam at sign frame %d" % g)
            for s in sprites():
                if kart_tile(s[3]): continue
                if s[3] < 0x40 or s[1] >= 256: continue
                key = s[3]
                seen.setdefault(key, []).append((g, s[1], s[2], s[4], s[5]))
            L.flow(1)
        break
# the plate's own path, frame by frame: that is the whole animation
with open(os.path.join(TMP, "lap_sign_path.txt"), "w") as fh:
    for g, x, y, at, big in seen.get(0x0A0, []):
        fh.write("%d %d %d\n" % (g, x, y))
log("wrote lap_sign_path.txt (%d frames)" % len(seen.get(0x0A0, [])))

if not seen:
    log("no lap crossed in the time available")
else:
    log("tiles that appeared through the crossing:")
    for t in sorted(seen):
        v = seen[t]
        log("  tile $%03X  %3d frames, f%d..%d, x %d..%d y %d..%d attr $%02X %s"
            % (t, len(v), v[0][0], v[-1][0],
               min(a[1] for a in v), max(a[1] for a in v),
               min(a[2] for a in v), max(a[2] for a in v),
               v[0][3], "16x16" if v[0][4] else "8x8"))
