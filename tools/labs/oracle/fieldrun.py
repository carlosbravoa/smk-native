"""The game's own field on a cup course at a class, in the oracle: every
kart per frame (gv3ai.py generalised).  fieldrun.py OUT cup course class frames"""
import sys, os, time
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools/labs")
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools")
from lab import log
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
OUT, CUP, COURSE, CLS, FRAMES = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
r = Rom.load("/home/carlos/extended/devel/games/mariokart/rom/smk_usa.sfc")
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig_read = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return CUP
        if addr == 0x0152: return COURSE
        if addr == 0x0030: return CLS * 2
        if addr == 0x0031: return 0
    return orig_read(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
f = open(OUT, "w")
f.write("f,mode,track," + ",".join("k%d_x,k%d_y,k%d_a0,k%d_ac,k%d_ea,k%d_lap,k%d_surf,k%d_c0" % ((k,)*8) for k in range(8)) + "\n")
t0 = time.time(); n = 0; racing = 0
w = b.wram
def W(a): return w[a] | w[a+1] << 8
while time.time() - t0 < 7200 and racing < FRAMES:
    c.run_frames_scanline(1); n += 1
    mode = w[0x36] // 2
    if mode in (1, 6) and (w[0x1018] or w[0x1019]):
        racing += 1
        row = [str(n), str(mode), str(W(0x0124))]
        for k in range(8):
            base = 0x1000 + k * 0x100
            row += [str(W(base+0x18)), str(W(base+0x1C)), str(w[base+0xA0]), str(w[base+0xAC]), str(W(base+0xEA)), str(w[base+0xC1]), "%02X" % w[base+0x68], str(w[base+0xC0])]
        f.write(",".join(row) + "\n")
    if n % 500 == 0: f.flush(); log("frame", n, "racing", racing, "mode", mode, "track", hex(W(0x0124)), "%.0fs" % (time.time()-t0))
f.close(); log("done", n, "racing frames", racing)
