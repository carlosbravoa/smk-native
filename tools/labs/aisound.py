#!/usr/bin/env python3
"""Does an AI kart make a NOISE when it throws?

The user: "check the sound they make when passing by or when throwing an
object".  The pass-by is the engine (NOTES 229/234); this settles the
throw, and it settles it the way the user insists - by driving the game
into the state rather than waiting for a recording to contain one.

aiweapon3's setup, verbatim: attract race un-demoed, every kart's lap
byte pushed to lap 2 so the AI is allowed to attack, and the object
blocks watched for one going live or jumping to a kart.  On top of it,
sfxsweep's tap on the sound entries.  Then every drop is printed WITH
every sound request in the same frame window, so "an AI threw and the
game asked for nothing" is a measurement, not an absence of evidence.

    tools/labs/aisound.py            # FRAMES=6000 LAP=2
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
import sfxsweep

FRAMES = int(os.environ.get("FRAMES", "6000"))
LAP = int(os.environ.get("LAP", "2"))
WINDOW = 4                       # frames either side of a drop that count

L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, wram = L.w, L.b.wram
for q in range(8):
    wram[0x1000 + q * 0x100 + 0xC1] = 0x7F + LAP

sink = []
sfxsweep.install(L, sink)

BL = list(range(0x1800, 0x1A00, 0x80)) + [0x1A00, 0x1A80]
def pos(b): return w(b + 0x18), w(b + 0x1C)
prev = {b: pos(b) for b in BL}
prevlive = {b: w(b + 0x12) & 0x8000 for b in BL}

sounds = []          # (frame, id, caller)
drops = []           # (frame, block, kart, player distance)

for f in range(FRAMES):
    mark = len(sink)
    L.flow(1)
    for a, pc, caller in sink[mark:]:
        sounds.append((f, a & 0xFF, caller))
    ks = [(q, w(0x1000 + q * 0x100 + 0x18), w(0x1000 + q * 0x100 + 0x1C)) for q in range(8)]
    px, py = ks[0][1], ks[0][2]
    for b in BL:
        p = pos(b); live = w(b + 0x12) & 0x8000
        moved = p != prev[b] and abs(p[0] - prev[b][0]) + abs(p[1] - prev[b][1]) > 64
        if moved or (live and not prevlive[b]):
            q = min(ks, key=lambda k: abs(k[1] - p[0]) + abs(k[2] - p[1]))
            if q[0] != 0:                       # an AI's, not the player's
                drops.append((f, b, q[0], abs(px - p[0]) + abs(py - p[1])))
                log("f%d AI DROP block $%04X by kart %d, player d=%d"
                    % (f, b, q[0], abs(px - p[0]) + abs(py - p[1])))
        prev[b] = p; prevlive[b] = live

log("--- %d AI drops, %d sound requests in %d frames ---" % (len(drops), len(sounds), FRAMES))
hit = 0
for df, b, q, d in drops:
    near = [(sf, sid, c) for sf, sid, c in sounds if abs(sf - df) <= WINDOW]
    if near:
        hit += 1
        log("  drop f%d (kart %d, d=%d) -> %s" % (df, q, d,
            ', '.join("$%02X from $%06X" % (sid, c) for _, sid, c in near)))
    else:
        log("  drop f%d (kart %d, d=%d) -> SILENT (no request within %d frames)"
            % (df, q, d, WINDOW))
log("%d of %d AI drops had a sound request beside them" % (hit, len(drops)))

from collections import Counter
for (sid, c), n in Counter((s, c) for _, s, c in sounds).most_common(20):
    log("  sound $%02X from $%06X x%d" % (sid, c, n))
sfxsweep.flush_csv()
log("done")
