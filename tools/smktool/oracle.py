"""Run one of the game's own routines over a controlled state.

This is the verification backbone for the native port: instead of judging a
reimplementation by feel, run the original 65816 routine and the C version
over the same inputs and diff the outputs.

    o = Oracle(rom)
    res = o.call(0x84E09E, a=0x86, x=0x0000, y=0x8000, m8=False, x8=False)
    res.wram(0x7F0000, 32)
"""
from __future__ import annotations
from dataclasses import dataclass, field

from .cpu import CPU, Bus, M_, X_, C_, Z_, N_, V_, D_
from .rom import Rom


@dataclass
class Result:
    cpu: CPU
    bus: Bus
    steps: int

    @property
    def A(self): return self.cpu.A
    @property
    def X(self): return self.cpu.X
    @property
    def Y(self): return self.cpu.Y
    @property
    def P(self): return self.cpu.P

    def wram(self, addr24: int, n: int) -> bytes:
        base = ((addr24 >> 16) - 0x7E) << 16 | (addr24 & 0xFFFF)
        return bytes(self.bus.wram[base:base + n])

    def word(self, addr24: int) -> int:
        b = self.wram(addr24, 2)
        return b[0] | b[1] << 8

    def flags(self) -> str:
        p = self.cpu.P
        return "".join(c if p & b else "-" for c, b in
                       zip("NVMXDIZC", (N_, V_, M_, X_, D_, 4, Z_, C_)))

    @property
    def dsp_calls(self):
        return self.bus.dsp.calls


class Oracle:
    def __init__(self, rom: Rom):
        self.rom = bytes(rom.data)

    def call(self, addr24: int, *, a=0, x=0, y=0, d=0, db=None, s=0x1FFF,
             m8=False, x8=False, wram: dict[int, bytes] | None = None,
             regs: dict[int, int] | None = None, long_call=True,
             max_steps=20_000_000, trace=False) -> Result:
        bus = Bus(self.rom)
        if wram:
            for addr, data in wram.items():
                base = ((addr >> 16) - 0x7E) << 16 | (addr & 0xFFFF) \
                       if addr >= 0x7E0000 else addr
                bus.wram[base:base + len(data)] = data
        if regs:
            bus.reg_reads.update(regs)
        cpu = CPU(bus)
        cpu.A, cpu.X, cpu.Y, cpu.D, cpu.S = a, x, y, d, s
        cpu.DB = (addr24 >> 16) if db is None else db
        cpu.P = (M_ if m8 else 0) | (X_ if x8 else 0)
        cpu.trace_enabled = trace
        steps = cpu.call(addr24, long_call=long_call, max_steps=max_steps)
        return Result(cpu, bus, steps)
