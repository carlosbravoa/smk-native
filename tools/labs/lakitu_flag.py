#!/usr/bin/env python3
"""Lakitu's third job: the CHEQUERED FLAG at the finish.

    tools/labs/lakitu_flag.py

The user's screenshot of a win shows him hovering at the left with the
flag out, casting the oval shadow (which is HIS, not the kart's - "mario
is not airbone!").  Nothing in the port draws it and no gate has ever
seen a finish.

Driving five laps in the oracle would cost the best part of an hour, so
the lap word is FORCED instead: $C0's high byte is the lap counter based
at $7F (NOTES 174), and $F8 is the progress watermark it is checked
against, so setting both to the last lap makes the next crossing the
finish.  Then drive the flow field into the line and record OAM.

OAM is the instrument on purpose (S28): it names the exact tiles, sizes
and palettes the game draws, which is the thing that cannot be guessed
from a sheet.
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

TMP = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "tmp")
P1 = 0x1000
LAST = int(os.environ.get("LAST", "0x84"), 0)

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
    return 0xC0 <= (t & 0xFF) <= 0xDF

# get moving first, then jump the lap counter to the last lap
for _ in range(400):
    L.flow(1)
    if L.speed() > 200: break
log("moving at %d; lap word $%04X" % (L.speed(), L.w(P1 + 0xC0)))
L.sw(P1 + 0xC0, LAST << 8)
L.sw(P1 + 0xF8, LAST << 8)
log("lap word forced to $%04X, watermark to $%04X"
    % (L.w(P1 + 0xC0), L.w(P1 + 0xF8)))

seen, f = {}, 0
t0 = time.time()
done = False
while time.time() - t0 < 1500 and not done:
    L.flow(1); f += 1
    now = L.w(P1 + 0xC0) >> 8
    if now > LAST:
        log("FINISHED at f%d (lap word $%04X)" % (f, L.w(P1 + 0xC0)))
        for g in range(240):
            if g in (0, 30, 60, 100, 160):
                for nm, buf in (("vram", L.b.vram), ("cgram", L.b.cgram),
                                ("oam", L.b.oam)):
                    open(os.path.join(TMP, "flag_%s_%d.bin" % (nm, g)),
                         "wb").write(bytes(buf))
                log("  dumped vram/cgram/oam at finish frame %d" % g)
            for s in sprites():
                if kart_tile(s[3]): continue
                seen.setdefault(s[3], []).append((g, s[1], s[2], s[4], s[5]))
            L.flow(1)
        done = True

if not done:
    log("never crossed - the forced lap word did not take")
else:
    log("\nsprites through the finish (tile: frames, x range, y range, attr):")
    for t, rows in sorted(seen.items()):
        xs = [r[1] for r in rows]; ys = [r[2] for r in rows]
        log("  tile $%03X  %3d frames  x %3d..%3d  y %3d..%3d  attr $%02X %s"
            % (t, len(rows), min(xs), max(xs), min(ys), max(ys), rows[0][3],
               "16x16" if rows[0][4] else "8x8"))
    with open(os.path.join(TMP, "flag_path.txt"), "w") as fh:
        for t, rows in sorted(seen.items()):
            for g, x, y, at, big in rows:
                fh.write("%03X %d %d %d\n" % (t, g, x, y))
    log("wrote flag_path.txt")
