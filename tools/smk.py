#!/usr/bin/env python3
"""smk - Super Mario Kart reverse-engineering / rebuild toolkit."""
from __future__ import annotations
import argparse, json, os, sys, subprocess, hashlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smktool.rom import Rom, MappingError
from smktool.symbols import Symbols
from smktool.disasm import Tracer
from smktool.listing import Formatter
from smktool.tables import discover, health, emit_sym

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEF_ROM = os.path.join(ROOT, "rom", "smk_usa.sfc")
DEF_SYMS = os.path.join(ROOT, "symbols")


def load(args) -> tuple[Rom, Symbols]:
    rom = Rom.load(args.rom)
    syms = Symbols.load(args.syms) if os.path.exists(args.syms) else Symbols()
    return rom, syms


def parse_addr(t: str) -> int:
    t = t.replace(":", "").lstrip("$")
    return int(t, 16)


# ---------------------------------------------------------------- commands
def cmd_info(args):
    rom, _ = load(args)
    info = rom.identify()
    h = rom.header
    print(json.dumps(info, indent=2))
    print("\nheader @ $%05X" % h.base)
    print("  mapmode   $%02X  (%s%s)" % (h.mapmode, h.mapper,
                                         ", fastrom" if h.fastrom else ""))
    print("  cart type $%02X" % h.cart_type)
    print("  rom size  $%02X (%d KB)   sram $%02X (%d KB)"
          % (h.rom_size_log, 1 << h.rom_size_log, h.sram_size_log,
             (1 << h.sram_size_log) if h.sram_size_log else 0))
    print("  version   $%02X   country $%02X   dev $%02X" % (h.version, h.country, h.dev_id))
    print("  checksum  $%04X / $%04X   valid=%s" % (h.checksum, h.complement, rom.checksum_ok()))
    print("\nvectors:")
    for k, v in rom.vectors().items():
        try:
            pc = "file $%05X" % rom.snes_to_pc(0x800000 | v) if v >= 0x8000 else "-"
        except MappingError:
            pc = "-"
        print("  %-12s $%04X   %s" % (k, v, pc))


def cmd_trace(args):
    rom, syms = load(args)
    extra = [(parse_addr(a), 1, 1) for a in (args.entry or [])]
    res = Tracer(rom, syms).trace(extra or None)
    cov = res.coverage(rom)
    print("instructions   : %d" % len(res.insns))
    print("subroutines    : %d" % len(res.funcs))
    print("bytes covered  : %d / %d (%.2f%%)" % (len(cov), len(rom.data),
                                                 100 * len(cov) / len(rom.data)))
    print("flag conflicts : %d" % len(res.conflicts))
    print("indirect jumps : %d" % len(res.indirect))
    if res.indirect:
        for a in sorted(res.indirect):
            print("   $%06X  %s" % (a, res.insns[a].mnem))
    if res.conflicts and args.verbose:
        print("conflict sites : " + ", ".join("$%06X" % a for a in sorted(res.conflicts)))
    if args.coverage_map:
        _coverage_map(rom, cov)


def _coverage_map(rom: Rom, cov: set[int]):
    print("\ncoverage by 32KB block:")
    blk = 0x8000
    for i in range(0, len(rom.data), blk):
        c = sum(1 for p in range(i, min(i + blk, len(rom.data))) if p in cov)
        bar = "#" * int(40 * c / blk)
        print("  $%05X-$%05X %5.1f%% %s" % (i, i + blk - 1, 100 * c / blk, bar))


def cmd_dis(args):
    rom, syms = load(args)
    extra = [(parse_addr(a), args.m, args.x) for a in (args.entry or [])]
    res = Tracer(rom, syms).trace(extra or None)
    f = Formatter(rom, res, syms, show_bytes=not args.no_bytes)
    start = parse_addr(args.start) if args.start else None
    end = parse_addr(args.end) if args.end else None
    text = f.render(start, end)
    if args.out:
        open(args.out, "w").write(text)
        print("wrote %s (%d lines)" % (args.out, text.count("\n")))
    else:
        print(text, end="")


