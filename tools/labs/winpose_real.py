#!/usr/bin/env python3
"""The winner's celebration pose, identified from the frames the game
uploads.  Force the last lap (as lakitu_flag.py), put P1 FIRST in the
order table so the finish is a win, and after the crossing compare the
kart's 16 uploaded tiles (sprite $180-$1B3, 16-tile stride) against every
32x32 frame of the driver's sheet, every frame: the frame index the game
draws, and the kart's OAM entries beside it."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
P1 = 0x1000
LAST = 0x84
import time as _t
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
class Shim:
    pass
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
_b = Bus(bytes(_r.data)); _c = CPU(_b)
_c.PB, _c.PC = 0x80, _r.vectors()["emu.RESET"]; _c.P = M_ | X_; _c.S = 0x1FFF
_c.run_to(0x80805C, budget=8_000_000)
_orig = _b.read
def _rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33, 0x0E50, 0x0E51): return 0
        if addr == 0x0150: return 0
        if addr == 0x0152: return 0
    return _orig(bank, addr)
_b.read = _rd
_b.reg_reads[0x4218] = 0; _b.reg_reads[0x4219] = 0
_t0 = _t.time()
while _t.time() - _t0 < 900:
    _c.run_frames_scanline(10)
    if _b.wram[0x36] // 2 in (1, 6) and sum(1 for k in range(128) if _b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break
_c.run_frames_scanline(400)
L = Shim(); L.b = _b; L.c = _c; L.r = _r
L.w = lambda a: _b.wram[a] | _b.wram[a+1] << 8
L.sw = lambda a, v: (_b.wram.__setitem__(a, v & 0xFF), _b.wram.__setitem__(a+1, (v >> 8) & 0xFF))
L.s16 = lambda v: v - 65536 if v > 32767 else v
L.speed = lambda: L.s16(L.w(P1 + 0xEA))
L.pos = lambda: (L.w(P1+0x18), L.w(P1+0x1C))
L.heading = lambda: L.w(P1 + 0xA4)
def _frame(hi=0, lo=0):
    _b.reg_reads[0x4219] = hi; _b.reg_reads[0x4218] = lo; _c.run_frames_scanline(1)
L.frame = _frame
def _flow(n, lo=0):
    for _ in range(n):
        x, y = L.pos(); cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
        want = _b.wram[0x14000 + cell] << 8; d = (want - L.heading()) & 0xFFFF
        if d > 32768: d -= 65536
        pad = 0x80
        if d < -0x300: pad |= 0x02
        elif d > 0x300: pad |= 0x01
        _frame(pad, lo)
L.flow = _flow
r = L.r; rom = bytes(r.data)
log("1P race: track $%02X mode $%02X" % (_b.wram[0x0124], _b.wram[0x36]))
def pc(snes): return (snes & 0x3FFFFF) if (snes >> 16) >= 0xC0 else ((snes >> 16) - 0x80) * 0x10000 + (snes & 0xFFFF)
SHEET = pc(0xC02000)
def sheet_frame(f):
    base = (f // 4) * 64 + (f % 4) * 4
    out = b''
    for rr in range(4):
        for c in range(4):
            t = base + rr * 16 + c
            out += rom[SHEET + t * 32: SHEET + (t + 1) * 32]
    return out
FRAMES = {f: sheet_frame(f) for f in range(48)}
def kart_tiles():
    out = b''
    for rr in range(4):
        for c in range(4):
            t = 0x180 + rr * 16 + c
            out += bytes(L.b.vram[(0x400 + t) * 32:(0x401 + t) * 32])
    return out
def which_frame():
    kt = kart_tiles()
    for f, b in FRAMES.items():
        if b == kt: return f
    # nearest by tile count
    best = max(FRAMES, key=lambda f: sum(1 for i in range(16) if FRAMES[f][i*32:(i+1)*32] == kt[i*32:(i+1)*32]))
    n = sum(1 for i in range(16) if FRAMES[best][i*32:(i+1)*32] == kt[i*32:(i+1)*32])
    return "~%d(%d/16)" % (best, n)
def kart_oam():
    o = L.b.oam; out = []
    for k in range(128):
        y = o[k*4+1]
        if y in (0, 0xF0, 0xE0): continue
        x = o[k*4]; hi = o[512 + (k >> 2)]; x |= ((hi >> ((k & 3) * 2)) & 1) << 8
        if x >= 256: continue
        t = o[k*4+2] | (o[k*4+3] & 1) << 8
        big = (hi >> ((k & 3) * 2 + 1)) & 1
        out.append((k, x, y, "%03X" % t, "%02X" % o[k*4+3], "L" if big else "s"))
    return out
# a REAL finish: the AI karts are held still every frame, P1 drives the
# five laps by the flow field and wins; nothing about the lap word is forced
def freeze_ai():
    for q in range(1, 8):
        base = 0x1000 + q * 0x100
        L.sw(base + 0xEA, 0); L.sw(base + 0x22, 0); L.sw(base + 0x24, 0)
t0 = time.time(); f = 0; fin = None
log("driving the whole race; lap byte $%02X" % L.b.wram[P1 + 0xC1])
while time.time() - t0 < 3300:
    freeze_ai()
    L.flow(1) if fin is None else L.frame(0); f += 1
    if fin is None and f % 600 == 0: log("f%d lap $%02X at %s spd %d" % (f, L.b.wram[P1 + 0xC1], L.pos(), L.speed()))
    if fin is None and L.b.wram[P1 + 0xC1] >= 0x85:
        fin = f; log("FINISHED at f%d rank $%04X $A6=%02X $AC=%02X" % (f, L.w(P1 + 0xE6), L.b.wram[P1+0xA6], L.b.wram[P1+0xAC]))
    if fin is not None:
        g = f - fin
        if g in (10, 60, 120, 200, 280):
            for nm, buf in (("vram", L.b.vram), ("cgram", L.b.cgram), ("oam", L.b.oam)):
                open("tmp/winreal_%s_%d.bin" % (nm, g), "wb").write(bytes(buf))
        if g % 10 == 0 or g < 6:
            log("+%3d frame %-10s $A6=%02X $AC=%02X $94=%04X $A4=%04X pose=%04X spd=%d z=%d oam=%s" % (
                g, which_frame(), L.b.wram[P1+0xA6], L.b.wram[P1+0xAC], L.w(0x94), L.w(P1+0xA4), L.w(P1+0x2A), L.speed(), L.s16(L.w(P1+0x1E)), kart_oam()))
        if g >= 360: break
log("done")
