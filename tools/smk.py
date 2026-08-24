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
from smktool import assets as A
from smktool.compress import decompress, compress
from smktool import gfx as G

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEF_ROM = os.path.join(ROOT, "rom", "smk_usa.sfc")
DEF_SYMS = os.path.join(ROOT, "romhack", "symbols")


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


def cmd_lin(args):
    """Linear disassembly from an address with explicit starting flags.

    Use when the tracer has not reached a routine yet - a loader called only
    through a dispatch path, say.  You supply M and X; getting them wrong
    desynchronises the output, which is itself a useful signal.
    """
    rom, syms = load(args)
    from smktool.opcodes import FORMAT, TERMINATORS
    tr = Tracer(rom, syms)
    a = parse_addr(args.addr)
    m, x = args.m, args.x
    for _ in range(args.count):
        try:
            pc = rom.snes_to_pc(a)
        except MappingError:
            print("  <unmapped>"); break
        i = tr.decode(a, pc, m, x)
        if i is None:
            break
        raw = " ".join("%02X" % b for b in rom.data[pc:pc + i.size])
        if i.mode in ("immM", "immX"):
            op = (" #$%02X" if i.size - 1 == 1 else " #$%04X") % i.operand
        elif i.mode in ("rel", "rell"):
            op = " $%06X" % i.target
        elif i.mode == "bm":
            op = " $%02X,$%02X" % (i.operand & 0xFF, i.operand >> 8)
        else:
            op = FORMAT.get(i.mode, "").format(i.operand)
        nm = syms.ram_name(i.operand) if i.mode.startswith(("ab", "dp")) else None
        print("  $%06X %-11s M=%d X=%d  %s%s%s"
              % (a, raw, m, x, i.mnem.lower(), op, ("   ; " + nm) if nm else ""))
        if i.mnem == "REP":
            if i.operand & 0x20: m = 0
            if i.operand & 0x10: x = 0
        elif i.mnem == "SEP":
            if i.operand & 0x20: m = 1
            if i.operand & 0x10: x = 1
        if i.mnem in TERMINATORS or i.mnem in ("RTL", "RTI"):
            if args.stop:
                break
        a = (a & 0xFF0000) | ((a + i.size) & 0xFFFF)


def cmd_hex(args):
    rom, syms = load(args)
    a = parse_addr(args.addr)
    pc = rom.snes_to_pc(a) if a > 0xFFFF else a
    n = int(args.len, 0)
    for off in range(pc, min(pc + n, len(rom.data)), 16):
        row = rom.data[off:off + 16]
        print(" %05X  %-47s  |%s|" % (off, " ".join("%02X" % b for b in row),
              "".join(chr(b) if 32 <= b < 127 else "." for b in row)))


def cmd_assets(args):
    rom, _ = load(args)
    if args.action == "list":
        print(json.dumps(A.manifest(rom), indent=2))
        return
    t = A.table(rom, args.table)
    if args.action == "export":
        data = t.read(args.index)
        out = args.out or f"assets/extracted/{args.table}_{args.index}.bin"
        os.makedirs(os.path.dirname(out), exist_ok=True)
        open(out, "wb").write(data)
        print(f"{args.table}[{args.index}] -> {out} ({len(data)} bytes)")
    elif args.action == "export-all":
        root = args.out or "assets/extracted"
        os.makedirs(root, exist_ok=True)
        n = 0
        for name in A.REGISTRY:
            tt = A.table(rom, name)
            for e in tt.entries():
                p = os.path.join(root, f"{name}_{e.index}.bin")
                open(p, "wb").write(tt.read(e.index))
                n += 1
        open(os.path.join(root, "manifest.json"), "w").write(
            json.dumps(A.manifest(rom), indent=2))
        print(f"exported {n} assets to {root}/")


def cmd_gfx(args):
    rom, _ = load(args)
    t = A.table(rom, args.table)
    data = t.read(args.index)
    if args.identify:
        for nm, sc, n in G.identify_format(data):
            print("  %-18s score %.4f  (%d tiles)" % (nm, sc, n))
        return
    pal = G.grey_palette(256)
    if args.palette is not None:
        pal = G.read_palette(A.table(rom, "palette").read(args.palette), 0, 256)
    fmt = args.format or G.identify_format(data)[0][0]
    buf = G.deinterleave(data, 1) if "odd" in fmt else (
        G.deinterleave(data, 0) if "even" in fmt else data)
    kw = dict(mode7=True) if fmt.startswith("mode7") else (
        dict(bpp=4) if "4bpp" in fmt else dict(bpp=2))
    tiles = G.decode_tiles(buf, **kw)
    w, h, rgb = G.tilesheet(tiles, pal, args.per_row)
    out = args.out or f"assets/extracted/{args.table}_{args.index}.png"
    os.makedirs(os.path.dirname(out), exist_ok=True)
    G.write_png(out, w, h, rgb, scale=args.scale)
    print(f"{args.table}[{args.index}] as {fmt}: {len(tiles)} tiles -> {out}")


def cmd_expand(args):
    rom, _ = load(args)
    size = {"512k": 0x80000, "1m": 0x100000, "2m": 0x200000}[args.size.lower()]
    before = len(rom.data)
    A.expand(rom, size)
    rom.fix_checksum()
    out = args.out or args.rom
    rom.save(out)
    print(f"{before} -> {len(rom.data)} bytes, wrote {out}")


