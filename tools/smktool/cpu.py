"""A 65816 interpreter, used as an oracle for the native port.

The point is not to emulate a SNES.  It is to run one of the game's own
routines over a controlled RAM state and see exactly what it produces, so a
C reimplementation can be diffed against the original rather than judged by
feel.  Interrupts, PPU timing and rendering are deliberately absent.

Memory map (HiROM):
    $00-$3F:$0000-$1FFF   WRAM mirror of $7E:0000
    $00-$3F:$2000-$5FFF   hardware registers (stubbed, logged)
    $00-$0F:$6000-$7FFF   DSP-1 data / status
    $00-$3F:$8000-$FFFF   ROM
    $40-$7D:$0000-$FFFF   ROM
    $7E-$7F               WRAM (128 KB)
    $80-$BF               mirror of $00-$3F
    $C0-$FF               ROM
"""
from __future__ import annotations
from .opcodes import OPCODES
from .dsp1 import DSP1

# flag bits
C_, Z_, I_, D_, X_, M_, V_, N_ = 1, 2, 4, 8, 16, 32, 64, 128


class MemoryError_(Exception):
    pass


class Bus:
    def __init__(self, rom: bytes):
        self.rom = rom
        self.mask = len(rom) - 1
        self.wram = bytearray(0x20000)          # $7E0000-$7FFFFF
        self.regs: dict[int, int] = {}           # hardware writes, for inspection
        self.reg_reads: dict[int, int] = {}      # values to hand back on read
        self.dsp = DSP1()
        self.log_hw = False
        self.hw_writes: list[tuple[int, int]] = []

        # --- APU stub -------------------------------------------------
        # We do not emulate the SPC700.  The 65816 side only needs the IPL
        # handshake to succeed: the boot ROM signals "ready" as $AA/$BB in
        # ports 0/1, and the upload loop then waits for port 0 to echo the
        # byte counter it just wrote.  Echoing is enough to walk the game
        # through its entire sound upload.
        self.apu_out = [0xAA, 0xBB, 0x00, 0x00]
        self.apu_writes = 0

        # --- PPU status ----------------------------------------------
        # $4210 RDNMI: bit 7 set means "NMI occurred"; reading clears it.
        # $4212 HVBJOY: bit 7 vblank, bit 0 joypad busy.
        self.nmi_flag = False
        self.vblank = False
        self.irq_flag = False        # $4211 TIMEUP, cleared on read
        self.vcount = 0
        self.hcount = 0

    # ---- classification ----
    def _rom_pc(self, bank: int, addr: int) -> int:
        return (((bank & 0x3F) << 16) | addr) & self.mask

    def read(self, bank: int, addr: int) -> int:
        bank &= 0xFF
        addr &= 0xFFFF
        b = bank & 0x7F if bank < 0xC0 else bank
        if bank in (0x7E, 0x7F):
            return self.wram[((bank - 0x7E) << 16) | addr]
        low = bank & 0x7F
        if low <= 0x3F:
            if addr < 0x2000:
                return self.wram[addr]
            if 0x2140 <= addr <= 0x2143:
                return self.apu_out[addr & 3]
            if addr == 0x4210:
                v = 0x42 | (0x80 if self.nmi_flag else 0)
                self.nmi_flag = False
                return v
            if addr == 0x4211:
                v = 0x80 if self.irq_flag else 0
                self.irq_flag = False
                return v
            if addr == 0x4212:
                return (0x80 if self.vblank else 0)
            if addr == 0x213C:                     # OPHCT, latched H
                return self.hcount & 0xFF
            if addr == 0x213D:                     # OPVCT, latched V
                return self.vcount & 0xFF
            if addr < 0x6000:
                return self.reg_reads.get(addr, 0)
            if addr < 0x8000:
                if low <= 0x1F:
                    return self.dsp.read(addr)
                return 0
            return self.rom[self._rom_pc(bank, addr)]
        return self.rom[self._rom_pc(bank, addr)]

    def write(self, bank: int, addr: int, val: int) -> None:
        bank &= 0xFF
        addr &= 0xFFFF
        val &= 0xFF
        if bank in (0x7E, 0x7F):
            self.wram[((bank - 0x7E) << 16) | addr] = val
            return
        low = bank & 0x7F
        if low <= 0x3F:
            if addr < 0x2000:
                self.wram[addr] = val
                return
            if addr < 0x6000:
                self.regs[addr] = val
                if 0x2140 <= addr <= 0x2143:
                    # echo: what the IPL upload loop is waiting for
                    self.apu_out[addr & 3] = val
                    self.apu_writes += 1
                if self.log_hw:
                    self.hw_writes.append((addr, val))
                return
            if addr < 0x8000 and low <= 0x1F:
                self.dsp.write(addr, val)
                return
        # writes to ROM are ignored, as on hardware

    # convenience
    def read16(self, bank: int, addr: int) -> int:
        return self.read(bank, addr) | self.read(bank, (addr + 1) & 0xFFFF) << 8

    def w(self, addr24: int, val: int) -> None:
        self.write(addr24 >> 16, addr24 & 0xFFFF, val)

    def r(self, addr24: int) -> int:
        return self.read(addr24 >> 16, addr24 & 0xFFFF)


