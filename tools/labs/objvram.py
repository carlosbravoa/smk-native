"""Where the theme's object sheet lands in OBJ VRAM, and what each tile is.

The block fields do not say which sprites are the Thwomp - $30 stays at
the parked $0140 the whole way in - so identify them the other way round:
the Bowser Castle object sheet is 57 known tiles decompressed from
$81EBD3's pointer, so find that run inside OBJ VRAM and any OAM sprite
pointing into it IS an object.  That also gives the shadow's own tiles.
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.compress import decompress
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cup, course, want = 0, 3, 17
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
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
while time.time() - t0 < 1800:
    c.run_frames_scanline(10)
    if b.wram[0x0124] == want and b.wram[0x36] // 2 in (1, 6):
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0,0xF0,0xE0)) >= 10: break
w = b.wram
def rw(a): return w[a] | w[a+1] << 8
print("reached race: track %d" % w[0x0124], flush=True)
for _ in range(700):
    b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)

vram = bytes(b.vram)
out = os.path.join(ROOT, "tmp")
open(os.path.join(out, "vram.bin"), "wb").write(vram)
print("VRAM %d bytes saved" % len(vram))
tp = r.snes_to_pc(0x81EBD3) + 6 * 3
src = r.data[tp] | (r.data[tp+1] << 8) | (r.data[tp+2] << 16)
sheet, _ = decompress(bytes(r.data), r.snes_to_pc(src), max_out=0x10000)
sheet = bytes(sheet)
print("theme 6 sheet $%06X, %d bytes (%d tiles)" % (src, len(sheet), len(sheet)//32))
i = vram.find(sheet[:32*8])
print("sheet found in VRAM at byte $%04X" % i if i >= 0 else "sheet NOT contiguous in VRAM")
if i >= 0:
    n = 0
    while n < len(sheet)//32 and vram[i+n*32:i+n*32+32] == sheet[n*32:n*32+32]: n += 1
    print("  %d of %d tiles match contiguously" % (n, len(sheet)//32))
obsel = b.regs.get(0x2101, 0)
base = (obsel & 7) * 0x4000
print("OBSEL $%02X -> OBJ tile 0 at VRAM byte $%04X" % (obsel, base))
if i >= 0:
    print("  => object sheet tile 0 is OBJ tile $%03X" % ((i - base) // 32))
print("\nOAM (tile, and which sheet tile that is):")
lo = (i - base)//32 if i >= 0 else None
for k in range(128):
    x, y, t_, at = b.oam[k*4:k*4+4]
    hi = b.oam[512 + (k >> 2)]
    xh = (hi >> ((k & 3) * 2)) & 1
    big = (hi >> ((k & 3) * 2 + 1)) & 1
    if y in (0, 0xF0, 0xE0): continue
    t = t_ | ((at & 1) << 8)
    tag = ""
    if lo is not None and lo <= t < lo + 57: tag = "  <== object sheet tile %d" % (t - lo)
    print("  k%3d y %3d x %4d tile $%03X pal %d pri %d %s%s %s%s"
          % (k, y, x - (256 if xh else 0), t, (at >> 1) & 7, (at >> 4) & 3,
             "H" if (at >> 6) & 1 else "-", "V" if (at >> 7) & 1 else "-",
             "16x16" if big else "8x8", tag), flush=True)