def cmd_jumptables(args):
    """Discover indirect-dispatch tables and converge to a fixpoint.

    Each round writes the proposals, then re-traces: newly reachable code
    exposes further dispatch sites, so this repeats until nothing new turns up.
    """
    out = args.out or os.path.join(DEF_SYMS, "10_jumptables.sym")
    prev = None
    for rd in range(1, args.rounds + 1):
        rom, syms = load(args)
        tr = Tracer(rom, syms)
        res = tr.trace()
        cands = discover(rom, res, tracer=tr)
        if args.dry_run:
            print(emit_sym(cands))
            return
        os.makedirs(os.path.dirname(out), exist_ok=True)
        open(out, "w").write(emit_sym(cands))
        rom2, syms2 = load(args)
        h = health(rom2, Tracer(rom2, syms2).trace())
        print("round %d: tables=%-4d insns=%-6d cov=%5.2f%% junk=%-4d "
              "overlaps=%-4d conflicts=%d"
              % (rd, len(cands), h["instructions"],
                 100 * h["coverage_bytes"] / len(rom2.data),
                 h["junk_opcodes"], h["overlaps"], h["conflicts"]))
        if prev == (len(cands), h["instructions"]):
            break
        prev = (len(cands), h["instructions"])
    print("wrote", out)


def cmd_health(args):
    rom, syms = load(args)
    h = health(rom, Tracer(rom, syms).trace())
    h["coverage_pct"] = round(100 * h["coverage_bytes"] / len(rom.data), 3)
    print(json.dumps(h, indent=2))


def cmd_hex(args):
    rom, syms = load(args)
    a = parse_addr(args.addr)
    pc = rom.snes_to_pc(a) if a > 0xFFFF else a
    n = int(args.len, 0)
    for off in range(pc, min(pc + n, len(rom.data)), 16):
        row = rom.data[off:off + 16]
        print(" %05X  %-47s  |%s|" % (off, " ".join("%02X" % b for b in row),
              "".join(chr(b) if 32 <= b < 127 else "." for b in row)))


def cmd_checksum(args):
    rom, _ = load(args)
    ok = rom.checksum_ok()
    old, new = rom.fix_checksum()
    print("checksum was $%04X, computed $%04X (%s)" % (old, new, "ok" if ok else "FIXED"))
    if args.write:
        rom.save(args.rom)
        print("wrote", args.rom)


def cmd_verify(args):
    rom, _ = load(args)
    info = rom.identify()
    print("sha1 %s  %s" % (info["sha1"], "KNOWN: " + info["name"] if info["known"]
                           else "UNKNOWN base ROM"))
    sys.exit(0 if info["known"] else 1)


def main():
    p = argparse.ArgumentParser(prog="smk", description=__doc__)
    p.add_argument("--rom", default=DEF_ROM)
    p.add_argument("--syms", default=DEF_SYMS)
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("info", help="ROM header, vectors, identification")
    s.set_defaults(fn=cmd_info)

    s = sub.add_parser("verify", help="check the base ROM hash")
    s.set_defaults(fn=cmd_verify)

    s = sub.add_parser("trace", help="run the tracing disassembler, report coverage")
    s.add_argument("-e", "--entry", action="append")
    s.add_argument("-v", "--verbose", action="store_true")
    s.add_argument("--coverage-map", action="store_true")
    s.set_defaults(fn=cmd_trace)

    s = sub.add_parser("dis", help="emit an asar-assemblable listing")
    s.add_argument("-e", "--entry", action="append")
    s.add_argument("-s", "--start"); s.add_argument("-E", "--end")
    s.add_argument("-o", "--out")
    s.add_argument("--no-bytes", action="store_true")
    s.add_argument("-m", type=int, default=1); s.add_argument("-x", type=int, default=1)
    s.set_defaults(fn=cmd_dis)

    s = sub.add_parser("jumptables", help="discover indirect dispatch tables")
    s.add_argument("-o", "--out")
    s.add_argument("-n", "--rounds", type=int, default=8)
    s.add_argument("--dry-run", action="store_true")
    s.set_defaults(fn=cmd_jumptables)

    s = sub.add_parser("health", help="trace quality metrics (desync detectors)")
    s.set_defaults(fn=cmd_health)

    s = sub.add_parser("hex", help="hex dump at a snes address or file offset")
    s.add_argument("addr"); s.add_argument("-n", "--len", default="0x100")
    s.set_defaults(fn=cmd_hex)

    s = sub.add_parser("checksum", help="verify / fix the ROM checksum")
    s.add_argument("-w", "--write", action="store_true")
    s.set_defaults(fn=cmd_checksum)

    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
