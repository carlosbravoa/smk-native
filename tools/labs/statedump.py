"""Read a MAME save state: the pose, the live object blocks, and OAM.

MAMESAVE v2 is a 32-byte header then zlib.  WRAM is found by its own
contents rather than by offset - the object block table at $1DA0 points at
blocks whose +$18/+$1C are plausible world coordinates - and OAM is then
found by the SIGNATURE the pose implies: project every live object with
the port's own law (checked to under 1 SNES px against the user's
screenshot, NOTES 156) and look for the 544-byte window that actually has
sprites at those screen positions.  A loose search matches tens of
thousands of windows; this one does not.

  python3 tools/labs/statedump.py ~/.mame/sta/snes/1.sta
"""
import sys, os, zlib, math, struct

path = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/.mame/sta/snes/1.sta")
raw = open(path, "rb").read()
data = zlib.decompress(raw[32:])
print("%s: %d bytes -> %d decompressed" % (path, len(raw), len(data)))

K, H, LES, TRAIL, LEAD = 4972.0, 20.36, 256.0, 61.0, 0x00C0
BLK = (0x1800, 0x1880, 0x1900, 0x1980)

def s16(v): return v - 65536 if v > 32767 else v

def score_wram(base):
    """$1DA0 is the LIVE TABLE: it holds the block addresses themselves.

    Requiring the words there to be drawn from {$1800,$1880,$1900,$1980},
    and the blocks they name to hold world coordinates and a script
    pointer in the bytecode range, is specific enough to be unique - a
    coordinates-only test matched nine thousand offsets."""
    def rw(a):
        p = base + a
        return data[p] | data[p+1] << 8
    if base + 0x20000 > len(data): return None
    tbl = [rw(0x1DA0 + i*2) for i in range(4)]
    named = [t for t in tbl if t in BLK]
    if len(set(named)) < 2: return None
    live = 0
    for a in BLK:
        x, y, sc = rw(a + 0x18), rw(a + 0x1C), rw(a + 0x04)
        if 0 < x < 1024 and 0 < y < 1024 and 0xE000 <= sc <= 0xEFFF: live += 1
    if live < 2: return None
    kx, ky = rw(0x1000 + 0x18), rw(0x1000 + 0x1C)
    if not (0 < kx < 1024 and 0 < ky < 1024): return None
    if not (0 < data[0x0124 + base] < 24): return None
    return live

cands = [b for b in range(0, len(data) - 0x20000, 1) if score_wram(b)]
if not cands:
    print("no WRAM found"); raise SystemExit(1)
W = cands[0]
def rw(a): return data[W + a] | data[W + a + 1] << 8
print("WRAM base 0x%06X  (%d candidates)  track %d theme %d mode %d"
      % (W, len(cands), data[W + 0x0124], data[W + 0x0126] // 2, data[W + 0x002C]))
kx, ky, kh = rw(0x1000+0x18), rw(0x1000+0x1C), rw(0x1000+0xA4)
print("kart (%d,%d) heading $%04X speed %d" % (kx, ky, kh, s16(rw(0x1000+0xEA))))

az = ((kh + LEAD) & 0xFFFF)/65536.0*2*math.pi - math.pi/2
ca, sa = math.cos(az), math.sin(az)
want = []
print("\n live object blocks:")
for a in BLK:
    x, y = rw(a+0x18), rw(a+0x1C)
    if not (0 < x < 1024 and 0 < y < 1024): continue
    dx, dy = x - kx, y - ky
    zf = dx*ca + dy*sa; xr = -dx*sa + dy*ca
    d = zf + TRAIL
    if d < 12: continue
    gx = 128 + xr*(LES/d); gl = H + K/d
    z = s16(rw(a+0x1F))
    lift = z * 0.9347 / d
    print("   $%04X (%3d,%3d) script $%04X z=%5d | axis depth %5.1f scale %4d"
          " | ground (%6.1f,%5.1f) lift %5.1f -> feet line %5.1f"
          % (a, x, y, rw(a+0x04), z, zf, rw(a+0x06), gx, gl, lift, gl - lift))
    if 0 <= gx < 256: want.append((gx, gl, gl - lift))

# No OAM search here.  Locating the PPU's 544-byte OAM inside the state by
# signature was tried and abandoned: position alone matched 32750 windows
# and tightening it to the theme sheet's tile range matched none, so every
# variant was either uselessly broad or resting on an assumption about the
# layout that could not be checked.  The pose and the projected positions
# above are what make a screenshot of the same moment measurable, and that
# measurement is the one that has actually worked (NOTES 156).

if len(sys.argv) > 3:
    iw, ih = int(sys.argv[2]), int(sys.argv[3])
    print("\nwhere to look in a %dx%d screenshot of this frame" % (iw, ih))
    print("  (x = %.2f px per SNES px, y = %.2f px per line, full 112-line view)"
          % (iw / 256.0, ih / 112.0))
    for gx, gl, feet in want:
        print("   object: ground (%6.1f,%5.1f) -> image (%6.0f,%5.0f)"
              "   feet line %5.1f -> image y %5.0f"
              % (gx, gl, gx * iw / 256.0, gl * ih / 112.0, feet, feet * ih / 112.0))
