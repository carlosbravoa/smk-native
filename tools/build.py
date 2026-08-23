#!/usr/bin/env python3
"""Build a modified ROM: base ROM + asar patches -> build/smk.sfc

Never modifies the base ROM.  Reports exactly which bytes changed, so a patch
that silently clobbers something is visible immediately.
"""
from __future__ import annotations
import argparse, os, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smktool.rom import Rom

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASAR = os.path.join(ROOT, "vendor", "asar-build", "asar", "bin", "asar")


def find_asar() -> str:
    if os.path.exists(ASAR):
        return ASAR
    p = shutil.which("asar")
    if p:
        return p
    sys.exit("asar not found. Run `make tools` to build it (vendor/asar).")


def diff_report(base: Rom, out: Rom, limit: int = 40) -> list[tuple[int, int]]:
    """Contiguous changed byte ranges as (start_pc, length)."""
    a, b = base.data, out.data
    n = min(len(a), len(b))
    runs, start = [], None
    for i in range(n):
        if a[i] != b[i]:
            if start is None:
                start = i
        elif start is not None:
            runs.append((start, i - start)); start = None
    if start is not None:
        runs.append((start, n - start))
    return runs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default=os.path.join(ROOT, "rom", "smk_usa.sfc"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "smk.sfc"))
    ap.add_argument("--asm", default=os.path.join(ROOT, "src", "main.asm"))
    ap.add_argument("--symfile", default=os.path.join(ROOT, "build", "smk.sym"))
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args()

    if not os.path.exists(a.base):
        sys.exit(f"base ROM missing: {a.base}\n"
                 "Place your own legally-obtained Super Mario Kart (USA) ROM there.")
    base = Rom.load(a.base)
    info = base.identify()
    if not info["known"]:
        print(f"! base ROM sha1 {info['sha1']} is not a recognised build; "
              "patch offsets may not apply.", file=sys.stderr)

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    shutil.copyfile(a.base, a.out)

    asar = find_asar()
    cmd = [asar, "--no-title-check", "--symbols=wla",
           f"--symbols-path={a.symfile}", a.asm, a.out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode != 0:
        sys.exit("asar failed")

    out = Rom.load(a.out)
    runs = diff_report(base, out)
    total = sum(n for _, n in runs)
    old, new = out.fix_checksum()
    out.save(a.out)

    if not a.quiet:
        print(f"\nbase   {a.base}\n       sha1 {info['sha1']}  ({info['name']})")
        print(f"out    {a.out}\n       sha1 {Rom.load(a.out).sha1}")
        print(f"\n{len(runs)} changed region(s), {total} bytes:")
        for start, n in runs[:40]:
            try:
                snes = "$%06X" % out.pc_to_snes(start)
            except Exception:
                snes = "-"
            print(f"   file ${start:05X}  {snes}  {n:5d} bytes")
        if len(runs) > 40:
            print(f"   ... and {len(runs)-40} more")
        print(f"\nchecksum ${old:04X} -> ${new:04X}")


if __name__ == "__main__":
    main()
