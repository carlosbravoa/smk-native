"""Shared rig for the measurement labs.

Every battery needs the same four things: boot the oracle, install the
un-demo hook so player 1 is a real player, reach a running race, and
steer along the game's own flow field so the kart stays on the road while
we measure something else.  Keeping it here means a lab is just the
measurement.

These labs live in the repo on purpose: /tmp gets cleaned, and losing a
rig means re-deriving how to measure rather than just re-running it.
"""
import sys, os, time, math

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

P1 = 0x1000


def log(*a):
    print(*a, flush=True)


class Lab:
    def __init__(self, rom_path=None, settle=200):
        rom_path = rom_path or os.path.join(ROOT, "rom", "smk_usa.sfc")
        self.r = Rom.load(rom_path)
        self.b = Bus(bytes(self.r.data))
        self.c = CPU(self.b)
        self.c.PB, self.c.PC = 0x80, self.r.vectors()["emu.RESET"]
        self.c.P = M_ | X_
        self.c.S = 0x1FFF
        self.c.run_to(0x80805C, budget=8_000_000)
        # the un-demo hook: reads of $0E32/$0E33 return 0, so race setup
        # configures player 1 as a real player and the pad drives it
        orig = self.b.read
        def rd(bank, addr):
            lo = bank & 0x7F
            if (lo <= 0x3F or bank == 0x7E) and addr in (0x0E32, 0x0E33):
                return 0
            return orig(bank, addr)
        self.b.read = rd
        self.b.reg_reads[0x4218] = 0
        self.b.reg_reads[0x4219] = 0
        self.reach_race()
        if settle:
            self.c.run_frames_scanline(settle)

    # ---- state helpers -------------------------------------------------
    def w(self, a):
        return self.b.wram[a] | self.b.wram[a + 1] << 8

    def sw(self, a, v):
        self.b.wram[a] = v & 0xFF
        self.b.wram[a + 1] = (v >> 8) & 0xFF

    @staticmethod
    def s16(v):
        return v - 65536 if v > 32767 else v

    def speed(self):
        return self.s16(self.w(P1 + 0xEA))

    def pos(self):
        return self.s16(self.w(P1 + 0x18)), self.s16(self.w(P1 + 0x1C))

    def heading(self):
        return self.w(P1 + 0x2A)

    def slip(self):
        """velocity direction minus heading, signed angle units"""
        vx, vy = self.s16(self.w(P1 + 0x22)), self.s16(self.w(P1 + 0x24))
        if not (vx or vy):
            return 0
        va = int(math.atan2(vx, -vy) * 65536 / (2 * math.pi)) % 65536
        d = (va - self.heading()) & 0xFFFF
        return d - 65536 if d > 32768 else d

    def oam_visible(self):
        return sum(1 for k in range(128)
                   if self.b.oam[k * 4 + 1] not in (0, 0xF0, 0xE0))

    # ---- driving -------------------------------------------------------
    def reach_race(self, timeout=900):
        t0 = time.time()
        while time.time() - t0 < timeout:
            self.c.run_frames_scanline(10)
            if (self.w(0x36) // 2 in (1, 6) and self.oam_visible() >= 10
                    and any(self.s16(self.w(0x1000 + k * 0x100 + 0xEA)) > 32
                            for k in range(8))):
                return True
        raise RuntimeError("never reached a running race")

    def frame(self, hi=0, lo=0):
        self.b.reg_reads[0x4219] = hi
        self.b.reg_reads[0x4218] = lo
        self.c.run_frames_scanline(1)

    def flow(self, n, lo=0):
        """hold throttle and bang-bang steer toward the game's flow field"""
        for _ in range(n):
            x, y = self.pos()
            cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
            want = self.b.wram[0x14000 + cell] << 8
            d = (want - self.heading()) & 0xFFFF
            if d > 32768:
                d -= 65536
            pad = 0x80
            if d < -0x300:
                pad |= 0x02
            elif d > 0x300:
                pad |= 0x01
            self.frame(pad, lo)
        self.b.reg_reads[0x4218] = 0

    def pace(self, want=600, tries=3):
        """flow-steer until the kart is up to speed; True if it got there"""
        for _ in range(tries):
            self.flow(260)
            if self.speed() >= want:
                return True
        return self.speed() >= want

    # ---- the surface-behaviour table ($0B00) ---------------------------
    def surface_snapshot(self):
        return bytes(self.b.wram[0x0B00:0x0BC0])

    def surface_restore(self, snap):
        for i, v in enumerate(snap):
            self.b.wram[0x0B00 + i] = v

    def surface_fill(self, snap, cls):
        """make every driveable tile class `cls`, leaving solids alone"""
        for i, v in enumerate(snap):
            if not (v & 0x20) and not (v & 0x80):
                self.b.wram[0x0B00 + i] = cls
