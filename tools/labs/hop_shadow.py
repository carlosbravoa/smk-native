"""The shadow sprite, and the $1F -> screen-pixel law, from the kart's hop.

The user: the shadow is the same flickering black oval under every object,
and the kart has one too when it hops.  A flicker every other frame is the
SNES faking translucency, and it is also why single-frame OAM dumps kept
finding nothing - the sprite is only there on half of them.

So drive the kart, hop it (L), and log EVERY frame of the arc:

  * the sprite that appears only while airborne is the shadow, and OAM
    gives its tile, palette and size - ROM art, not an invented ellipse
  * the kart sprite's screen Y against the shadow's, over the whole arc,
    is the $1F -> screen conversion swept rather than fitted to one point
    (the port's SMK_MOVER_UNIT = 410 is inherited and wrong)
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cup, course, want = 0, 3, 17
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return cup
        if addr == 0x0152: return course
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
t0 = time.time()
while time.time() - t0 < 1800:
    c.run_frames_scanline(10)
    if b.wram[0x0124] == want and b.wram[0x36] // 2 in (1, 6):
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0,0xF0,0xE0)) >= 10: break
w = b.wram
def rw(a): return w[a] | w[a+1] << 8
def s16(v): return v - 65536 if v > 32767 else v
print("reached race: track %d" % w[0x0124], flush=True)
for _ in range(700):
    b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)
print("up to speed: %d" % s16(rw(0x1000+0xEA)), flush=True)

def snap():
    out = []
    for k in range(128):
        x, y, t_, at = b.oam[k*4:k*4+4]
        hi = b.oam[512 + (k >> 2)]
        xh = (hi >> ((k & 3) * 2)) & 1
        big = (hi >> ((k & 3) * 2 + 1)) & 1
        if y in (0, 0xF0, 0xE0): continue
        out.append((x - (256 if xh else 0), y, t_ | ((at & 1) << 8),
                    (at >> 1) & 7, (at >> 4) & 3, (at >> 6) & 1, 16 if big else 8))
    return out

base = {}
for f in range(8):                              # ground truth, no hop
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    for s in snap(): base[s[2]] = base.get(s[2], 0) + 1
print("baseline tiles on the ground: %s"
      % " ".join("$%03X" % t for t in sorted(base)), flush=True)

print("\n f   $1F  hop | sprites whose TILE never appears on the ground", flush=True)
for f in range(70):
    b.reg_reads[0x4219] = 0x80
    b.reg_reads[0x4218] = 0x20 if f == 0 else 0       # L = hop
    c.run_frames_scanline(1)
    z = s16(rw(0x1000+0x1F))
    new = [s for s in snap() if s[2] not in base]
    kart = [s for s in snap() if s[2] in (0x180, 0x1A0)]
    ky = min((s[1] for s in kart), default=-1)
    print("%3d %5d %4d | kart top y=%3d | %s"
          % (f, z, s16(rw(0x1000+0x20)), ky,
             " ".join("(x%d y%d t$%03X p%d pr%d %s %dx%d)"
                      % (x, y, t, p, pr, "H" if h else "-", sz, sz)
                      for x, y, t, p, pr, h, sz in new) or "-"), flush=True)
