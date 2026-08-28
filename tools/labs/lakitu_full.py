"""The whole Lakitu sequence: the drop, the lamps, the release, the
fly-away.

Records his OAM entry every frame from the frame $0146 is armed until he
is well clear of the screen, which is the fixture src/lakitu.c is checked
against.  His position generator is NOT decoded - the one address that
tracked the sprite turned out to be the OAM shadow buffer at $0220 - so
what the port carries is this trajectory, measured.
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TMP = os.path.join(ROOT, "tmp")
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

rows = []
for f in range(520):
    o = b.oam
    ly = o[11*4+1]; ly = ly if ly < 128 else ly - 256
    rows.append((s16(w(0x0146)), s16(w(0x0142)), ly,
                 o[11*4+2], o[8*4+2], o[9*4+2], o[10*4+2], o[11*4+1], o[11*4]))
    b.reg_reads[0x4219] = 0; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)

with open(os.path.join(TMP, "lakitu_full.txt"), "w") as fh:
    fh.write("i c146 c142 y tile lamp0 lamp1 lamp2 oamy oamx\n")
    for i, rr in enumerate(rows):
        fh.write("%d %d %d %d %d %d %d %d %d %d\n" % ((i,) + rr))
print("frames %d, $0146 %d..%d" % (len(rows), rows[0][0], rows[-1][0]), flush=True)

