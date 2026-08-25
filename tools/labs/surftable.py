"""Does our surface table match the one the running game actually uses?

Our loader reads the ROM's compressed surface blob and copies 192 bytes
at the per-theme offset.  The game builds its table in WRAM at $0B00.
If they differ, every surface decision downstream is wrong.
"""
from lab import Lab, log

L = Lab()
live = bytes(L.b.wram[0x0B00:0x0BC0])
track = L.w(0x0124)
log("live game: track %d" % track)
log("live $0B00 table (192 bytes):")
for i in range(0, 192, 32):
    log("  %02X: %s" % (i, " ".join("%02X" % b for b in live[i:i + 32])))

hist = {}
for b in live:
    hist[b] = hist.get(b, 0) + 1
log("classes present live: %s"
    % " ".join("$%02X:%d" % (k, v) for k, v in sorted(hist.items())))

# what tiles does the live tilemap actually use, and what classes result?
used = {}
for i in range(0x10000, 0x14000):
    t = L.b.wram[i]
    cls = live[t] if t < 192 else None
    if cls is not None:
        used[cls] = used.get(cls, 0) + 1
log("classes REACHED by the live tilemap: %s"
    % " ".join("$%02X:%d" % (k, v) for k, v in sorted(used.items())))
