"""Kart against kart: is there a contact response, and who writes it?

NOTES 112 looked for one in the attract demo and found nothing - two AI
karts passed within 3 px with no reaction.  That is one recording of a
race where nobody actually leant on anybody, so it settles nothing.

This drives it instead.  Reach a running race, then put P1 right on top
of an AI kart and step, recording every write to either kart's position,
velocity, speed or state WITH the PC that made it.  A response, if there
is one, cannot hide from that.

    python3 tools/labs/bump.py [victim]      victim = kart block 1..7
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
victim = int(sys.argv[1]) if len(sys.argv) > 1 else 2

r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr in (0x0150, 0x0152): return 0
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
def w(a): return b.wram[a] | b.wram[a + 1] << 8
def sw(a, v):
    b.wram[a] = v & 0xFF; b.wram[a + 1] = (v >> 8) & 0xFF
def s16(v): return v - 65536 if v > 32767 else v

t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and (b.wram[0x1018] or b.wram[0x1019]):
        if sum(1 for k in range(128)
               if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break

# let the countdown run out, throttle off so there is no over-rev
for _ in range(600):
    b.reg_reads[0x4219] = 0; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    if s16(w(0x0146)) == 0 and b.wram[0x3A] == 6: break
print("released; $3A = %d" % b.wram[0x3A], flush=True)

# get everybody moving
for _ in range(90):
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)

P1, V = 0x1000, 0x1000 + victim * 0x100
print("before: P1 (%d,%d) sp %d | victim (%d,%d) sp %d"
      % (w(P1+0x18), w(P1+0x1C), s16(w(P1+0xEA)),
         w(V+0x18), w(V+0x1C), s16(w(V+0xEA))), flush=True)

WATCH = {}
for base, tag in ((P1, "P1"), (V, "vk")):
    for off in (0x18, 0x1A, 0x1C, 0x1E, 0x22, 0x24, 0xEA, 0xA6, 0xAC, 0xE2, 0xB2, 0x2A):
        WATCH[base + off] = "%s+$%02X" % (tag, off)
        WATCH[base + off + 1] = None          # high byte, same store
hits = []
watching = [False]
orig_w = b.write
def wr(bank, addr, val):
    if watching[0] and addr in WATCH and ((bank & 0x7F) <= 0x3F or bank == 0x7E):
        if WATCH[addr]:
            hits.append((c.PB, c.PC, WATCH[addr]))
    orig_w(bank, addr, val)
b.write = wr

# put P1 exactly on the victim, matching its heading, and watch
sw(P1 + 0x18, w(V + 0x18))
sw(P1 + 0x1C, w(V + 0x1C))
watching[0] = True
print("\nframe | P1 x    y   spd  vx   vy  $A6 $AC $E2  | victim x    y   spd  vx   vy  $E2", flush=True)
for f in range(24):
    print("%5d | %5d %5d %4d %4d %4d  %02X  %02X %04X | %5d %5d %4d %4d %4d %04X"
          % (f, w(P1+0x18), w(P1+0x1C), s16(w(P1+0xEA)), s16(w(P1+0x22)), s16(w(P1+0x24)),
             w(P1+0xA6) & 0xFF, w(P1+0xAC) & 0xFF, w(P1+0xE2),
             w(V+0x18), w(V+0x1C), s16(w(V+0xEA)), s16(w(V+0x22)), s16(w(V+0x24)),
             w(V+0xE2)), flush=True)
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)

seen = {}
for pb, pc, tag in hits:
    seen.setdefault((tag, pb, pc), 0)
    seen[(tag, pb, pc)] += 1
print("\nwriters while overlapping:", flush=True)
for (tag, pb, pc), n in sorted(seen.items()):
    print("  %-8s $%02X:%04X  x%d" % (tag, pb, pc, n), flush=True)
