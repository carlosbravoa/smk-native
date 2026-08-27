"""Which CGRAM palette do the object sprites use?

The port draws every theme's objects from palette base $F0.  That gives a
green pipe on Mario Circuit and a grey Thwomp on Rainbow Road, but LAVA
colours on Bowser Castle, whose $F0 row is reds and ambers (user).  Rather
than fit a row per theme, read the palette bits the game puts in OAM.

A sprite's OAM attribute byte carries the palette in bits 1-3, so CGRAM
base = $80 + pal*16.  Karts use the low rows ($80-$B0, see SMK_DRIVERS);
whatever the objects use should stand out.  Drives a full lap first, since
Thwomps are parked until then (NOTES 152).
"""
import sys, os, time
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools")
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

cup    = int(sys.argv[1]) if len(sys.argv) > 1 else 0
course = int(sys.argv[2]) if len(sys.argv) > 2 else 3
want   = int(sys.argv[3]) if len(sys.argv) > 3 else 17

r = Rom.load("/home/carlos/extended/devel/games/mariokart/rom/smk_usa.sfc")
b = Bus(bytes(r.data)); c = CPU(b)
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
while time.time() - t0 < 1200:
    c.run_frames_scanline(10)
    if b.wram[0x0124] == want and b.wram[0x36] // 2 in (1, 6):
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10:
            break
w = b.wram
def rw(a): return w[a] | w[a+1] << 8
for _ in range(900):
    b.reg_reads[0x4219] = 0x80
    c.run_frames_scanline(1)
    if rw(0x1000+0xEA) > 100: break
t0 = time.time()
while time.time() - t0 < 1500:
    x, y = rw(0x1000+0x18), rw(0x1000+0x1C)
    cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
    d = ((w[0x14000 + cell] << 8) - rw(0x1000+0xA4)) & 0xFFFF
    if d > 32768: d -= 65536
    b.reg_reads[0x4219] = 0x80 | (0x02 if d < -0x300 else 0x01 if d > 0x300 else 0)
    c.run_frames_scanline(1)
    if (rw(0x1000 + 0xC0) >> 8) >= 0x81: break
print("track $%02X, lap word $%04X" % (w[0x0124], rw(0x1000+0xC0)), flush=True)

# now sample OAM over a while and tally palette use
from collections import Counter
tally = Counter()
for f in range(600):
    x, y = rw(0x1000+0x18), rw(0x1000+0x1C)
    cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
    d = ((w[0x14000 + cell] << 8) - rw(0x1000+0xA4)) & 0xFFFF
    if d > 32768: d -= 65536
    b.reg_reads[0x4219] = 0x80 | (0x02 if d < -0x300 else 0x01 if d > 0x300 else 0)
    c.run_frames_scanline(1)
    for k in range(128):
        yy = b.oam[k*4+1]
        if yy in (0, 0xF0, 0xE0): continue
        attr = b.oam[k*4+3]
        tally[(attr >> 1) & 7] += 1
print("\nOAM palette use over 600 frames (pal -> CGRAM base, count):")
for p, n in sorted(tally.items()):
    print("   pal %d -> $%02X   %d" % (p, 0x80 + p*16, n))
