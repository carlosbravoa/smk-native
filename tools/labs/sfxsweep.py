#!/usr/bin/env python3
"""Drive the game into each state and log which sound its own code plays.

The user, rightly: waiting for a recording to happen to contain a
lightning bolt is absurd when the machine is deterministic and we have
an interpreter for it.  So this does not wait - it BOOTS a race, hands
the player each item in turn, presses the button, and watches the 65816
arrive at the sound routine, reading the id out of A and the caller off
the stack.  No recording, no ear, no luck.

    tools/labs/sfxsweep.py [items|hazards|all]
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

ENTRIES = {0x81F57A, 0x81F5A7, 0x81F5C2, 0x81F504, 0x81F5F8, 0x81F5E2}
ITEMS = {0: 'mushroom', 1: 'feather', 2: 'star', 3: 'banana', 4: 'green shell',
         5: 'red shell', 6: 'Boo', 7: 'coin', 8: 'lightning'}

def install(L, sink):
    """watch every instruction for an arrival at the sound routine"""
    cpu = L.c
    orig = cpu.step
    def step():
        pc = (cpu.PB << 16) | cpu.PC
        if pc in ENTRIES:
            a = cpu.A & 0xFFFF
            # via $F5E2 the stack carries the inner JSR's two bytes plus
            # PHP/PHY/PHX before the outer JSL's return - seven down
            base = cpu.S + (7 if pc == 0x81F5E2 else 0)
            rd = L.b.wram
            lo, hi, bk = rd[(base + 1) & 0x1FFF], rd[(base + 2) & 0x1FFF], rd[(base + 3) & 0x1FFF]
            caller = (bk << 16) | (((hi << 8 | lo) - 3) & 0xFFFF)
            sink.append((a, pc, caller))
        orig()
    cpu.step = step

def sweep_items(L):
    sink = []
    install(L, sink)
    L.pace(500)
    for item, name in ITEMS.items():
        del sink[:]
        L.sw(0x0D70, 0xC000 | item)        # the item, READY
        L.frame(0x80, 0x80)                # B held, A pressed: use it
        for _ in range(150):
            L.frame(0x80, 0)
        got = {}
        for a, pc, caller in sink:
            got.setdefault(a & 0xFF, set()).add(caller)
        if got:
            for sid in sorted(got):
                sites = ' '.join('$%02X:%04X' % (c >> 16, c & 0xFFFF) for c in sorted(got[sid]))
                log('item %d (%-11s) -> $%02X  from %s' % (item, name, sid, sites))
        else:
            log('item %d (%-11s) -> nothing' % (item, name))

def sweep_hazards(L):
    sink = []
    install(L, sink)
    snap = L.surface_snapshot()
    for cls in (0x22, 0x24, 0x26, 0x2A, 0x2C, 0x52, 0x5A, 0x5C, 0x5E, 0x80):
        del sink[:]
        L.surface_restore(snap)
        L.pace(500)
        L.surface_fill(snap, cls)
        for _ in range(90):
            L.frame(0x80, 0)
        got = {}
        for a, pc, caller in sink:
            got.setdefault(a & 0xFF, set()).add(caller)
        line = ', '.join('$%02X from %s' % (sid, ' '.join('$%02X:%04X' % (c >> 16, c & 0xFFFF)
                                                          for c in sorted(got[sid])))
                         for sid in sorted(got))
        log('surface $%02X -> %s' % (cls, line or 'nothing'))
    L.surface_restore(snap)

def sweep_collisions(L):
    """A wall, another kart, and an object - each forced under the kart."""
    sink = []
    install(L, sink)
    def report(what):
        got = {}
        for a, pc, caller in sink:
            got.setdefault(a & 0xFF, set()).add(caller)
        line = ', '.join('$%02X from %s' % (sid, ' '.join('$%02X:%04X' % (c >> 16, c & 0xFFFF)
                                                          for c in sorted(got[sid])))
                         for sid in sorted(got))
        log('%-22s -> %s' % (what, line or 'nothing'))
        del sink[:]

    snap = L.surface_snapshot()
    L.pace(600); del sink[:]
    L.surface_fill(snap, 0x80)          # solid everywhere: a wall to run into
    for _ in range(40):
        L.frame(0x80, 0)
    L.surface_restore(snap)
    report('into a wall ($80)')

    L.pace(600); del sink[:]
    x, y = L.pos()                      # kart 2 dropped on top of the player
    for _ in range(40):
        L.sw(0x1118, x & 0xFFFF); L.sw(0x111C, y & 0xFFFF)
        L.frame(0x80, 0)
        x, y = L.pos()
    report('another kart on top')

    L.pace(600); del sink[:]
    for _ in range(40):                 # and an object block moved onto it
        x, y = L.pos()
        for base in (0x1800, 0x1840, 0x1880, 0x18C0):
            L.sw(base + 0x18, x & 0xFFFF); L.sw(base + 0x1C, y & 0xFFFF)
        L.frame(0x80, 0)
    report('an object on top')


def sweep_menus():
    """The menus, driven from the title screen by the pad - no race."""
    import lab as _lab
    from smktool.rom import Rom
    from smktool.cpu import CPU, Bus, M_, X_
    r = Rom.load(os.path.join(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))), 'rom', 'smk_usa.sfc'))
    b = Bus(bytes(r.data)); c = CPU(b)
    c.PB, c.PC = 0x80, r.vectors()['emu.RESET']; c.P = M_ | X_; c.S = 0x1FFF
    c.run_to(0x80805C, budget=8_000_000)
    b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0

    class Shim:
        pass
    L = Shim(); L.c = c; L.b = b
    sink = []
    install(L, sink)

    def frames(n, hi=0, lo=0):
        b.reg_reads[0x4219] = hi; b.reg_reads[0x4218] = lo
        c.run_frames_scanline(n)
        b.reg_reads[0x4219] = 0; b.reg_reads[0x4218] = 0

    def report(what):
        got = {}
        for a, pc, caller in sink:
            got.setdefault(a & 0xFF, set()).add(caller)
        line = ', '.join('$%02X from %s' % (sid, ' '.join('$%02X:%04X' % (cc >> 16, cc & 0xFFFF)
                                                          for cc in sorted(got[sid])))
                         for sid in sorted(got))
        log('%-22s -> %s' % (what, line or 'nothing'))
        del sink[:]

    frames(240)
    del sink[:]
    for label, hi, lo in (('START (enter)', 0x10, 0x00),
                          ('A (confirm)',  0x00, 0x80),
                          ('DOWN',         0x04, 0x00),
                          ('UP',           0x08, 0x00),
                          ('LEFT',         0x02, 0x00),
                          ('RIGHT',        0x01, 0x00),
                          ('B (back)',     0x80, 0x00),
                          ('A (confirm)',  0x00, 0x80),
                          ('DOWN',         0x04, 0x00),
                          ('A (confirm)',  0x00, 0x80)):
        frames(4, hi, lo)
        frames(40)
        report('menu: ' + label)


def course_lab(cup, course, zero=(0x0E50, 0x0E51)):
    """A Lab, but booted into a chosen cup/course - the object sounds are
    per theme (the plants, the moles, the Thwomps each have their own
    call site in bank $85), so each one has to be reached."""
    from smktool.rom import Rom
    from smktool.cpu import CPU, Bus, M_, X_
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    r = Rom.load(os.path.join(root, 'rom', 'smk_usa.sfc'))
    b = Bus(bytes(r.data)); c = CPU(b)
    c.PB, c.PC = 0x80, r.vectors()['emu.RESET']; c.P = M_ | X_; c.S = 0x1FFF
    c.run_to(0x80805C, budget=8_000_000)
    orig = b.read
    def rd(bank, addr):
        lo = bank & 0x7F
        if lo <= 0x3F or bank == 0x7E:
            if addr in (0x0E32, 0x0E33) or addr in zero: return 0
            if addr == 0x0150: return cup
            if addr == 0x0152: return course
        return orig(bank, addr)
    b.read = rd
    b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
    L = Lab.__new__(Lab)
    L.r, L.b, L.c = r, b, c
    L.reach_race()
    c.run_frames_scanline(200)
    return L


def sweep_objects(cup, course, label):
    """Drive the kart into each live entity of a course, one at a time."""
    L = course_lab(cup, course)
    sink = []
    install(L, sink)
    track = L.b.wram[0x0124]
    log('%s: cup %d course %d -> track $%02X theme $%02X'
        % (label, cup, course, track, L.b.wram[0x0126]))
    for base in (0x1800, 0x1840, 0x1880, 0x18C0):
        if L.w(base) == 0:
            continue
        L.pace(500)
        del sink[:]
        for _ in range(80):
            x, y = L.pos()
            h = L.heading()
            import math
            a = h * 2 * math.pi / 65536.0
            # a few pixels straight ahead, so the kart drives into it
            ex = int(x + math.sin(a) * 24) & 0xFFFF
            ey = int(y - math.cos(a) * 24) & 0xFFFF
            L.sw(base + 0x18, ex); L.sw(base + 0x1C, ey)
            L.frame(0x80, 0)
        got = {}
        for a2, pc, caller in sink:
            got.setdefault(a2 & 0xFF, set()).add(caller)
        line = ', '.join('$%02X from %s' % (sid, ' '.join('$%02X:%04X' % (cc >> 16, cc & 0xFFFF)
                                                          for cc in sorted(got[sid])))
                         for sid in sorted(got))
        log('  block $%04X (type $%04X) -> %s' % (base, L.w(base), line or 'nothing'))


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else 'all'
    if what == 'menus':
        sweep_menus()
        return
    if what == 'objects':
        # cup, course, what lives there
        for cup, course, label in ((1, 2, 'Donut Plains - moles'),
                                   (1, 0, 'Choco Island - plants'),
                                   (0, 3, 'Bowser Castle - Thwomps'),
                                   (3, 4, 'Rainbow Road - Thwomps'),
                                   (2, 1, 'Koopa Beach - cheep-cheeps')):
            try:
                sweep_objects(cup, course, label)
            except Exception as e:
                log('%s: FAILED %s' % (label, e))
        return
    L = Lab(zero=(0x0E50, 0x0E51))
    L.reach_race()
    log('in a race; sweeping %s' % what)
    if what in ('items', 'all'):
        sweep_items(L)
    if what in ('hazards', 'all'):
        sweep_hazards(L)
    if what in ('collisions', 'all'):
        sweep_collisions(L)

main()
