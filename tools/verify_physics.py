#!/usr/bin/env python3
"""Verify the ported kart kinematics against the game actually running.

Boots the ROM in the interpreter, forces race mode, captures the real kart
state for a few hundred frames, then checks that the rule implemented in
src/kart.c - `position += velocity << 8`, with position 16.16 and velocity
8.8 - reproduces the game's own frame-to-frame motion exactly.

No trace is committed: it is regenerated from the user's ROM every run, so
no game data lives in this repository.
"""
from __future__ import annotations
import os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORLD = 1024 << 16
RACE_MODE = 6


def s16(v: int) -> int:
    return v - 0x10000 if v > 0x7FFF else v


def boot_into_race(rom: Rom, track: int = 0, settle: int = 150):
    bus = Bus(bytes(rom.data))
    cpu = CPU(bus)
    cpu.PB, cpu.PC = 0x80, rom.vectors()["emu.RESET"]
    cpu.P = M_ | X_
    cpu.S = 0x1FFF
    if not cpu.run_to(0x80805C, budget=8_000_000):
        raise RuntimeError("the ROM never reached its main loop")

    w = lambda a: bus.wram[a] | bus.wram[a + 1] << 8
    def sw(a, v):
        bus.wram[a] = v & 0xFF
        bus.wram[a + 1] = (v >> 8) & 0xFF

    bus.reg_reads[0x4218] = 0
    bus.reg_reads[0x4219] = 0
    cpu.run_frames_scanline(30)

    # $32 is the pending-mode variable; $81E09A copies it into $36 and
    # re-enables NMI/IRQ.  This is the game's own way of changing mode.
    sw(0x0124, track)
    sw(0x32, RACE_MODE * 2)
    for _ in range(40):
        cpu.run_frames_scanline(5)
        if w(0x36) // 2 == RACE_MODE:
            break
    else:
        raise RuntimeError("never entered race mode")
    cpu.run_frames_scanline(settle)
    return cpu, bus, w(0xB4)          # $B4 holds the active kart's base


def capture(cpu, bus, base: int, frames: int):
    w = lambda a: bus.wram[a] | bus.wram[a + 1] << 8
    out = []
    for _ in range(frames):
        out.append({
            "f": w(0x34),
            "xi": w(base + 0x18), "xf": w(base + 0x16),
            "yi": w(base + 0x1C), "yf": w(base + 0x1A),
            "ang": w(base + 0x2A),
            "vx": s16(w(base + 0x22)), "vy": s16(w(base + 0x24)),
            "speed": s16(w(base + 0xEA)),
            "surf": bus.wram[base + 0x68],
        })
        cpu.run_frames_scanline(1)
    return out


def main():
    base_rom = os.path.join(ROOT, "rom", "smk_usa.sfc")
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    if not os.path.exists(base_rom):
        print(f"skipping: {base_rom} not present")
        return 0

    t0 = time.time()
    rom = Rom.load(base_rom)
    cpu, bus, kart = boot_into_race(rom)
    print(f"race mode reached, kart state at ${kart:04X} "
          f"({time.time()-t0:.0f}s)")
    rows = capture(cpu, bus, kart, frames)

    ok = bad = 0
    worst = 0
    for a, b in zip(rows, rows[1:]):
        for vi, vf, vv in (("xi", "xf", "vx"), ("yi", "yf", "vy")):
            p0 = (a[vi] << 16) | a[vf]
            p1 = (b[vi] << 16) | b[vf]
            # the game updates velocity first, then integrates position with
            # it, so the step from frame n to n+1 uses frame n+1's velocity
            pred = (p0 + (b[vv] << 8)) & (WORLD - 1)
            d = (p1 - pred) & (WORLD - 1)
            if d > WORLD // 2:
                d -= WORLD
            if d == 0:
                ok += 1
            else:
                bad += 1
                worst = max(worst, abs(d))

    moved = any(r["vy"] or r["vx"] for r in rows)
    print(f"captured {len(rows)} frames, kart {'moving' if moved else 'STATIONARY'}")
    print(f"position += velocity<<8 : {ok} exact, {bad} differ"
          + (f", worst {worst/65536:.4f} px" if bad else ""))
    if not moved:
        print("FAIL: the kart never moved, so nothing was verified")
        return 1
    if bad:
        print("FAIL: src/kart.c does not reproduce the game's motion")
        return 1
    print("OK: the ported kinematics reproduce the game exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
