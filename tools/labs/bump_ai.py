"""Is a bump between two AI karts different from one involving the player?

The user, after playing it: "between them bouncing is different, less
aggressive."  $819C28 does branch on `cpx #$1000` - it reads $2C for the
player and $32 for everyone else - so the response is not blind to who
is in it, and that is worth measuring rather than arguing about.

Same setup twice: the same two velocities, the same separation, once
with P1 in the pair and once between two AI karts.
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
        if addr in (0x0150, 0x0152): return 0
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
def w(a): return b.wram[a] | b.wram[a + 1] << 8
def sw(a, v): b.wram[a] = v & 0xFF; b.wram[a + 1] = (v >> 8) & 0xFF
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
    if s16(w(0x0146)) == 0 and b.wram[0x3A] == 6: break
for _ in range(120):
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)

def clear_pair(A, B):
    for base in (A, B):
        sw(base + 0x5E, 0); sw(base + 0x50, 0); sw(base + 0x52, 0)
        sw(base + 0x10, w(base + 0x10) & ~0x1000)

def trial(A, B, wa, wb, va, vb, anchor):
    """put A on top of B with the given velocities; anchor is where to
    hold them apart beforehand so nothing is latched"""
    sw(A + 0x1C, (w(anchor + 0x1C) + 64) & 0xFFFF)
    for _ in range(4):
        clear_pair(A, B)
        b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)
    clear_pair(A, B)
    sw(A + 0x4E, wa); sw(B + 0x4E, wb)
    sw(A + 0x18, w(B + 0x18)); sw(A + 0x1C, w(B + 0x1C))
    sw(A + 0x22, va[0] & 0xFFFF); sw(A + 0x24, va[1] & 0xFFFF)
    sw(B + 0x22, vb[0] & 0xFFFF); sw(B + 0x24, vb[1] & 0xFFFF)
    sw(A + 0xEA, int((va[0]**2 + va[1]**2) ** 0.5))
    sw(B + 0xEA, int((vb[0]**2 + vb[1]**2) ** 0.5))
    b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)
    return (s16(w(A+0x22)), s16(w(A+0x24)), s16(w(B+0x22)), s16(w(B+0x24)),
            w(A+0xE2), w(B+0xE2))

CASES = [("equal  ", 0x1A, 0x1A), ("heavy+1", 0x1B, 0x1A), ("heavy+2", 0x1B, 0x19)]
for tag, wa, wb in CASES:
    for who, A, B in (("P1 vs AI", 0x1000, 0x1200), ("AI vs AI", 0x1300, 0x1200)):
        a1, a2, b1, b2, ea, eb = trial(A, B, wa, wb, (0, -600), (0, -400), B)
        print("%s  %s | in (0,-600)/(0,-400) -> A (%5d,%5d) B (%5d,%5d) | E2 %04X %04X"
              % (tag, who, a1, a2, b1, b2, ea, eb), flush=True)
