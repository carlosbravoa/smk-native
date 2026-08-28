"""When does the green lamp light, and when are the karts released?

The OAM script says the green comes on at $0146 = -27 while NOTES 145
says the release is at $0146 = 0.  One of those is not what it looks
like, so watch the field: the AI karts accelerate on the release frame.
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
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
        if addr == 0x0152: return 0
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
def w(a): return b.wram[a] | b.wram[a + 1] << 8
def s16(v): return v - 65536 if v > 32767 else v

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

print("$0146   $0142  lamps      $3A  speeds 0..7   P1 $EA $C2", flush=True)
for f in range(420):
    t146 = s16(w(0x0146))
    lamps = tuple(b.oam[k*4+2] for k in (8, 9, 10))
    sp = [s16(w(0x1000 + k*0x100 + 0xEA)) for k in range(8)]
    if t146 > -60 or f == 0 or any(sp):
        print("%6d %6d  $%02X $%02X $%02X  %3d  %s   %d %d"
              % (t146, s16(w(0x0142)), lamps[0], lamps[1], lamps[2],
                 w(0x3A), ' '.join("%4d" % s for s in sp),
                 s16(w(0x10EA)), s16(w(0x10C2))), flush=True)
    if t146 > 40: break
    b.reg_reads[0x4219] = 0
    b.reg_reads[0x4218] = 0x80        # hold the throttle like a player
    c.run_frames_scanline(1)
