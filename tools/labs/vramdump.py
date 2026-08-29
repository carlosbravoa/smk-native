#!/usr/bin/env python3
"""Save VRAM, CGRAM and OAM of the running race to tmp/, so tiles can be
rendered offline in any palette (tools/labs/vramrender.py)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(settle=90, zero=(0x0E50, 0x0E51))
open("tmp/vram.bin","wb").write(bytes(L.b.vram)); open("tmp/cgram.bin","wb").write(bytes(L.b.cgram)); open("tmp/oam.bin","wb").write(bytes(L.b.oam))
log("saved vram %d cgram %d oam %d; OBSEL=%02X" % (len(L.b.vram), len(L.b.cgram), len(L.b.oam), L.b.regs.get(0x2101, 0)))
