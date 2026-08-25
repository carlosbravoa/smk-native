"""Re-simulate the decoded player physics from the logged pad words and
compare frame-for-frame against MAME's log of the attract race.

    python3 resim.py demolog.csv 1000 [B2,FA,EA]   (kart 1000 = P1, 1100 = P2)

The per-player block values (BLK) are the ones demolog.lua prints at race
start (BLOCK0710); the demo pairs Mario (P1) with Toad (P2) at 100cc.

Simulated: turn rate $B2 ($80A80F), heading $A4 / velocity angle $A2 /
pose $2A ($80A892), the slide machine states 0 and 2 ($80AA52/$80AAFA),
and the drive accel/speed ($80A6F7 path).  Everything else (hops, items,
collisions) is taken from the log as input.
"""
import csv, sys
LOG = sys.argv[1] if len(sys.argv) > 1 else "demolog.csv"
KART = sys.argv[2] if len(sys.argv) > 2 else "1000"
DETAIL = sys.argv[3].split(",") if len(sys.argv) > 3 else []
import os
ORDER = int(os.environ.get('ORDER', '0')); ROUND = int(os.environ.get('ROUND', '0'))

def s16(v):
    v = int(v)
    v &= 0xFFFF
    return v - 65536 if v > 32767 else v

# drift rows $80AC36: 8 rows x 8 words
ROWS = [
    [0xFFF0,0x0100,0x0800,0x0080,0x00A0,0x00C0,0x00E0,0x0800],
    [0xFFD0,0x0040,0x0900,0x0040,0x0060,0x0080,0x0120,0x1100],
    [0xFFE0,0x0100,0x1800,0x0040,0x0060,0x0080,0x0120,0x2100],
    [0xFFC0,0x0110,0x2000,0x0080,0x00C0,0x0100,0x0140,0x2100],
    [0xFF00,0x0120,0x2800,0x00C0,0x0120,0x0100,0x0180,0x2100],
    [0xFE00,0x0140,0x3000,0x0100,0x0180,0x0200,0x0200,0x2900],
    [0xFD00,0x0200,0x2800,0x0240,0x0360,0x0480,0x0280,0x3100],
    [0xFE00,0x00E0,0x2000,0x0140,0x01E0,0x0280,0x0200,0x2900],
]
# per-player block dumped from $0710 (P1) / $0768 (P2)
BLK = {
 "1000": [0x0200,0x0600,0x0400,0x0800,0x0c00,0x0c00,0x0c00,0x0800,0x0800,0x0400,0x0200,0x0180,0x0100,0x0080,0x0040,0x0040,
          0xffff]*0 + [0x0200,0x0600,0x0400,0x0800,0x0c00,0x0c00,0x0c00,0x0800,0x0800,0x0400,0x0200,0x0180,0x0100,0x0080,0x0040,0x0040,
          0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x0250,0x0120,0x0220,0x0240,0x0240,0x0290,
          0x0995,0x0098,0x0068,0x0070, 0x0a80,0x00a0,0x0078,0x0080, 0x0b00,0x00b0,0x0088,0x0090],
 "1100": [0x0400,0x0c00,0x0ff0,0x0ff0,0x0c00,0x0c00,0x0c00,0x0800,0x0800,0x0400,0x0400,0x0200,0x0080,0x0040,0x0020,0x0020,
          0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x0270,0x0130,0x01f0,0x0260,0x0260,0x02b0,
          0x0995,0x0098,0x0058,0x0080, 0x0a80,0x00a0,0x0068,0x0070, 0x0b00,0x00b0,0x0070,0x0080],
}
A4A0 = [0x20,0x20,0x20,0x30,0x20,0x40,0x40,0x50,0x20,0x60,0x30,0x30,0x00,0x40,0x00,0x60]
A9B8 = [0x00,0x10,0x20,0x30,0x38,0x3C,0x3E,0x40,0x80]
A7FF = [0x0000,-0x80,-0x100,-0x180,-0x200,-0x280,-0x300,-0x380]
A65D = [-4,-10,-16,-24,-48,-112,-160,-192]     # over-cap
A67D = [-4,-6,-8,-10,-16,-40,-80,-120]          # coast (no B)
A68D = [-2,-8,-16,-24,-32,-40,-48,-56]          # over target

rows = [r for r in csv.DictReader(open(LOG)) if r["kart"] == KART]
blk = BLK[KART]
def blkw(i): return blk[i]

