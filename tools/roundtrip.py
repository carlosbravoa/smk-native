#!/usr/bin/env python3
"""Round-trip proof: disassemble -> reassemble -> compare bytes.

Every instruction the tracer decoded is re-emitted as asar source and
assembled back onto a copy of the base ROM.  If a single byte differs, the
disassembler is lying about that instruction and we want to know.
"""
from __future__ import annotations
import argparse, os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smktool.rom import Rom
from smktool.symbols import Symbols
from smktool.disasm import Tracer
from smktool.listing import Formatter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASAR = os.path.join(ROOT, "vendor", "asar-build", "asar", "bin", "asar")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default=os.path.join(ROOT, "rom", "smk_usa.sfc"))
    ap.add_argument("--syms", default=os.path.join(ROOT, "romhack", "symbols"))
    ap.add_argument("--keep", action="store_true", help="keep the temp asm/rom")
    a = ap.parse_args()

    rom = Rom.load(a.base)
    syms = Symbols.load(a.syms) if os.path.exists(a.syms) else Symbols()
    res = Tracer(rom, syms).trace()
    fmt = Formatter(rom, res, syms, show_bytes=False, raw=True)

    addrs = sorted(res.insns)
    # every relative-branch target needs a real label
    targets = {res.insns[ad].target for ad in addrs
               if res.insns[ad].mode in ("rel", "rell")}
    fmt.branch_labels = {t: "L_%06X" % t for t in targets}

    lines = ["hirom"]
    prev_end = None
    for ad in addrs:
        i = res.insns[ad]
        if ad != prev_end:
            lines.append("org $%06X" % ad)
        if ad in targets:
            lines.append("%s:" % fmt.branch_labels[ad])
        lines.append(fmt.line(i))
        prev_end = (ad & 0xFF0000) | ((ad + i.size) & 0xFFFF)

    # targets that landed mid-instruction or outside the trace still need a
    # definition; an org + bare label emits no bytes.
    orphans = sorted(targets - set(addrs))
    for t in orphans:
        lines.append("org $%06X" % t)
        lines.append("%s:" % fmt.branch_labels[t])
    src = "\n".join(lines) + "\n"
    if orphans:
        print("note: %d branch target(s) are not instruction starts "
              "(mid-instruction or untraced)" % len(orphans))

    tmp = tempfile.mkdtemp(prefix="smk-rt-")
    asm = os.path.join(tmp, "rt.asm")
    out = os.path.join(tmp, "rt.sfc")
    open(asm, "w").write(src)
    shutil.copyfile(a.base, out)

    r = subprocess.run([ASAR, "--no-title-check", asm, out],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout); print(r.stderr, file=sys.stderr)
        print(f"\nasm kept at {asm}")
        sys.exit("asar failed to reassemble the listing")

    new = Rom.load(out)
    bad: list[tuple[int, int]] = []
    for ad in addrs:
        i = res.insns[ad]
        pc = rom.snes_to_pc(ad)
        if rom.data[pc:pc + i.size] != new.data[pc:pc + i.size]:
            bad.append((ad, i.size))

    n_ins = len(addrs)
    n_by = sum(res.insns[x].size for x in addrs)
    print(f"instructions re-assembled : {n_ins}")
    print(f"bytes covered             : {n_by}")
    print(f"mismatches                : {len(bad)}")
    for ad, sz in bad[:25]:
        i = res.insns[ad]
        pc = rom.snes_to_pc(ad)
        print("  $%06X  %-5s %-5s  orig %s  ->  got %s   [%s]"
              % (ad, i.mnem, i.mode,
                 " ".join("%02X" % b for b in rom.data[pc:pc + sz]),
                 " ".join("%02X" % b for b in new.data[pc:pc + sz]),
                 fmt.line(i).strip()))
    if len(bad) > 25:
        print(f"  ... and {len(bad)-25} more")
    if a.keep or bad:
        print(f"\nartifacts: {tmp}")
    else:
        shutil.rmtree(tmp)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
