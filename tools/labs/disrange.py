#!/usr/bin/env python3
"""Linear disassembly of one address range.

(Named disrange, not dis: a lab file called dis.py shadows the standard
library module inspect imports, and smktool then fails to load.)

    tools/labs/dis.py 80ADA0 80AE90 [M X]

The tracer follows control flow; sometimes what is wanted is simply to
read a routine straight through with the M/X widths the caller is known
to be in.
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from smktool.rom import Rom
from smktool.disasm import Tracer

def main():
    a0 = int(sys.argv[1], 16); a1 = int(sys.argv[2], 16)
    m = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    x = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    rom = Rom.load(os.environ.get("SMK_ROM", "rom/smk_usa.sfc"))
    t = Tracer(rom)
    a = a0
    while a < a1:
        ins = t.decode(a, rom.snes_to_pc(a), m, x)
        if ins is None: break
        opnd = {
            'imp': '', 'immM': f"#${ins.operand:04X}" if not m else f"#${ins.operand:02X}",
            'immX': f"#${ins.operand:04X}" if not x else f"#${ins.operand:02X}",
            'imm8': f"#${ins.operand:02X}",
            'dp': f"${ins.operand:02X}", 'dpx': f"${ins.operand:02X},x",
            'dpy': f"${ins.operand:02X},y", 'dpi': f"(${ins.operand:02X})",
            'dpix': f"(${ins.operand:02X},x)", 'dpiy': f"(${ins.operand:02X}),y",
            'dpil': f"[${ins.operand:02X}]", 'dpily': f"[${ins.operand:02X}],y",
            'abs': f"${ins.operand:04X}", 'abx': f"${ins.operand:04X},x",
            'aby': f"${ins.operand:04X},y", 'absl': f"${ins.operand:06X}",
            'ablx': f"${ins.operand:06X},x",
            'rel': f"${ins.target:06X}" if ins.target else f"{ins.operand}",
            'rell': f"${ins.target:06X}" if ins.target else f"{ins.operand}",
        }.get(ins.mode, f"${ins.operand:X} ({ins.mode})")
        print(f"  ${a:06X}  {ins.mnem:4s} {opnd}")
        if getattr(ins, 'mnem', '') == 'SEP':
            if ins.operand & 0x20: m = 1
            if ins.operand & 0x10: x = 1
        elif getattr(ins, 'mnem', '') == 'REP':
            if ins.operand & 0x20: m = 0
            if ins.operand & 0x10: x = 0
        a += ins.size if hasattr(ins, 'size') else 1
main()
