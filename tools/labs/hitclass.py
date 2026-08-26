"""Drive into a track OBJECT and watch its block at $1800+ for a despawn."""
import sys, time, math
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools")
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
P1 = 0x1000
cup, course, want = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
pick = int(sys.argv[4]) if len(sys.argv) > 4 else 0
TARGET = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0x82
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
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x0124] == want and b.wram[0x36]//2 in (1,6):
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0,0xF0,0xE0)) >= 10: break
def w(a): return b.wram[a] | b.wram[a+1] << 8
# let the countdown finish and the field start moving
for _ in range(600):
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    if w(0x1000+0xEA) > 100: break
def sw(a, v): b.wram[a] = v & 0xFF; b.wram[a+1] = (v >> 8) & 0xFF
print("track $%02X theme $%02X" % (b.wram[0x0124], b.wram[0x0126]), flush=True)
print("race running, player speed %d at (%d,%d)" % (w(P1+0xEA), w(P1+0x18), w(P1+0x1C)), flush=True)
# the STAMPED objects: find cells whose class is a wall near the road
import collections
cells = []
for cell in range(0x2000):
    t = b.wram[0x10000 + cell]
    if b.wram[0x0B00 + t] != TARGET: continue
    cx, cy = cell % 128, cell // 128
    if cy + 5 >= 128: continue
    # road for at least four cells south of it, so the kart can run up
    ok = all(b.wram[0x0B00 + b.wram[0x10000 + (cy + k) * 128 + cx]] >= 0x40 for k in (2,3,4,5))
    if ok: cells.append(cell)
print("class $%02X cells with a run-up: %d" % (TARGET, len(cells)), flush=True)
if not cells: sys.exit()
cell = cells[pick % len(cells)]
ox, oy = (cell % 128) * 8 + 4, (cell // 128) * 8 + 4
snap = bytes(b.wram[0x10000:0x12000])
base = 0x1800
e = -1
snap = bytes(b.wram[0x10000:0x12000])
def dump(tag, f):
    cur = b.wram[0x10000:0x12000]
    diffs = [i for i in range(0x2000) if cur[i] != snap[i]]
    print("%s f%3d kart(%d,%d) spd=%d AE=%02X $10=%04X A0=%d AC=%d E2=%04X | tile@cell %02X | changed %s" % (
        tag, f, w(P1+0x18), w(P1+0x1C), w(P1+0xEA), b.wram[P1+0xAE], w(P1+0x10), w(P1+0xA0), w(P1+0xAC),
        w(P1+0xE2), b.wram[0x10000+cell], [(d, snap[d], cur[d]) for d in diffs[:4]]), flush=True)
# place the kart 48 px "south" of the object, heading north (toward it)
sw(P1+0x18, ox); sw(P1+0x1C, (oy + 28) & 0xFFFF)
sw(P1+0x16, 0); sw(P1+0x1A, 0)
for a in (0x2A, 0xA2, 0xA4): sw(P1+a, 0)      # 0 = -Y = toward the object
sw(P1+0xEA, 0x300)
print("placed at (%d,%d), wall cell %d at (%d,%d) tile %02X class %02X" % (ox, (oy+40)&0xFFFF, cell, ox, oy, b.wram[0x10000+cell], b.wram[0x0B00+b.wram[0x10000+cell]]), flush=True)
for f in range(80):
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    if f % 2 == 0 or f < 8: dump("  ", f)
