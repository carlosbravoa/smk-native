#!/usr/bin/env python3
"""The overtake voices, forced: what a kart says when it passes you.

`$84:EF05` is the whole thing.  Per kart it compares the rank word
`$00E6,y` with the one it remembered in `$0040,y`, and:

    same        -> nothing
    improved    -> JSL $84D98D : id = table $84D99B[character]
    got worse   -> JSL $84D9AB : the kart now ONE RANK AHEAD is looked up
                   ($010E,x maps rank to kart base), and IT speaks -
                   id = table $84D9CA[that character], 0 = silent

So the sound of being overtaken belongs to the overtaker, not to you.
This does not wait for a race to produce one: it pokes the remembered
rank so the comparison goes the way we want, and reads the id off the
65816 as it arrives at the sound entry.

    tools/labs/rankfx.py
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
import sfxsweep

NAMES = ['Mario', 'Luigi', 'Bowser', 'Peach', 'DK Jr', 'Yoshi', 'Koopa', 'Toad']
P1 = 0x1000

L = Lab(settle=int(os.environ.get('SETTLE','120')), zero=(0x0E50, 0x0E51))
w, sw = L.w, L.sw
sink = []
sfxsweep.install(L, sink)

def fire(setup, label):
    """apply setup, run a few frames, report every sound requested.

    $0042,y must be cleared first: $84:EF1C sets it to $0A after every
    voice, and while it runs the comparison is not even made - which is
    exactly why the first pass through this list only heard every third
    driver."""
    del sink[:]
    sw(P1 + 0x42, 0)                     # the ROM's own cooldown
    setup()
    L.flow(6)
    ids = [(a & 0xFF, c) for a, pc, c in sink]
    log("%-34s -> %s" % (label,
        ', '.join("$%02X (from $%06X)" % (i, c) for i, c in ids) or "SILENT"))
    return ids

log("=== rank IMPROVED: the driver's own voice (table $84D99B) ===")
gained = {}
for c in range(8):
    def setup(c=c):
        L.b.wram[P1 + 0x12] = c * 2          # who P1 is (kept doubled)
        L.b.wram[P1 + 0x13] = 0
        sw(P1 + 0x40, (w(P1 + 0xE6) + 2) & 0xFFFF)   # remembered = one worse
    ids = fire(setup, "P1 = %s overtakes" % NAMES[c])
    gained[c] = ids[0][0] if ids else None

log("")
log("=== rank got WORSE: the OVERTAKER speaks (table $84D9CA) ===")
passed = {}
for c in range(8):
    def setup(c=c):
        # put P1 somewhere with a kart above it, and make that kart be `c`
        r = w(P1 + 0xE6)
        if r < 2:
            sw(P1 + 0xE6, 4); r = 4
        ahead = w(0x010E + r - 2)            # kart base of the rank above
        L.b.wram[(ahead & 0x1FFF) + 0x12] = c * 2
        L.b.wram[(ahead & 0x1FFF) + 0x13] = 0
        L.b.wram[(ahead & 0x1FFF) + 0x10] &= 0x7F    # clear the BMI skip bit
        sw(P1 + 0x40, (w(P1 + 0xE6) - 2) & 0xFFFF)   # remembered = one better
        sw(P1 + 0x42, 0)
    ids = fire(setup, "%s passes P1" % NAMES[c])
    passed[c] = ids[0][0] if ids else None

log("")
log("=== the ROM's tables, for comparison ===")
rom = open(os.environ.get('SMK_ROM', 'rom/smk_usa.sfc'), 'rb').read()
if len(rom) % 1024 == 512:
    rom = rom[512:]
def off(a): return ((a >> 16 & 0x3F) << 16) | (a & 0xFFFF)     # HiROM
for c in range(8):
    a = rom[off(0x84D99B) + c * 2]
    b = rom[off(0x84D9CA) + c * 2]
    log("  %-7s  overtakes: ROM $%02X forced %s   |  passes you: ROM %s forced %s"
        % (NAMES[c], a,
           ("$%02X" % gained[c]) if gained[c] is not None else "-",
           ("$%02X" % b) if b else "silent",
           ("$%02X" % passed[c]) if passed[c] is not None else "-"))
sfxsweep.flush_csv()
log("done")
