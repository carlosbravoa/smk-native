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
import math
import os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORLD = 1024 << 16
RACE_MODE = 6


def s16(v: int) -> int:
    return v - 0x10000 if v > 0x7FFF else v


KART0 = 0x1000            # kart blocks are $100 apart
KART_STRIDE = 0x100
KARTS = 8


def boot_into_race(rom: Rom, settle: int = 700):
    """Reach the game's own attract-mode demo race, where karts really drive.

    Forcing $32 into race mode reaches a race SCENE but the karts never get
    a start signal - every slot sits at speed 0 (NOTES 050).  The previous
    version of this check did exactly that and then followed $B4, the
    current-object pointer, so it verified the motion of whatever object the
    game happened to be processing.  Waiting for the demo is slower and
    actually exercises karts.
    """
    bus = Bus(bytes(rom.data))
    cpu = CPU(bus)
    cpu.PB, cpu.PC = 0x80, rom.vectors()["emu.RESET"]
    cpu.P = M_ | X_
    cpu.S = 0x1FFF
    if not cpu.run_to(0x80805C, budget=8_000_000):
        raise RuntimeError("the ROM never reached its main loop")

    w = lambda a: bus.wram[a] | bus.wram[a + 1] << 8
    bus.reg_reads[0x4218] = 0
    bus.reg_reads[0x4219] = 0

    def oam_visible():
        return sum(1 for i in range(128)
                   if bus.oam[i * 4 + 1] not in (0, 0xF0, 0xE0))

    deadline = time.time() + 900
    while time.time() < deadline:
        cpu.run_frames_scanline(10)
        moving = any(s16(w(KART0 + k * KART_STRIDE + 0xEA)) > 32
                     for k in range(KARTS))
        if w(0x36) // 2 in (1, 6) and oam_visible() >= 10 and moving:
            break
    else:
        raise RuntimeError("the demo race never started")
    cpu.run_frames_scanline(settle)
    return cpu, bus

def capture(cpu, bus, frames: int):
    """Every kart slot, every frame.

    Deliberately NOT keyed on $B4: that is the game's current-object
    pointer, reloaded as it walks its object list, so its value depends on
    when in the frame you read it (NOTES 049).  Using it picks an arbitrary
    object - which is how this check silently tested the wrong thing.
    """
    w = lambda a: bus.wram[a] | bus.wram[a + 1] << 8
    out = []
    for _ in range(frames):
        frame = []
        for k in range(KARTS):
            base = KART0 + k * KART_STRIDE
            frame.append({
                "xi": w(base + 0x18), "xf": w(base + 0x16),
                "yi": w(base + 0x1C), "yf": w(base + 0x1A),
                "ang": w(base + 0x2A),
                "vx": s16(w(base + 0x22)), "vy": s16(w(base + 0x24)),
                "speed": s16(w(base + 0xEA)),
                "z": w(base + 0x20),
            })
        out.append({"f": w(0x34), "k": frame})
        cpu.run_frames_scanline(1)
    return out


def driving(a, b):
    """True when a kart is in free motion between these two frames: moving,
    on the ground, and with a velocity that matches its own heading - which
    is what $80F8CF computes.  Karts being scripted, bounced or spun fail
    this and are reported separately rather than failing the check."""
    if b["speed"] <= 0 or b["z"] or a["z"]:
        return False
    ang = b["ang"] * 2.0 * math.pi / 65536.0
    want_x = math.sin(ang) * b["speed"]
    want_y = -math.cos(ang) * b["speed"]
    return abs(b["vx"] - want_x) <= 2 and abs(b["vy"] - want_y) <= 2


def main():
    base_rom = os.path.join(ROOT, "rom", "smk_usa.sfc")
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    if not os.path.exists(base_rom):
        print(f"skipping: {base_rom} not present")
        return 0

    t0 = time.time()
    rom = Rom.load(base_rom)
    cpu, bus = boot_into_race(rom)
    print(f"race running, past the countdown ({time.time()-t0:.0f}s)")
    rows = capture(cpu, bus, frames)

    ok = bad = 0
    worst = 0
    skipped = 0
    per_kart = []
    for k in range(KARTS):
        kok = kbad = 0
        for ra, rb in zip(rows, rows[1:]):
            a, b = ra["k"][k], rb["k"][k]
            if not driving(a, b):
                skipped += 1
                continue
            for vi, vf, vv in (("xi", "xf", "vx"), ("yi", "yf", "vy")):
                p0 = (a[vi] << 16) | a[vf]
                p1 = (b[vi] << 16) | b[vf]
                # the game updates velocity first, then integrates position
                # with it, so frame n -> n+1 uses frame n+1's velocity
                pred = (p0 + (b[vv] << 8)) & (WORLD - 1)
                d = (p1 - pred) & (WORLD - 1)
                if d > WORLD // 2:
                    d -= WORLD
                if d == 0:
                    kok += 1
                else:
                    kbad += 1
                    worst = max(worst, abs(d))
        per_kart.append((kok, kbad))
        ok += kok
        bad += kbad

    print(f"captured {len(rows)} frames x {KARTS} karts")
    for k, (kok, kbad) in enumerate(per_kart):
        if kok or kbad:
            print(f"  kart {k}: {kok:5d} exact, {kbad:5d} differ")
    print(f"position += velocity<<8 : {ok} exact, {bad} differ, "
          f"worst {worst/65536:.4f} px "
          f"({skipped} kart-frames skipped as not freely driving)")
    if ok == 0:
        print("INCONCLUSIVE: no kart was in free motion in this window")
        return 1
    if bad:
        print("FAIL: src/kart.c does not reproduce the game's motion")
        return 1
    print("OK: the ported kinematics reproduce the game exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
