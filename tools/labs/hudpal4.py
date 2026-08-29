#!/usr/bin/env python3
"""Every write to CGRAM entries 64-95 from BOOT, with the PC: the BG3 block
(mode 0) that the HUD's item icons and frame use.  Built without Lab so
the hook is in place before the attract race is even set up."""
import sys, os, time
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT,"tools"))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
r = Rom.load(os.path.join(ROOT,"rom","smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_|X_; c.S = 0x1FFF
orig_read = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if (lo <= 0x3F or bank == 0x7E) and addr in (0x0E32, 0x0E33, 0x0E50, 0x0E51): return 0
    return orig_read(bank, addr)
b.read = rd
b.reg_reads[0x4218]=0; b.reg_reads[0x4219]=0
hits=[]; frame=[0]
orig=b._ppu_write
def hook(reg,val):
    if reg == 0x2122:
        a = b.cgadd & 0x1FF
        if 128 <= a < 192: hits.append((frame[0], a, val, (c.PB<<16)|c.PC))
    return orig(reg,val)
b._ppu_write = hook
t0=time.time()
while time.time()-t0 < 1500:
    c.run_frames_scanline(10); frame[0]+=10
    if b.wram[0x36]//2 in (1,6) and sum(1 for k in range(128) if b.oam[k*4+1] not in (0,0xF0,0xE0)) >= 10: break
print("race reached at ~frame %d, track %d; CGRAM 64..95 now: %s" % (frame[0], b.wram[0x0124],
      " ".join("%04X" % (b.cgram[i*2]|b.cgram[i*2+1]<<8) for i in range(64,96))))
print("writes to entries 64..95 since boot: %d" % len(hits))
import collections
bypc=collections.OrderedDict()
for f,a,v,pc in hits: bypc.setdefault(pc,[]).append((f,a,v))
for pc,ws in bypc.items():
    ents=sorted(set(a//2 for f,a,v in ws))
    print("   $%06X  %4d writes  frames %d..%d  entries %s" % (pc,len(ws),ws[0][0],ws[-1][0], ents if len(ents)<12 else "%d..%d"%(ents[0],ents[-1])))
# the last value written to entry 83 (pal 4 colour 3) and 91, with pc
for ent in (81,82,83,89,90,91,93):
    last=[(f,a,v,pc) for f,a,v,pc in hits if a//2==ent]
    if last:
        lo=[x for x in last if x[1]%2==0][-1]; hi=[x for x in last if x[1]%2==1][-1]
        print("   entry %d last = %04X  (lo from $%06X f%d, hi from $%06X f%d)" % (ent, lo[2]|hi[2]<<8, lo[3],lo[0],hi[3],hi[0]))
