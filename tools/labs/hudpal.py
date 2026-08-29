#!/usr/bin/env python3
"""Where does the HUD's item palette come from, and does it change?

    tools/labs/hudpal.py

The item icons are 2bpp BG3 tiles on palettes 4-7 (docs/ITEMS.md §7).  In
the attract race's CGRAM palette 4 is yellow/black/dark red and 6 is grey,
and a green shell drawn with them looks nothing like the user's screenshot
(green and white).  So either the race loads different colours than the
attract does, or the item code animates them.  This hooks every write to
$2122 aimed at CGRAM bytes 32..63 (palettes 4-7) with the PC that made it,
through a whole roulette into READY, and dumps CGRAM at three points.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

TMP = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tmp")
L = Lab(settle=60, zero=(0x0E50, 0x0E51))
b, c = L.b, L.c
writes = []
frame = [0]
orig_write = b.write
def hooked(bank, addr, val):
    if addr == 0x2122 and 32 <= (b.cgadd & 0x1FF) < 64:
        writes.append((frame[0], b.cgadd & 0x1FF, val, (c.PB << 16) | c.PC))
    return orig_write(bank, addr, val)
b.write = hooked
for _ in range(200):
    L.flow(1); frame[0] += 1
    if L.speed() > 400: break
log("moving; CGRAM 16..31 now: " + " ".join("%04X" % (b.cgram[i*2] | b.cgram[i*2+1] << 8) for i in range(16, 32)))
log("writes to palettes 4-7 so far: %d" % len(writes))
for f, a, v, pc in writes[:12]: log("   f%d  cgram byte %d = %02X  from $%06X" % (f, a, v, pc))
writes.clear()
L.sw(0x0D70, 0xA000); L.sw(0x0D78, 0xC1); L.sw(0x0D74, 0xB49D); L.sw(0x0D7C, 4)   # a green shell
for f in range(320):
    L.flow(1); frame[0] += 1
    if f in (10, 200, 300):
        open(os.path.join(TMP, "hud_cgram_%d.bin" % f), "wb").write(bytes(b.cgram))
        log("f%d: $0D70=$%04X  pal4 %s  pal6 %s" % (f, L.w(0x0D70),
            " ".join("%04X" % (b.cgram[i*2] | b.cgram[i*2+1] << 8) for i in range(16, 20)),
            " ".join("%04X" % (b.cgram[i*2] | b.cgram[i*2+1] << 8) for i in range(24, 28))))
log("writes to palettes 4-7 during the roulette and hold: %d" % len(writes))
seen = {}
for f, a, v, pc in writes:
    seen.setdefault(pc, []).append((f, a, v))
for pc, ws in sorted(seen.items()):
    log("   $%06X wrote %d bytes; first: f%d byte %d = %02X" % (pc, len(ws), ws[0][0], ws[0][1], ws[0][2]))
