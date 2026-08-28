"""The rev machine, driven by a real throttle: who writes $C2, with what
delta, and what happens at the line.

The pad's accelerate is B, which is bit 7 of the HIGH byte $4219 - not
$4218, which is A/X/L/R.  Holding $4219 = $80 from the countdown's arm is
the "held from the start" case NOTES 142a describes.

    python3 tools/labs/rev.py [press_at]   press_at = $0146 to press from
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
press_at = int(sys.argv[1]) if len(sys.argv) > 1 else -336

r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr in (0x0150, 0x0152): return 0
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
def w(a): return b.wram[a] | b.wram[a + 1] << 8
def s16(v): return v - 65536 if v > 32767 else v

writers = {}
orig_w = b.write
def wr(bank, addr, val):
    if addr in (0x10C2, 0x10C3) and ((bank & 0x7F) <= 0x3F or bank == 0x7E):
        writers.setdefault((c.PB, c.PC), 0)
        writers[(c.PB, c.PC)] += 1
    orig_w(bank, addr, val)
b.write = wr

t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and (b.wram[0x1018] or b.wram[0x1019]):
        if sum(1 for k in range(128)
               if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break
for _ in range(600):
    b.reg_reads[0x4219] = 0; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    if s16(w(0x0146)) < 0: break

print("press from $0146 = %d" % press_at, flush=True)
print("$0146  $38 rev     dE0   E2   spd  EE   AC", flush=True)
prev = None
for f in range(460):
    t146 = s16(w(0x0146))
    rev = s16(w(0x10C2))
    d = "" if prev is None else "%+5d" % (rev - prev)
    if t146 > -30 or rev != prev or f < 4:
        print("%5d %4d %6d %5s  %04X %04X %5d %4d %04X"
              % (t146, b.wram[0x38], rev, d, w(0x10E0), w(0x10E2),
                 s16(w(0x10EA)), s16(w(0x10EE)), w(0x10AC)), flush=True)
    prev = rev
    if t146 > 60: break
    b.reg_reads[0x4219] = 0x80 if t146 >= press_at else 0
    b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
print("writers of $C2:", ["$%02X:%04X x%d" % (a, p, n)
                          for (a, p), n in sorted(writers.items())], flush=True)
