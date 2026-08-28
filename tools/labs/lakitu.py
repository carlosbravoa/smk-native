"""Lakitu and his light: the whole countdown, sprite by sprite.

NOTES 145a's next step - boot the Python oracle to a race and read the
OBJ half of VRAM while the lights run, because MAME exposes neither OAM
nor VRAM to Lua.

Runs to the frame $0146 is loaded with -336 (NOTES 145) and then records,
every frame until the release: the full 544-byte OAM, $0142 and $0146.
VRAM and CGRAM are dumped three times across the countdown, so a tile
that is swapped mid-sequence shows up as a difference rather than being
missed.

NEGATIVE RESULT, so it is not chased twice: his position generator is
not in low WRAM.  Sweeping $0000-$0FFF for a byte or a word whose high
byte tracks the sprite's y on every frame turns up exactly one address,
$022C - and that is the OAM shadow buffer the DMA copies out, i.e. the
output.  Hence the measured trajectory in src/lakitu.c.

    python3 tools/labs/lakitu.py [cup course]      -> tmp/lakitu_*.bin
"""
import sys, os, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TMP = os.path.join(ROOT, "tmp")
P1 = 0x1000
cup = int(sys.argv[1]) if len(sys.argv) > 2 else 0
course = int(sys.argv[2]) if len(sys.argv) > 2 else 0

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
b.reg_reads[0x4218] = 0
b.reg_reads[0x4219] = 0

def w(a): return b.wram[a] | b.wram[a + 1] << 8
def s16(v): return v - 65536 if v > 32767 else v

t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and (b.wram[0x1018] or b.wram[0x1019]):
        if sum(1 for k in range(128)
               if b.oam[k * 4 + 1] not in (0, 0xF0, 0xE0)) >= 10:
            break
print("track $%02X  mode $%02X" % (b.wram[0x0124], b.wram[0x36]), flush=True)

# step to the frame the countdown is armed
for _ in range(600):
    b.reg_reads[0x4219] = 0; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    if s16(w(0x0146)) < 0:
        break
else:
    raise SystemExit("the countdown never armed")
print("armed: $0146 = %d  $0142 = %d" % (s16(w(0x0146)), s16(w(0x0142))), flush=True)

oam = open(os.path.join(TMP, "lakitu_oam.bin"), "wb")
meta = open(os.path.join(TMP, "lakitu_meta.txt"), "w")
dumps = 0
for f in range(400):
    t146, t142 = s16(w(0x0146)), s16(w(0x0142))
    oam.write(bytes(b.oam))
    meta.write("%d %d %d\n" % (f, t146, t142))
    if t146 in (-330, -180, -30) or (t146 == 0 and dumps < 4):
        tag = "%d" % t146
        open(os.path.join(TMP, "lakitu_vram_%s.bin" % tag), "wb").write(bytes(b.vram))
        open(os.path.join(TMP, "lakitu_cgram_%s.bin" % tag), "wb").write(bytes(b.cgram))
        dumps += 1
        print("dumped vram/cgram at $0146 = %s" % tag, flush=True)
    if t146 >= 0 and f > 340:
        print("released at f%d" % f, flush=True)
        break
    b.reg_reads[0x4219] = 0; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
oam.close(); meta.close()
print("frames captured: %d" % (f + 1), flush=True)
