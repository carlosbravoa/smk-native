# Battery C of accelgrip.py, split out so it can be run alone (ONLY=C) and
# so the run also WRITES the game's own per-frame log in demolog.lua's
# format - which turns this drive into a replay the port can be scored
# against (tools/demoreplay.c), not just two histograms to eyeball.
#
# The driver is the game's own flow field, bang-bang, throttle pinned: not
# a human, but the SAME rule can be run on our side, so the two engines are
# compared on identical inputs on the identical track.
FIELDS = [0x16, 0x1A, 0x22, 0x24, 0xAE, 0x10, 0x12, 0x18, 0x1C, 0x1F, 0x26,
          0x28, 0x2A, 0x60, 0xA0, 0xA2, 0xA4, 0xA6, 0xA8, 0xAA, 0xAC, 0xAE,
          0xB0, 0xB2, 0xB4, 0xC2, 0xC4, 0xCA, 0xD6, 0xDE, 0xE0, 0xE2, 0xE4,
          0xEA, 0xFA, 0xFC, 0xE8, 0xEE, 0xB8]
OUT = os.environ.get("FLOWCSV", "tmp/flowdrive.csv")
fh = open(OUT, "w")
hdr = ["frame", "kart", "g28", "g2A", "g2C", "g2E", "g30", "gE00", "gE02",
       "g124", "g126"] + ["f%02X" % f for f in FIELDS]
fh.write(",".join(hdr) + "\n")

log("")
log("=== C. the real track: hold the throttle and steer down the flow field ===")
park(952, 756)
zero_speed()
for a in (0xA8, 0xAA, 0xA6, 0xFA):
    L.sw(P1 + a, 0)
N = int(os.environ.get("FLOWN", "1800"))
rows = []
for f in range(N):
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
    row = [str(f), "1000"] + [str(L.w(a)) for a in
                              (0x28, 0x2A, 0x2C, 0x2E, 0x30, 0x0E00, 0x0E02, 0x0124, 0x0126)]
    row += [str(L.w(P1 + a)) for a in FIELDS]
    fh.write(",".join(row) + "\n")
    x, y = L.pos()
    tile = L.b.wram[0x10000 + ((y >> 3) & 127) * 128 + ((x >> 3) & 127)]
    rows.append((st(0xEA), L.b.wram[0x0B00 + tile], L.w(P1 + 0xA6), st(0xA8),
                 L.w(P1 + 0xD6)))
fh.close()
log("  wrote %s (%d frames, demolog format - replay it with smk_demoreplay)" % (OUT, N))
sp = sorted(r[0] for r in rows)
tgt = rows[-1][4]
n = len(sp)
log("  frames %d   target $D6 %d (coins %d)" % (n, tgt, L.w(0x0E00)))
log("  speed percentiles: p10 %d  p25 %d  p50 %d  p75 %d  p90 %d  p99 %d  max %d"
    % (sp[n // 10], sp[n // 4], sp[n // 2], sp[3 * n // 4], sp[9 * n // 10],
       sp[99 * n // 100], sp[-1]))
at = sum(1 for r in rows if r[0] >= r[4])
near = sum(1 for r in rows if r[0] >= r[4] * 0.95)
log("  frames at/above the target: %d (%.1f%%)   above 95%% of it: %d (%.1f%%)"
    % (at, 100.0 * at / n, near, 100.0 * near / n))
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
