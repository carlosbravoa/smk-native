"""What does the game itself draw a near pipe with?

Every previous size claim came from reading the object sheet and guessing
the assembly.  This reads the answer out of the running game: the OAM
entries of the object sprites while a pipe is close to the camera - how
many, which size bit, and the pixel bounding box they cover.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
from smktool.screen import sprite_entries, render_sprites
from smktool.gfx import write_png

L = Lab(settle=120)

def objs():
    out = []
    for i, x, y, t9, a, big in sprite_entries(bytes(L.b.oam)):
        if y >= 0xE0 or y == 0:
            continue
        if 0xC0 <= (t9 & 0x1FF) <= 0xFF:
            out.append((i, x, y, t9 & 0x1FF, a, big))
    return out

seen = {}
best = None
for step in range(400):
    L.flow(2)
    o = objs()
    if not o:
        continue
    for e in o:
        seen[((e[3] >> 4) << 4, (e[4] >> 1) & 7)] = seen.get(
            ((e[3] >> 4) << 4, (e[4] >> 1) & 7), 0) + 1
    # group the entries by screen proximity; keep the tallest group seen
    for seed in o:
        grp = [e for e in o if abs(e[2] - seed[2]) <= 32
               and abs(e[1] - seed[1]) <= 24]
        h = max(e[2] for e in grp) - min(e[2] for e in grp)
        if best is None or h > best[0]:
            best = (h, grp, step, bytes(L.b.oam), bytes(L.b.vram),
                    bytes(L.b.cgram))

log("object-range OAM seen over the lap  (tile hi nibble, palette): count")
for k in sorted(seen):
    log("   $%02X pal %d : %d" % (k[0], k[1], seen[k]))

if best is None:
    log("no object sprites seen"); raise SystemExit(1)
h, grp, step, oam, vram, cgram = best
log("\ntallest object group (flow step %d): %d sprites, %d px vertical span"
    % (step, len(grp), h))
log("  idx    x    y  tile  big  pal   dx  dy")
x0 = min(e[1] for e in grp); y0 = min(e[2] for e in grp)
for i, x, y, t9, a, big in sorted(grp, key=lambda e: (e[2], e[1])):
    log("  %3d %4d %4d  $%03X   %d    %d   %+3d %+3d"
        % (i, x, y, t9, big, (a >> 1) & 7, x - x0, y - y0))
W, H, buf = render_sprites(vram, cgram, oam, 0x02)
write_png("/tmp/objframe.png", W, H, buf)
log("frame -> /tmp/objframe.png")
