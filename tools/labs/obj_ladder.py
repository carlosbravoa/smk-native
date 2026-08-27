"""The object drawing ladder: which sprite at which scale, matched by position.

Pipes sit on the ground so they cast no shadow to pair with, and nothing in
the theme sheet ever showed up as a body - so identify the object's sprites
by WHERE they are.  The port's projection was checked against the user's own
screenshot to under 1 SNES pixel (NOTES 156), so the expected screen x of a
live block is trustworthy: any sprite sitting there is that object.

For each live block, log its scale (+$06 = $4200 / axis depth), the band
$84DA3C = C0 60 30 00 puts it in, and the sprites found at its screen x -
their tiles, count, and assembled size.  That is the ladder the port is
supposed to reproduce and currently does not.
"""
import sys, os, time, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.compress import decompress
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cup    = int(sys.argv[1]) if len(sys.argv) > 1 else 0
course = int(sys.argv[2]) if len(sys.argv) > 2 else 0
K, H, LES, TRAIL, LEAD = 4972.0, 20.36, 256.0, 61.0, 0x00C0
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc")); rom = bytes(r.data)
b = Bus(rom); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return cup
        if addr == 0x0152: return course
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
t0 = time.time()
while time.time() - t0 < 1800:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and b.wram[0x0124] not in (0, 0xFF):
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0,0xF0,0xE0)) >= 10: break
w = b.wram
def rw(a): return w[a] | w[a+1] << 8
def s16(v): return v - 65536 if v > 32767 else v
theme = w[0x0126] // 2
print("race: track %d theme %d" % (w[0x0124], theme), flush=True)
for _ in range(400):
    b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)
tp = r.snes_to_pc(0x81EBD3) + theme * 3
src = rom[tp] | (rom[tp+1] << 8) | (rom[tp+2] << 16)
sheet, _ = decompress(rom, r.snes_to_pc(src), max_out=0x10000)
vram = bytes(b.vram); i = vram.find(bytes(sheet)[:32*8])
OBJ0 = (i - (b.regs.get(0x2101,0) & 7) * 0x4000) // 32
print("object sheet at OBJ $%03X (%d tiles)" % (OBJ0, len(sheet)//32), flush=True)

def drive(n):
    for _ in range(n):
        x, y = rw(0x1000+0x18), rw(0x1000+0x1C)
        cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
        d = ((w[0x14000 + cell] << 8) - rw(0x1000+0xA4)) & 0xFFFF
        if d > 32768: d -= 65536
        b.reg_reads[0x4219] = 0x80 | (0x02 if d < -0x300 else 0x01 if d > 0x300 else 0)
        c.run_frames_scanline(1)

def oam():
    out = []
    for k in range(128):
        x, y, t_, at = b.oam[k*4:k*4+4]
        hi = b.oam[512 + (k >> 2)]
        xh = (hi >> ((k & 3) * 2)) & 1
        big = (hi >> ((k & 3) * 2 + 1)) & 1
        if y in (0, 0xF0, 0xE0): continue
        out.append((x - (256 if xh else 0), y, t_ | ((at & 1) << 8),
                    (at >> 1) & 7, 16 if big else 8, (at >> 6) & 1))
    return out

def band(sc):
    for j, t in enumerate((0xC0, 0x60, 0x30)):
        if sc > t: return j
    return -1

seen = {}
print("\n  scale band |  ground (x,line) | sprites there: n, span, tiles", flush=True)
for f in range(9000):
    drive(1)
    kx, ky = rw(0x1000+0x18), rw(0x1000+0x1C)
    az = ((rw(0x1000+0xA4) + LEAD) & 0xFFFF)/65536.0*2*math.pi - math.pi/2
    ca, sa = math.cos(az), math.sin(az)
    s = oam()
    for a in (0x1800, 0x1880, 0x1900, 0x1980):
        ox, oy = rw(a+0x18), rw(a+0x1C)
        if not (0 < ox < 1024 and 0 < oy < 1024): continue
        dx, dy = ox - kx, oy - ky
        zf = dx*ca + dy*sa; xr = -dx*sa + dy*ca
        d = zf + TRAIL
        if d < 20: continue
        gx = 128 + xr*(LES/d); gl = H + K/d
        if not (0 <= gx < 256 and 0 <= gl < 112): continue
        sc = rw(a+0x06)
        near = [e for e in s if e[0] < gx + 14 and e[0] + e[4] > gx - 14
                and e[1] < gl + 4 and e[1] + e[4] > gl - 44]
        if not near: continue
        x0 = min(e[0] for e in near); x1 = max(e[0]+e[4] for e in near)
        y0 = min(e[1] for e in near); y1 = max(e[1]+e[4] for e in near)
        tiles = tuple(sorted({(e[2], e[5]) for e in near}))
        key = (band(sc), len(near), x1-x0, y1-y0, tiles)
        if key in seen: continue
        seen[key] = (sc, f)
        print("  %5d  %2d  | (%5.1f,%5.1f) | %d spr %2dx%-2d  %s"
              % (sc, band(sc), gx, gl, len(near), x1-x0, y1-y0,
                 " ".join("$%03X%s%s" % (t, "H" if fl else "",
                          "=sheet%d" % (t-OBJ0) if OBJ0 <= t < OBJ0+57 else "")
                          for t, fl in tiles)), flush=True)
    if len(seen) > 45: break
print("\ndistinct: %d" % len(seen))
