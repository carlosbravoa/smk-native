"""Where does the start grid really come from?

$819212 is `sta $18,x` in the grid builder.  Catch every write from it,
with the direct page it was reading ($0C pointer, $12/$14 offsets, Y) and
the value it stored.  That turns the packing from a reading into a
measurement - $819207's two known offsets (4 and 10) cannot produce the
positions the game actually starts karts at.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import log
from track_force import boot

hits = []
import smktool.cpu as cpumod

r, b, c = None, None, None
def install(bus, cpu):
    orig = bus.write
    def wr(bank, addr, val):
        if cpu.PB == 0x81 and 0x9210 <= cpu.PC <= 0x9220:
            w = bus.wram
            hits.append((cpu.PC, bank, addr, val,
                         w[0x0C] | w[0x0D] << 8, w[0x0E],
                         w[0x12] | w[0x13] << 8, w[0x14] | w[0x15] << 8))
        orig(bank, addr, val)
    bus.write = wr

# boot with the hook installed as early as we can
import time
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
rom = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
bus = Bus(bytes(rom.data)); cpu = CPU(bus)
cpu.PB, cpu.PC = 0x80, rom.vectors()["emu.RESET"]; cpu.P = M_ | X_; cpu.S = 0x1FFF
cpu.run_to(0x80805C, budget=8_000_000)
orig_read = bus.read
def rd(bank, addr):
    if ((bank & 0x7F) <= 0x3F or bank == 0x7E) and addr in (0x0E32, 0x0E33):
        return 0
    return orig_read(bank, addr)
bus.read = rd
bus.reg_reads[0x4218] = 0; bus.reg_reads[0x4219] = 0
install(bus, cpu)
t0 = time.time()
while time.time() - t0 < 600 and len(hits) < 40:
    cpu.run_frames_scanline(10)
log("writes from the grid builder: %d" % len(hits))
log("  PC     addr   value   $0C ptr      $12  $14")
for pc, bank, addr, val, c0c, c0e, d12, d14 in hits[:20]:
    log("  $%04X  $%04X  %5d   $%02X:%04X   %3d  %3d" % (pc, addr, val, c0e, c0c, d12, d14))