def cmd_freespace(args):
    rom, _ = load(args)
    runs = A.free_runs(rom, args.min)
    total = sum(n for _, n, _ in runs)
    print(f"{len(runs)} runs >= {args.min} bytes, {total} bytes total "
          f"({100*total/len(rom.data):.2f}% of ROM)")
    for s_, n, f in sorted(runs, key=lambda r: -r[1])[:args.top]:
        print("   file $%05X  $%06X  %6d bytes  fill=$%02X"
              % (s_, rom.pc_to_snes(s_), n, f))


def cmd_sprites(args):
    """Export a kart sprite sheet.

    Palettes come from the same 256-colour blob the track uses, so a track
    index selects the palette set and --pal picks the sprite palette within
    it ($90 Mario, $A0 Luigi, $B0 Peach).
    """
    rom, _ = load(args)
    from smktool import mode7 as M7
    pal = G.read_palette(M7.palette(rom, max(args.theme, 0)), 0, 256)
    base = rom.snes_to_pc(parse_addr(args.base))
    sub = pal[args.pal:args.pal + 16]
    w, h, rgb = G.sprite_sheet(bytes(rom.data), base, args.frames, sub,
                               args.per_row)
    out = args.out or "assets/extracted/sprites_%06X.png" % parse_addr(args.base)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    G.write_png(out, w, h, rgb, scale=args.scale)
    print("%d frames from $%06X -> %s"
          % (args.frames, parse_addr(args.base), out))


def cmd_spc(args):
    """Boot the ROM in the interpreter and dump the sound driver it uploads.

    No SPC700 emulation: the upload protocol tells us every byte and where
    it goes, which is all an .spc file is.
    """
    from smktool.cpu import CPU, Bus, M_, X_
    from smktool.apu import write_spc
    rom, _ = load(args)
    bus = Bus(bytes(rom.data))
    cpu = CPU(bus)
    cpu.PB, cpu.PC = 0x80, rom.vectors()["emu.RESET"]
    cpu.P = M_ | X_
    cpu.S = 0x1FFF
    if not cpu.run_to(0x80805C, budget=8_000_000):
        sys.exit("the ROM did not reach its main loop")
    bus.reg_reads[0x4218] = 0
    bus.reg_reads[0x4219] = 0
    cpu.run_frames_scanline(args.frames)
    a = bus.apu
    nz = sum(1 for v in a.ram if v)
    out = args.out or "build/smk_driver.spc"
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    write_spc(a, out)
    print(f"{a.summary()}  entry ${a.entry:04X}")
    print(f"captured {nz}/65536 bytes of SPC RAM -> {out}")
    print("note: this is the state right after upload - the driver has not "
          "run, so the DSP registers are zero and it is waiting for a "
          "'play track' command on its ports.")


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

    s = sub.add_parser("lin", help="linear disassembly with given M/X")
    s.add_argument("addr")
    s.add_argument("-n", "--count", type=int, default=40)
    s.add_argument("-m", type=int, default=0); s.add_argument("-x", type=int, default=0)
    s.add_argument("--stop", action="store_true", help="stop at first terminator")
    s.set_defaults(fn=cmd_lin)

    s = sub.add_parser("hex", help="hex dump at a snes address or file offset")
    s.add_argument("addr"); s.add_argument("-n", "--len", default="0x100")
    s.set_defaults(fn=cmd_hex)

    s = sub.add_parser("assets", help="list / export compressed assets")
    s.add_argument("action", choices=["list", "export", "export-all"])
    s.add_argument("table", nargs="?", default="palette")
    s.add_argument("index", nargs="?", type=int, default=0)
    s.add_argument("-o", "--out")
    s.set_defaults(fn=cmd_assets)

    s = sub.add_parser("gfx", help="render an asset to PNG")
    s.add_argument("table"); s.add_argument("index", type=int)
    s.add_argument("-p", "--palette", type=int)
    s.add_argument("-f", "--format")
    s.add_argument("--identify", action="store_true")
    s.add_argument("--per-row", type=int, default=16)
    s.add_argument("--scale", type=int, default=3)
    s.add_argument("-o", "--out")
    s.set_defaults(fn=cmd_gfx)

    s = sub.add_parser("expand", help="grow the ROM image (more free space)")
    s.add_argument("--size", default="1m", choices=["512k", "1m", "2m"])
    s.add_argument("-o", "--out")
    s.set_defaults(fn=cmd_expand)

    s = sub.add_parser("freespace", help="report unused ROM space")
    s.add_argument("--min", type=int, default=32)
    s.add_argument("--top", type=int, default=20)
    s.set_defaults(fn=cmd_freespace)

    s = sub.add_parser("sprites", help="export a kart sprite sheet to PNG")
    s.add_argument("--base", default="$C02000")
    s.add_argument("-n", "--frames", type=int, default=32)
    s.add_argument("--pal", type=lambda v: int(v, 0), default=0x90)
    s.add_argument("--theme", type=int, default=1)
    s.add_argument("--per-row", type=int, default=8)
    s.add_argument("--scale", type=int, default=3)
    s.add_argument("-o", "--out")
    s.set_defaults(fn=cmd_sprites)

    s = sub.add_parser("spc", help="dump the sound driver the ROM uploads (.spc)")
    s.add_argument("-o", "--out")
    s.add_argument("-n", "--frames", type=int, default=200)
    s.set_defaults(fn=cmd_spc)

    s = sub.add_parser("checksum", help="verify / fix the ROM checksum")
    s.add_argument("-w", "--write", action="store_true")
    s.set_defaults(fn=cmd_checksum)

    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
