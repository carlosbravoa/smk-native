#!/usr/bin/env python3
"""Save VRAM, CGRAM and OAM of the running race to tmp/, so tiles can be
rendered offline in any palette (tools/labs/vramrender.py)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(settle=90, zero=(0x0E50, 0x0E51))
ITEM = os.environ.get("ITEM")            # e.g. 1000 green, 0400 red, 0800 banana: fire it, wait, then dump
TAG = ""
if ITEM:
    L.pace(600)
    L.sw(0x1000 + 0xE0, L.w(0x1000 + 0xE0) | int(ITEM, 16))
    for _ in range(int(os.environ.get("WAIT", "20"))): L.frame(0x80)
    TAG = "_" + ITEM
open("tmp/vram%s.bin" % TAG,"wb").write(bytes(L.b.vram)); open("tmp/cgram%s.bin" % TAG,"wb").write(bytes(L.b.cgram)); open("tmp/oam%s.bin" % TAG,"wb").write(bytes(L.b.oam))
log("saved vram %d cgram %d oam %d; OBSEL=%02X" % (len(L.b.vram), len(L.b.cgram), len(L.b.oam), L.b.regs.get(0x2101, 0)))