class CPU:
    """Native-mode 65816.  Emulation mode is not modelled: the game leaves it
    within a few instructions of reset and never returns."""

    def __init__(self, bus: Bus):
        self.bus = bus
        self.A = self.X = self.Y = 0
        self.S = 0x1FFF
        self.D = 0
        self.DB = 0
        self.PB = 0
        self.PC = 0
        self.P = M_ | X_          # 8-bit A and index by default
        self.cycles = 0
        self.stop = False
        self.trace: list[int] = []
        self.trace_enabled = False
        self.int_depth = 0        # nesting guard: do not re-enter a handler

    # ---- flag helpers ----
    @property
    def m8(self) -> bool: return bool(self.P & M_)
    @property
    def x8(self) -> bool: return bool(self.P & X_)

    def set_zn(self, v: int, eight: bool) -> None:
        if eight:
            self.P = (self.P & ~(Z_ | N_)) | (Z_ if (v & 0xFF) == 0 else 0) \
                     | (N_ if v & 0x80 else 0)
        else:
            self.P = (self.P & ~(Z_ | N_)) | (Z_ if (v & 0xFFFF) == 0 else 0) \
                     | (N_ if v & 0x8000 else 0)

    def setflag(self, bit: int, on: bool) -> None:
        self.P = (self.P | bit) if on else (self.P & ~bit)

    # ---- fetch ----
    def fetch8(self) -> int:
        v = self.bus.read(self.PB, self.PC)
        self.PC = (self.PC + 1) & 0xFFFF
        return v

    def fetch16(self) -> int:
        return self.fetch8() | self.fetch8() << 8

    def fetch24(self) -> int:
        return self.fetch8() | self.fetch8() << 8 | self.fetch8() << 16

    # ---- stack ----
    def push8(self, v: int) -> None:
        self.bus.write(0, self.S, v & 0xFF)
        self.S = (self.S - 1) & 0xFFFF

    def pull8(self) -> int:
        self.S = (self.S + 1) & 0xFFFF
        return self.bus.read(0, self.S)

    def push16(self, v: int) -> None:
        self.push8((v >> 8) & 0xFF); self.push8(v & 0xFF)

    def pull16(self) -> int:
        lo = self.pull8(); return lo | self.pull8() << 8

    # ---- memory through the data bank ----
    def load(self, addr24: int, eight: bool) -> int:
        b, a = addr24 >> 16, addr24 & 0xFFFF
        v = self.bus.read(b, a)
        if not eight:
            v |= self.bus.read(b, (a + 1) & 0xFFFF) << 8
        return v

    def store(self, addr24: int, val: int, eight: bool) -> None:
        b, a = addr24 >> 16, addr24 & 0xFFFF
        self.bus.write(b, a, val & 0xFF)
        if not eight:
            self.bus.write(b, (a + 1) & 0xFFFF, (val >> 8) & 0xFF)

    # ---- effective addresses -------------------------------------------
    def ea(self, mode: str) -> int:
        D, DB = self.D, self.DB
        if mode == "dp":
            return (D + self.fetch8()) & 0xFFFF
        if mode == "dpx":
            return (D + self.fetch8() + self.X) & 0xFFFF
        if mode == "dpy":
            return (D + self.fetch8() + self.Y) & 0xFFFF
        if mode == "idp":
            p = (D + self.fetch8()) & 0xFFFF
            return (DB << 16) | self.bus.read16(0, p)
        if mode == "idx":
            p = (D + self.fetch8() + self.X) & 0xFFFF
            return (DB << 16) | self.bus.read16(0, p)
        if mode == "idy":
            p = (D + self.fetch8()) & 0xFFFF
            return (((DB << 16) | self.bus.read16(0, p)) + self.Y) & 0xFFFFFF
        if mode == "idl":
            p = (D + self.fetch8()) & 0xFFFF
            return (self.bus.read16(0, p) | self.bus.read(0, (p + 2) & 0xFFFF) << 16)
        if mode == "idly":
            p = (D + self.fetch8()) & 0xFFFF
            base = self.bus.read16(0, p) | self.bus.read(0, (p + 2) & 0xFFFF) << 16
            return (base + self.Y) & 0xFFFFFF
        if mode == "abs":
            return (DB << 16) | self.fetch16()
        if mode == "abx":
            return (((DB << 16) | self.fetch16()) + self.X) & 0xFFFFFF
        if mode == "aby":
            return (((DB << 16) | self.fetch16()) + self.Y) & 0xFFFFFF
        if mode == "abl":
            return self.fetch24()
        if mode == "alx":
            return (self.fetch24() + self.X) & 0xFFFFFF
        if mode == "sr":
            return (self.S + self.fetch8()) & 0xFFFF
        if mode == "sry":
            p = (self.S + self.fetch8()) & 0xFFFF
            return (((DB << 16) | self.bus.read16(0, p)) + self.Y) & 0xFFFFFF
        raise MemoryError_(f"addressing mode {mode!r} has no effective address")

    def operand(self, mode: str, eight: bool) -> tuple[int, int | None]:
        """Returns (value, effective_address). address is None for immediates."""
        if mode in ("immM", "immX", "imm8"):
            if mode == "imm8" or eight:
                return self.fetch8(), None
            return self.fetch16(), None
        if mode == "acc":
            return self.A, None
        a = self.ea(mode)
        return self.load(a, eight), a

    # ---- arithmetic ----------------------------------------------------
    def adc(self, v: int) -> None:
        eight = self.m8
        a = self.A & (0xFF if eight else 0xFFFF)
        c = self.P & C_
        if self.P & D_:
            # binary-coded decimal, nibble by nibble
            width = 2 if eight else 4
            res = 0
            carry = c
            for i in range(width):
                d = ((a >> (4 * i)) & 0xF) + ((v >> (4 * i)) & 0xF) + carry
                carry = 0
                if d > 9:
                    d += 6
                    carry = 1
                    d &= 0xF
                res |= d << (4 * i)
            r = res
            self.setflag(C_, bool(carry))
            self.setflag(V_, False)
        else:
            r = a + v + c
            lim = 0x100 if eight else 0x10000
            sign = 0x80 if eight else 0x8000
            self.setflag(C_, r >= lim)
            self.setflag(V_, bool((~(a ^ v) & (a ^ r)) & sign))
            r &= lim - 1
        self.A = (self.A & 0xFF00 | r) if eight else r
        self.set_zn(r, eight)

    def sbc(self, v: int) -> None:
        eight = self.m8
        a = self.A & (0xFF if eight else 0xFFFF)
        mask = 0xFF if eight else 0xFFFF
        c = self.P & C_
        if self.P & D_:
            width = 2 if eight else 4
            res = 0
            borrow = 0 if c else 1
            for i in range(width):
                d = ((a >> (4 * i)) & 0xF) - ((v >> (4 * i)) & 0xF) - borrow
                borrow = 0
                if d < 0:
                    d -= 6
                    borrow = 1
                    d &= 0xF
                res |= d << (4 * i)
            r = res
            self.setflag(C_, not borrow)
            self.setflag(V_, False)
        else:
            iv = v ^ mask
            r = a + iv + c
            lim = mask + 1
            sign = 0x80 if eight else 0x8000
            self.setflag(C_, r >= lim)
            self.setflag(V_, bool((~(a ^ iv) & (a ^ r)) & sign))
            r &= mask
        self.A = (self.A & 0xFF00 | r) if eight else r
        self.set_zn(r, eight)

    def compare(self, reg: int, v: int, eight: bool) -> None:
        mask = 0xFF if eight else 0xFFFF
        a = reg & mask
        r = (a - v) & mask
        self.setflag(C_, a >= (v & mask))
        self.set_zn(r, eight)

    # ---- scanline timing ------------------------------------------------
    #
    # Not cycle accurate, and does not need to be.  What the game needs is
    # that IRQs land on the scanlines it asked for, in order, at the game's
    # frame rate.  Instructions per line is an average; the SNES CPU runs
    # about 59_600 cycles per frame over 262 lines at roughly 4 cycles an
    # instruction.
    LINES_PER_FRAME = 262
    VBLANK_LINE = 225
    INSTR_PER_LINE = 57

    def _irq_due(self) -> bool:
        """Does the IRQ configured in NMITIMEN fire on this line/dot?"""
        en = self.bus.regs.get(0x4200, 0)
        mode = en & 0x30
        if mode == 0:
            return False
        htime = (self.bus.regs.get(0x4207, 0)
                 | (self.bus.regs.get(0x4208, 0) & 1) << 8)
        vtime = (self.bus.regs.get(0x4209, 0)
                 | (self.bus.regs.get(0x420A, 0) & 1) << 8)
        if mode == 0x10:                       # H only, every line
            return True
        if mode == 0x20:                       # V only
            return self.bus.vcount == vtime
        return self.bus.vcount == vtime and htime < 340   # H and V

    def irq(self) -> None:
        self.int_depth += 1
        self.push8(self.PB)
        self.push16(self.PC)
        self.push8(self.P)
        self.P |= I_
        self.P &= ~D_
        self.bus.irq_flag = True
        vec = self.bus.read(0, 0xFFEE) | self.bus.read(0, 0xFFEF) << 8
        self.PB, self.PC = 0, vec

    def run_frame(self) -> None:
        """One video frame: 262 scanlines, with NMI at vblank and IRQ where
        the game asked for it."""
        for line in range(self.LINES_PER_FRAME):
            self.bus.vcount = line
            self.bus.vblank = line >= self.VBLANK_LINE

            # Never re-enter: on hardware the handler returns long before
            # the next event, and nesting here only corrupts the stack.
            if self.int_depth == 0:
                if line == self.VBLANK_LINE and (self.bus.regs.get(0x4200, 0) & 0x80):
                    self.nmi()
                elif self._irq_due() and not (self.P & I_):
                    self.irq()

            for _ in range(self.INSTR_PER_LINE):
                self.step()
                if self.stop:
                    return

    # ---- interrupts -----------------------------------------------------
    def nmi(self) -> None:
        """Dispatch a native-mode NMI, as the hardware does at vblank."""
        self.int_depth += 1
        self.push8(self.PB)
        self.push16(self.PC)
        self.push8(self.P)
        self.P |= I_
        self.P &= ~D_
        self.bus.nmi_flag = True
        vec = self.bus.read(0, 0xFFEA) | self.bus.read(0, 0xFFEB) << 8
        self.PB, self.PC = 0, vec

    def run_to(self, pc24: int, budget: int = 4_000_000) -> bool:
        """Step until the program counter reaches pc24."""
        for _ in range(budget):
            if ((self.PB << 16) | self.PC) == pc24:
                return True
            self.step()
            if self.stop:
                return False
        return False

    def run_frames_scanline(self, frames: int) -> int:
        """Frames driven by the scanline model (NMI + IRQ)."""
        for f in range(frames):
            self.run_frame()
            if self.stop:
                return f
        return frames

    def run_frames(self, frames: int, *, wait_pc: int = 0x80805C,
                   budget: int = 2_000_000) -> int:
        """Run whole frames at the game's own pacing.

        The main loop clears the vblank flag and spins at `wait_pc` until NMI
        sets it, so one frame is: fire NMI from the spin, then run until the
        spin is reached again.  That is exactly one simulation step per
        vblank, which is what 60.0988 Hz means here.
        """
        if not self.run_to(wait_pc, budget):
            return 0
        done = 0
        for _ in range(frames):
            self.bus.vblank = True
            self.nmi()
            ok = self.run_to(wait_pc, budget)
            self.bus.vblank = False
            if not ok:
                break
            done += 1
        return done

    # ---- run -----------------------------------------------------------
    SENTINEL = 0xFFFF00

    def call(self, addr24: int, long_call: bool = True, max_steps: int = 20_000_000):
        """Run one routine to its RTS/RTL and return the number of steps."""
        self.PB, self.PC = (addr24 >> 16) & 0xFF, addr24 & 0xFFFF
        if long_call:
            self.push8((self.SENTINEL >> 16) & 0xFF)
        self.push16((self.SENTINEL - 1) & 0xFFFF)
        steps = 0
        while steps < max_steps:
            if ((self.PB << 16) | self.PC) == self.SENTINEL:
                return steps
            self.step()
            if self.stop:
                return steps
            steps += 1
        raise MemoryError_(f"routine at ${addr24:06X} did not return in {max_steps} steps")

    # ---- one instruction ------------------------------------------------
    BRANCH_COND = {
        "BPL": (N_, False), "BMI": (N_, True),
        "BVC": (V_, False), "BVS": (V_, True),
        "BCC": (C_, False), "BCS": (C_, True),
        "BNE": (Z_, False), "BEQ": (Z_, True),
    }

    def _fix_index_width(self) -> None:
        if self.P & X_:
            self.X &= 0xFF
            self.Y &= 0xFF

    def step(self) -> None:
        if self.trace_enabled:
            self.trace.append((self.PB << 16) | self.PC)
        op = self.fetch8()
        mnem, mode = OPCODES[op]
        m8, x8 = self.m8, self.x8
        self.cycles += 1

        # --- loads / stores ---
        if mnem == "LDA":
            v, _ = self.operand(mode, m8)
            self.A = (self.A & 0xFF00 | v) if m8 else v
            self.set_zn(v, m8)
        elif mnem == "LDX":
            v, _ = self.operand(mode, x8)
            self.X = v
            self.set_zn(v, x8)
        elif mnem == "LDY":
            v, _ = self.operand(mode, x8)
            self.Y = v
            self.set_zn(v, x8)
        elif mnem == "STA":
            self.store(self.ea(mode), self.A, m8)
        elif mnem == "STX":
            self.store(self.ea(mode), self.X, x8)
        elif mnem == "STY":
            self.store(self.ea(mode), self.Y, x8)
        elif mnem == "STZ":
            self.store(self.ea(mode), 0, m8)

        # --- arithmetic ---
        elif mnem == "ADC":
            v, _ = self.operand(mode, m8); self.adc(v)
        elif mnem == "SBC":
            v, _ = self.operand(mode, m8); self.sbc(v)
        elif mnem == "CMP":
            v, _ = self.operand(mode, m8); self.compare(self.A, v, m8)
        elif mnem == "CPX":
            v, _ = self.operand(mode, x8); self.compare(self.X, v, x8)
        elif mnem == "CPY":
            v, _ = self.operand(mode, x8); self.compare(self.Y, v, x8)

        elif mnem in ("INC", "DEC"):
            d = 1 if mnem == "INC" else -1
            if mode == "acc":
                if m8:
                    self.A = (self.A & 0xFF00) | ((self.A + d) & 0xFF)
                    self.set_zn(self.A & 0xFF, True)
                else:
                    self.A = (self.A + d) & 0xFFFF
                    self.set_zn(self.A, False)
            else:
                a = self.ea(mode)
                v = self.load(a, m8)
                mask = 0xFF if m8 else 0xFFFF
                v = (v + d) & mask
                self.store(a, v, m8)
                self.set_zn(v, m8)
        elif mnem in ("INX", "DEX", "INY", "DEY"):
            d = 1 if mnem in ("INX", "INY") else -1
            mask = 0xFF if x8 else 0xFFFF
            if mnem in ("INX", "DEX"):
                self.X = (self.X + d) & mask; self.set_zn(self.X, x8)
            else:
                self.Y = (self.Y + d) & mask; self.set_zn(self.Y, x8)

        # --- logic ---
        elif mnem in ("AND", "ORA", "EOR"):
            v, _ = self.operand(mode, m8)
            a = self.A & (0xFF if m8 else 0xFFFF)
            r = (a & v) if mnem == "AND" else (a | v) if mnem == "ORA" else (a ^ v)
            self.A = (self.A & 0xFF00 | r) if m8 else r
            self.set_zn(r, m8)
        elif mnem == "BIT":
            v, _ = self.operand(mode, m8)
            a = self.A & (0xFF if m8 else 0xFFFF)
            self.setflag(Z_, (a & v) == 0)
            if mode not in ("immM", "immX"):     # immediate BIT touches only Z
                sign = 0x80 if m8 else 0x8000
                self.setflag(N_, bool(v & sign))
                self.setflag(V_, bool(v & (sign >> 1)))
        elif mnem in ("TSB", "TRB"):
            a = self.ea(mode)
            v = self.load(a, m8)
            acc = self.A & (0xFF if m8 else 0xFFFF)
            self.setflag(Z_, (acc & v) == 0)
            self.store(a, (v | acc) if mnem == "TSB" else (v & ~acc), m8)

        # --- shifts ---
        elif mnem in ("ASL", "LSR", "ROL", "ROR"):
            mask = 0xFF if m8 else 0xFFFF
            sign = 0x80 if m8 else 0x8000
            if mode == "acc":
                v = self.A & mask
            else:
                a = self.ea(mode); v = self.load(a, m8)
            c = self.P & C_
            if mnem == "ASL":
                self.setflag(C_, bool(v & sign)); r = (v << 1) & mask
            elif mnem == "LSR":
                self.setflag(C_, bool(v & 1)); r = v >> 1
            elif mnem == "ROL":
                r = ((v << 1) | (1 if c else 0)) & mask
                self.setflag(C_, bool(v & sign))
            else:
                r = (v >> 1) | (sign if c else 0)
                self.setflag(C_, bool(v & 1))
            if mode == "acc":
                self.A = (self.A & 0xFF00 | r) if m8 else r
            else:
                self.store(a, r, m8)
            self.set_zn(r, m8)

        # --- branches and jumps ---
        elif mnem in self.BRANCH_COND:
            bit, want = self.BRANCH_COND[mnem]
            off = self.fetch8()
            if bool(self.P & bit) == want:
                off = off - 0x100 if off & 0x80 else off
                self.PC = (self.PC + off) & 0xFFFF
        elif mnem == "BRA":
            off = self.fetch8()
            off = off - 0x100 if off & 0x80 else off
            self.PC = (self.PC + off) & 0xFFFF
        elif mnem == "BRL":
            off = self.fetch16()
            off = off - 0x10000 if off & 0x8000 else off
            self.PC = (self.PC + off) & 0xFFFF
        elif mnem == "JMP":
            if mode == "abs":
                self.PC = self.fetch16()
            elif mode == "abl":
                t = self.fetch24(); self.PB, self.PC = t >> 16, t & 0xFFFF
            elif mode == "ind":
                p = self.fetch16(); self.PC = self.bus.read16(0, p)
            elif mode == "iax":
                p = (self.fetch16() + self.X) & 0xFFFF
                self.PC = self.bus.read(self.PB, p) | self.bus.read(self.PB, (p + 1) & 0xFFFF) << 8
            elif mode == "ial":
                p = self.fetch16()
                self.PC = self.bus.read16(0, p)
                self.PB = self.bus.read(0, (p + 2) & 0xFFFF)
        elif mnem == "JML":
            if mode == "abl":
                t = self.fetch24(); self.PB, self.PC = t >> 16, t & 0xFFFF
            else:                                    # [abs]
                p = self.fetch16()
                self.PC = self.bus.read16(0, p)
                self.PB = self.bus.read(0, (p + 2) & 0xFFFF)
        elif mnem == "JSR":
            if mode == "abs":
                t = self.fetch16()
                self.push16((self.PC - 1) & 0xFFFF)
                self.PC = t
            else:                                    # (abs,x)
                p = (self.fetch16() + self.X) & 0xFFFF
                self.push16((self.PC - 1) & 0xFFFF)
                self.PC = self.bus.read(self.PB, p) | self.bus.read(self.PB, (p + 1) & 0xFFFF) << 8
        elif mnem == "JSL":
            t = self.fetch24()
            self.push8(self.PB)
            self.push16((self.PC - 1) & 0xFFFF)
            self.PB, self.PC = t >> 16, t & 0xFFFF
        elif mnem == "RTS":
            self.PC = (self.pull16() + 1) & 0xFFFF
        elif mnem == "RTL":
            self.PC = (self.pull16() + 1) & 0xFFFF
            self.PB = self.pull8()
        elif mnem == "RTI":
            self.P = self.pull8()
            self.PC = self.pull16()
            self.PB = self.pull8()
            self._fix_index_width()
            if self.int_depth:
                self.int_depth -= 1

        # --- stack ---
        elif mnem == "PHA":
            self.push8(self.A & 0xFF) if m8 else self.push16(self.A)
        elif mnem == "PLA":
            v = self.pull8() if m8 else self.pull16()
            self.A = (self.A & 0xFF00 | v) if m8 else v
            self.set_zn(v, m8)
        elif mnem in ("PHX", "PHY"):
            v = self.X if mnem == "PHX" else self.Y
            self.push8(v & 0xFF) if x8 else self.push16(v)
        elif mnem in ("PLX", "PLY"):
            v = self.pull8() if x8 else self.pull16()
            if mnem == "PLX": self.X = v
            else: self.Y = v
            self.set_zn(v, x8)
        elif mnem == "PHP": self.push8(self.P)
        elif mnem == "PLP": self.P = self.pull8(); self._fix_index_width()
        elif mnem == "PHB": self.push8(self.DB)
        elif mnem == "PLB": self.DB = self.pull8(); self.set_zn(self.DB, True)
        elif mnem == "PHK": self.push8(self.PB)
        elif mnem == "PHD": self.push16(self.D)
        elif mnem == "PLD": self.D = self.pull16(); self.set_zn(self.D, False)
        elif mnem == "PEA": self.push16(self.fetch16())
        elif mnem == "PEI":
            p = (self.D + self.fetch8()) & 0xFFFF
            self.push16(self.bus.read16(0, p))
        elif mnem == "PER":
            off = self.fetch16()
            self.push16((self.PC + (off - 0x10000 if off & 0x8000 else off)) & 0xFFFF)

        # --- transfers ---
        elif mnem == "TAX":
            self.X = self.A & 0xFF if x8 else self.A; self.set_zn(self.X, x8)
        elif mnem == "TAY":
            self.Y = self.A & 0xFF if x8 else self.A; self.set_zn(self.Y, x8)
        elif mnem == "TXA":
            v = self.X & 0xFF if m8 else self.X
            self.A = (self.A & 0xFF00 | v) if m8 else v; self.set_zn(v, m8)
        elif mnem == "TYA":
            v = self.Y & 0xFF if m8 else self.Y
            self.A = (self.A & 0xFF00 | v) if m8 else v; self.set_zn(v, m8)
        elif mnem == "TXY":
            self.Y = self.X & (0xFF if x8 else 0xFFFF); self.set_zn(self.Y, x8)
        elif mnem == "TYX":
            self.X = self.Y & (0xFF if x8 else 0xFFFF); self.set_zn(self.X, x8)
        elif mnem == "TSX":
            self.X = self.S & (0xFF if x8 else 0xFFFF); self.set_zn(self.X, x8)
        elif mnem == "TXS": self.S = self.X & 0xFFFF
        elif mnem == "TCS": self.S = self.A & 0xFFFF
        elif mnem == "TSC": self.A = self.S & 0xFFFF; self.set_zn(self.A, False)
        elif mnem == "TCD": self.D = self.A & 0xFFFF; self.set_zn(self.D, False)
        elif mnem == "TDC": self.A = self.D & 0xFFFF; self.set_zn(self.A, False)
        elif mnem == "XBA":
            self.A = ((self.A << 8) | (self.A >> 8)) & 0xFFFF
            self.set_zn(self.A & 0xFF, True)

        # --- flags ---
        elif mnem == "CLC": self.setflag(C_, False)
        elif mnem == "SEC": self.setflag(C_, True)
        elif mnem == "CLI": self.setflag(I_, False)
        elif mnem == "SEI": self.setflag(I_, True)
        elif mnem == "CLD": self.setflag(D_, False)
        elif mnem == "SED": self.setflag(D_, True)
        elif mnem == "CLV": self.setflag(V_, False)
        elif mnem == "REP":
            self.P &= ~self.fetch8() & 0xFF
        elif mnem == "SEP":
            self.P |= self.fetch8(); self._fix_index_width()
        elif mnem == "XCE":
            pass                      # native throughout; emulation not modelled

        # --- block moves ---
        elif mnem in ("MVN", "MVP"):
            dst = self.fetch8(); src = self.fetch8()
            step = 1 if mnem == "MVN" else -1
            n = (self.A & 0xFFFF) + 1
            for _ in range(n):
                self.bus.write(dst, self.Y & 0xFFFF,
                               self.bus.read(src, self.X & 0xFFFF))
                self.X = (self.X + step) & 0xFFFF
                self.Y = (self.Y + step) & 0xFFFF
            self.A = 0xFFFF
            self.DB = dst

        # --- misc ---
        elif mnem == "NOP" or mnem == "WDM":
            if mnem == "WDM": self.fetch8()
        elif mnem in ("STP", "WAI"):
            self.stop = True
        elif mnem in ("BRK", "COP"):
            self.fetch8()
            self.stop = True
        else:
            raise MemoryError_(f"unimplemented opcode ${op:02X} {mnem} at "
                               f"${self.PB:02X}:{self.PC - 1:04X}")
