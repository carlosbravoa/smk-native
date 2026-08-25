"""Can the oracle be pointed at a track other than the demo's?

Everything per-track (tilemap, theme, entity list at $85:C800+track*64)
is indexed off $0124, so forcing reads of it from boot should relocate
the whole race - the same trick as the un-demo hook.  Without this the
only entities we can watch are track 7's pipes, which do not move.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import log            # sets up the smktool import path
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def boot(track):
    r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
    b = Bus(bytes(r.data))
    c = CPU(b)
    c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]
    c.P = M_ | X_; c.S = 0x1FFF
    c.run_to(0x80805C, budget=8_000_000)
    orig = b.read
    def rd(bank, addr):
        lo = bank & 0x7F
        if (lo <= 0x3F or bank == 0x7E):
            if addr in (0x0E32, 0x0E33):
                return 0
            if addr == 0x0124 and track is not None:
                return track * 2      # $0124 is the track*2 form (LSR LSR)
        return orig(bank, addr)
    b.read = rd
    b.reg_reads[0x4218] = 0
    b.reg_reads[0x4219] = 0
    t0 = time.time()
    while time.time() - t0 < 900:
        c.run_frames_scanline(10)
        if (b.wram[0x36] // 2 in (1, 6)
                and sum(1 for k in range(128)
                        if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10):
            break
    return r, b, c

for track in (int(sys.argv[1]) if len(sys.argv) > 1 else 15,):
    r, b, c = boot(track)
    log("forced track: $0124 = $%02X" % b.wram[0x0124])
    for e in range(6):
        blk = 0x1800 + e * 0x40
        x = b.wram[blk+0x18] | b.wram[blk+0x19] << 8
        y = b.wram[blk+0x1C] | b.wram[blk+0x1D] << 8
        log("  ent %d at (%4d,%4d)  +$02=$%04X +$04=$%04X"
            % (e, x, y, b.wram[blk+2] | b.wram[blk+3] << 8,
               b.wram[blk+4] | b.wram[blk+5] << 8))