def sim():
    # state (start from the log's first moving frame)
    i0 = next(i for i, r in enumerate(rows) if int(r["fEA"]) > 0) - 1
    r0 = rows[i0]
    B2 = s16(r0["fB2"]); A4 = int(r0["fA4"]); A8 = s16(r0["fA8"]); AA = s16(r0["fAA"])
    FA = s16(r0["fFA"]); A6 = int(r0["fA6"]); spd = s16(r0["fEA"]); frac = 0
    accel = 0; EC = 0; E8 = 0
    bad = {k: 0 for k in ("B2","A4","A8","AA","FA","A6","EA")}
    firstbad = {}
    n = 0
    E2 = int(r0["fE2"])
    for i in range(i0 + 1, len(rows)):
        r = rows[i]; prev = rows[i-1]
        C4 = int(r["fC4"]); row = ROWS[int(r["f28"]) >> 4]; DE = int(r["fDE"])
        S08 = (C4 & 3) if (C4 & 0x4030) else A4A0[int(r["fB0"]) >> 1]
        B0 = int(r["fB0"]); D6 = s16(r["fD6"]); B4 = s16(r["fB4"])
        F10 = int(r["f10"]); A0 = int(r["fA0"]); AC = int(r["fAC"]); E2p = int(prev["fE2"])
        # ---- motion ($80A4D0): speed += accel (accel from last control)
        if not (F10 & 0x4000):
            tot = ((spd << 16) | E8) + accel
            spd = s16(tot >> 16); E8 = tot & 0xFFFF
            if spd < 0: spd, E8 = 0, 0
        # ---- heading ($80A892)
        lo = False
        if spd >= 0x80 or (E2p & 0x8000):
            d = B2 >> 3
        else:
            if A0 == 8: y = 0x10 // 2
            else:
                B2 = 0; y = ((spd >> 3) & ~1) // 2
            if F10 & 0x2000: d = 0
            elif C4 & 0x200: d = -A9B8[y]
            elif C4 & 0x100: d = A9B8[y]
            else: d = 0
        A4 = (A4 + d) & 0xFFFF
        A2 = (A4 + A8) & 0xFFFF; P2A = (A4 - AA) & 0xFFFF
        # ---- control: slide machine ($80AA18)
        E2 = int(r["fE2"])   # we do not model E2; take bits 2/5 from log
        if A6 == 0:
            FA = 0
            a = spd
            if spd < 0x100: dec = True
            else:
                a = spd - B4
                if a >= 0: A6 = 2; dec = False
                elif (a & 0xFFFF) < row[0]: dec = True
                elif abs(B2) >= 0x300: A6 = 2; dec = False
                else: dec = True
            if dec:
                A8, AA, A6 = decay(A8, AA, row, A6)
        elif A6 == 2:
            if int(r["f28"]) == 0 or spd < 0x100:
                A6 = 0x1C
            else:
                steer = None
                if C4 & 0x8000: steer = C4 & 0x300
                elif (C4 & 0x30) and spd >= 0x1C0: steer = C4 & 0x300
                if steer == 0x200:      # Left
                    if AA < 0: AA += row[6]
                    elif AA >= row[7]: AA -= row[6]
                    else: AA += row[6]
                    if A8 >= 0:
                        FA = s16(FA - row[1])
                        if FA < 0 and (FA & 0xFFFF) < 0x8600: A6 = 0x10
                        a = A8 + row[3]
                    else:
                        FA = s16(FA + S08)          # SUB_80ABAD: stale $08
                        a = A8 + row[5]
                    A8 = clamp(a, row[2])
                elif steer == 0x100:    # Right
                    if AA >= 0: AA -= row[6]
                    elif -AA >= row[7]: AA += row[6]
                    else: AA -= row[6]
                    if A8 < 0:
                        FA = s16(FA + row[1])
                        if FA >= 0x7A00: A6 = 0x0E
                        a = A8 - row[3]
                    else:
                        FA = s16(FA - S08)          # SUB_80ABA3: stale $08
                        a = A8 - row[5]
                    A8 = clamp(a, row[2])
                else:
                    step = row[1] + 0xE0 + 1
                    FA = s16(FA + step) if FA < 0 else s16(FA - step)
                    A8, AA, A6 = decay(A8, AA, row, A6)
        # ---- turn rate ($80A80F)
        if ORDER and AC == 0 and A6 <= 8 and not (F10 & 0x80) and (C4 & 0x8000) and not (C4 & 0x4000):
            capq = s16(blkw(16 + (B0 >> 1)))
            if not (capq >= 0 and spd > capq) and spd < D6:
                kq = A7FF[7] if spd >= 0x3FF else (0 if spd < 0x300 else A7FF[((spd - 0x300) >> 5) & 7])
                B2 = s16(B2 + (((kq * B2) + (0x4000 if ROUND else 0)) >> 15))
        tbl = blk[(DE >> 1):(DE >> 1) + 4]   # words at block + DE
        mx, rev, ramp, dcy = tbl
        if F10 & 0x80:
            B2 = 0
        elif (E2 & 0x200) and not (E2 & 0x400):
            pass
        elif C4 & 0x200:
            if B2 == 0: B2 = -ramp
            elif B2 > 0: B2 -= rev
            else:
                m = -B2
                B2 = -mx if m >= mx else -(m + ramp)
        elif C4 & 0x100:
            if B2 < 0: B2 += rev
            elif B2 >= mx: B2 = mx
            else: B2 += ramp
        else:
            if B2 >= 0: B2 = max(0, B2 - dcy)
            else: B2 = min(0, B2 + dcy)
        # ---- drive ($80A553 -> $AC state 0 -> $A6 handler)
        modelled = AC == 0 and A6 <= 8 and not (F10 & 0x80)
        if modelled:
            if C4 & 0x4000:
                accel = ((A65D if (C4 & 0x40) else [-4,-7,-9,-12,-28,-72,-110,-160])[(spd >> 8) & 7] << 16) | EC
            elif not (C4 & 0x8000):
                accel = (A67D[(spd >> 8) & 7] << 16) | EC
            else:
                cap = s16(blkw(16 + (B0 >> 1)))
                if cap >= 0 and spd > cap:
                    accel = (A65D[(spd >> 8) & 7] << 16) | EC
                elif spd >= D6:
                    over = min(spd - D6, 0x1FF)
                    accel = (A68D[(over >> 6) & 7] << 16) | EC
                else:
                    if spd >= 0x3FF: k = A7FF[7]
                    elif spd < 0x300: k = 0
                    else: k = A7FF[((spd - 0x300) >> 5) & 7]
                    if not ORDER: B2 = s16(B2 + (((k * B2) + (0x4000 if ROUND else 0)) >> 15))
                    A = 0xC0 if (E2 & 1) else blkw(((spd if spd < 0x400 else 0x3FF) >> 6))
                    EC = ((A & 0xFF) << 8) | (EC & 0xFF)
                    accel = ((A >> 8) << 16) | EC
        else:
            accel = (s16(r["fEE"]) << 8) if "fEE" in r else 0
            spd = s16(r["fEA"]); A6 = int(r["fA6"])   # resync outside modelled states
        # ---- compare
        n += 1
        got = {"B2": B2, "A4": A4, "A8": A8, "AA": AA, "FA": FA, "A6": A6, "EA": spd}
        want = {"B2": s16(r["fB2"]), "A4": int(r["fA4"]), "A8": s16(r["fA8"]), "AA": s16(r["fAA"]),
                "FA": s16(r["fFA"]), "A6": int(r["fA6"]), "EA": s16(r["fEA"])}
        for k in got:
            if got[k] != want[k]:
                bad[k] += 1
                firstbad.setdefault(k, (r["frame"], got[k], want[k], hex(C4), int(r["f28"])))
                if k in DETAIL and bad[k] <= 12:
                    print("  %s f%s got %6d want %6d  C4=%04x spd=%d row=%02x DE=%02x A6=%d A8=%d prevB2=%d E2=%04x" % (k, r["frame"], got[k], want[k], C4, want["EA"], int(r["f28"]), DE, want["A6"], want["A8"], s16(prev["fB2"]), E2))
        # resync to the log so one divergence does not cascade
        B2, A4, A8, AA, FA, A6, spd = (want[k] for k in ("B2","A4","A8","AA","FA","A6","EA"))
        E8 = int(r["fE8"])
        if not modelled: accel = (s16(r["fEE"]) << 16) | EC
    print("kart", KART, "frames", n)
    for k in bad: print("  %s mismatches %4d / %d   first: %s" % (k, bad[k], n, firstbad.get(k)))

def clamp(a, lim):
    if a < 0: return -min(-a, lim)
    return min(a, lim)

def decay(A8, AA, row, A6):
    if AA < 0:
        AA = AA + row[6]
        if AA > 0: AA = 0
    else:
        AA = AA - row[6]
        if AA < 0: AA = 0
    if A8 < 0:
        if A8 + row[4] <= 0 and (A8 + row[4]) != 0 or A8 + row[4] < 0: A8 = A8 + row[4]
        else: A6 = 0
    else:
        if A8 - row[4] >= 0: A8 = A8 - row[4]
        else: A6 = 0
    return A8, AA, A6

sim()
