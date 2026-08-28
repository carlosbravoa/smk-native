"""The real starting grid, per track, straight from the game.

S2 (NOTES 142b) left the grid UNDECODED: it is computed, not stored, and
no offset makes the measured positions cell-aligned.  Until the routine
that builds it is found, the honest substitute is to read the grid the
game itself builds, on every course, and keep those numbers.

Reaching an arbitrary track is the NOTES 118 way - hook the reads of
$0150 (cup) and $0152 (course) so mode entry computes $0124 AND the
theme; forcing $0124 alone is the NOTES 059 trap.

The karts are read while the countdown still holds them, so these are
placement, not motion:  $1000 + k*$100 + $18 x, + $1C y, + $2A heading.

    python3 tools/labs/gridtable.py [cup course]        one course
    python3 tools/labs/gridtable.py                     all twenty
"""
import sys, os, time, json

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
P1 = 0x1000


def grid_for(cup, course, timeout=900):
    r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
    b = Bus(bytes(r.data)); c = CPU(b)
    c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]
    c.P = M_ | X_; c.S = 0x1FFF
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
    b.reg_reads[0x4218] = 0
    b.reg_reads[0x4219] = 0
    t0 = time.time()
    while time.time() - t0 < timeout:
        c.run_frames_scanline(10)
        if b.wram[0x36] // 2 in (1, 6) and any(
                b.wram[P1 + k * 0x100 + 0x18] or b.wram[P1 + k * 0x100 + 0x19]
                for k in range(8)):
            if sum(1 for k in range(128)
                   if b.oam[k * 4 + 1] not in (0, 0xF0, 0xE0)) >= 10:
                break
    else:
        raise RuntimeError("cup %d course %d never reached a race" % (cup, course))

    def w(a): return b.wram[a] | b.wram[a + 1] << 8
    karts = []
    for k in range(8):
        o = P1 + k * 0x100
        karts.append((w(o + 0x18), w(o + 0x1C), w(o + 0x2A), w(o + 0xEA)))
    return b.wram[0x0124], b.wram[0x0126], karts


def main():
    if len(sys.argv) == 3:
        want = [(int(sys.argv[1]), int(sys.argv[2]))]
    else:
        want = [(c, i) for c in range(4) for i in range(5)]
    out = {}
    for cup, course in want:
        t0 = time.time()
        track, theme, karts = grid_for(cup, course)
        print("cup %d course %d -> track %2d theme %d  (%.0fs)"
              % (cup, course, track, theme, time.time() - t0), flush=True)
        for k, (x, y, h, sp) in enumerate(karts):
            print("   kart %d  (%4d,%4d)  head $%04X  speed %d" % (k, x, y, h, sp),
                  flush=True)
        out[track] = [[x, y, h] for x, y, h, _ in karts]
        with open(os.path.join(ROOT, "tmp", "gridtable.json"), "w") as f:
            json.dump(out, f, indent=1, sort_keys=True)


main()
