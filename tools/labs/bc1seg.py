#!/usr/bin/env python3
"""BC1 (cup 0 course 3): poke the player's waypoint through every lap
segment and print which entities the game spawns - the reference for the
$84DAC5 offset-table port (bug 14)."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from lab import log
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return 0
        if addr == 0x0152: return 3
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and sum(1 for k in range(128) if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break
else: raise SystemExit("never reached the race")
c.run_frames_scanline(200)
def u16(a): return b.wram[a] | b.wram[a+1] << 8
def blocks():
    out=[]
    for base in range(0x1800, 0x1A00, 0x40):
        if u16(base):
            out.append((base, u16(base), u16(base+0x18), u16(base+0x1C)))
    return out
# thresholds for track 17: 9 16 23 32 255 -> probe one waypoint inside each
for wp in (2, 12, 20, 28, 40):
    b.wram[0x10C0] = wp & 0xFF; b.wram[0x10C1] = 0
    c.run_frames_scanline(30)
    log("wp %3d seg? $0D34=%d: %s" % (wp, u16(0x0D34),
        " ".join("(%d,%d)t%04X" % (x, y, t) for _, t, x, y in blocks())))
