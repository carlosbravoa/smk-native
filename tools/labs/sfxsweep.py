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

def main():
    what = sys.argv[1] if len(sys.argv) > 1 else 'all'
    L = Lab(zero=(0x0E50, 0x0E51))
    L.reach_race()
    log('in a race; sweeping %s' % what)
    if what in ('items', 'all'):
        sweep_items(L)
    if what in ('hazards', 'all'):
        sweep_hazards(L)

main()
