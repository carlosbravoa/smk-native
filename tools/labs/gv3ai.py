"""The game's own field on Ghost Valley 3 at 150cc, in the oracle: hook the
cup/course reads (NOTES 118) to Special Cup course 2 and the class read to
150cc, then log every kart per frame and dump the direction field."""
import sys, os, time
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools/labs")
from lab import log
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
OUT = sys.argv[1]
r = Rom.load("/home/carlos/extended/devel/games/mariokart/rom/smk_usa.sfc")
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig_read = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return 3          # Special Cup
        if addr == 0x0152: return 2          # its third course: Ghost Valley 3
        if addr == 0x0030: return 4          # 150cc
        if addr == 0x0031: return 0
    return orig_read(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
f = open(OUT, "w")
f.write("f,mode,track," + ",".join("k%d_x,k%d_y,k%d_a0,k%d_ac,k%d_ea,k%d_lap,k%d_surf,k%d_a4" % ((k,)*8) for k in range(8)) + "\n")
t0 = time.time(); n = 0; dumped = False
w = b.wram
def W(a): return w[a] | w[a+1] << 8
while time.time() - t0 < 5400 and n < 7000:
    c.run_frames_scanline(1); n += 1
    mode = w[0x36] // 2
    if mode in (1, 6) and (w[0x1018] or w[0x1019]):
        if not dumped and W(0x10EA) > 0:
            open(OUT + ".flow", "wb").write(bytes(w[0x14000:0x15000]))
            open(OUT + ".sect", "wb").write(bytes(w[0x15000:0x16000]))
            dumped = True; log("field dumped at frame", n, "track", hex(W(0x0124)))
        row = [str(n), str(mode), str(W(0x0124))]
        for k in range(8):
            base = 0x1000 + k * 0x100
            row += [str(W(base+0x18)), str(W(base+0x1C)), str(w[base+0xA0]), str(w[base+0xAC]), str(W(base+0xEA)), str(w[base+0xC1]), "%02X" % w[base+0x68], str(W(base+0xA4))]
        f.write(",".join(row) + "\n")
    if n % 500 == 0: f.flush(); log("frame", n, "mode", mode, "track", hex(W(0x0124)), "p1 spd", W(0x10EA), "%.0fs" % (time.time()-t0))
f.close(); log("done", n)
