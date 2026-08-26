"""Ram a chosen surface class and log every write to the tilemap, the
class table and the tile-change queue WITH the PC that made it.

    python3 blockpc.py <cup> <course> <expected-track> <class-hex>

Reaching an arbitrary track: hook the reads of $0150/$0152 so mode entry
computes $0124 AND the theme (NOTES 118 - forcing $0124 alone is the
NOTES 059 trap), then run 600 frames so the countdown finishes and the
field is moving before touching anything.

Used for NOTES 122: nothing writes the tilemap when a kart rams a wall.
"""
import sys, time
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools")
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
P1 = 0x1000
cup, course, want, TARGET = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4], 16)
r = Rom.load("/home/carlos/extended/devel/games/mariokart/rom/smk_usa.sfc")
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig_read = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return cup
        if addr == 0x0152: return course
    return orig_read(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x0124] == want and b.wram[0x36]//2 in (1,6):
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0,0xF0,0xE0)) >= 10: break
def w(a): return b.wram[a] | b.wram[a+1] << 8
def sw(a, v): b.wram[a] = v & 0xFF; b.wram[a+1] = (v >> 8) & 0xFF
print("track $%02X theme $%02X" % (b.wram[0x0124], b.wram[0x0126]), flush=True)
for _ in range(600):
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    if w(P1+0xEA) > 100: break
print("race running", flush=True)

log = []
watching = [False]
orig_write = b.write
def wr(bank, addr, val):
    if watching[0]:
        if bank == 0x7F and (addr < 0x4000 or addr >= 0xDF00):
            log.append(("map" if addr < 0x2000 else "queue", addr, val, c.PB, c.PC))
        elif (bank & 0x7F) <= 0x3F or bank == 0x7E:
            if 0x0B00 <= addr < 0x0C00 or addr in (0x1EB4, 0x1EB5, 0x1EB6, 0x1EB7):
                log.append(("cls" if addr < 0x0C00 else "cnt", addr, val, c.PB, c.PC))
    orig_write(bank, addr, val)
b.write = wr

# every $82 cell with a run-up, tried in turn at three speeds
cells = []
for cell in range(0x2000):
    t = b.wram[0x10000 + cell]
    if b.wram[0x0B00 + t] != TARGET: continue
    cx, cy = cell % 128, cell // 128
    if cy + 6 >= 128: continue
    if all(b.wram[0x0B00 + b.wram[0x10000 + (cy + k) * 128 + cx]] >= 0x40 for k in (2,3,4,5)):
        cells.append(cell)
print("class $%02X cells with a run-up: %d" % (TARGET, len(cells)), flush=True)
tries = 0
for cell in cells[:6]:
    ox, oy = (cell % 128) * 8 + 4, (cell // 128) * 8 + 4
    for spd in (0x200, 0x400, 0x600):
        tries += 1
        sw(P1+0x18, ox); sw(P1+0x1C, (oy + 30) & 0xFFFF)
        sw(P1+0x16, 0); sw(P1+0x1A, 0)
        for a in (0x2A, 0xA2, 0xA4): sw(P1+a, 0)
        sw(P1+0xEA, spd)
        b.wram[P1+0x1F] = b.wram[P1+0x20] = 0
        before = b.wram[0x10000 + cell]
        watching[0] = True
        for f in range(40):
            b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
            c.run_frames_scanline(1)
        watching[0] = False
        after = b.wram[0x10000 + cell]
        maps = [e for e in log if e[0] == "map"]
        print("cell %5d spd $%03X: tile %02X -> %02X, %d logged (%d map, %d queue, %d cls)" % (
            cell, spd, before, after, len(log), len(maps),
            sum(1 for e in log if e[0]=="queue"), sum(1 for e in log if e[0]=="cls")), flush=True)
        for e in log[:6]:
            print("    %-5s $%04X = %02X  from %02X:%04X" % (e[0], e[1], e[2], e[3], e[4]), flush=True)
        log.clear()
print("done, %d rams" % tries, flush=True)
