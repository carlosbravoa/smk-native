#!/usr/bin/env python3
"""End-to-end regression suite.

Everything here is checked against the ROM itself, not against expectations
baked into the code: if a claim in this project is wrong, one of these fails.
"""
from __future__ import annotations
import os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smktool.rom import Rom
from smktool.symbols import Symbols
from smktool.disasm import Tracer
from smktool.compress import decompress, compress, stream_size, scan
from smktool import assets as A
from smktool import gfx as G

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, "rom", "smk_usa.sfc")

PASS, FAIL = [], []


def check(name, cond, detail=""):
    (PASS if cond else FAIL).append(name)
    print(f"  {'ok  ' if cond else 'FAIL'}  {name}{('  - ' + detail) if detail else ''}")
    return cond


def main():
    if not os.path.exists(BASE):
        sys.exit(f"base ROM missing: {BASE}")
    rom = Rom.load(BASE)
    D = bytes(rom.data)

    print("ROM identity")
    info = rom.identify()
    check("recognised dump", info["known"], info["sha1"])
    check("checksum valid", rom.checksum_ok())
    check("HiROM + FastROM", rom.header.mapper == "hirom" and rom.header.fastrom)
    check("512 KB, no copier header", len(rom.data) == 0x80000)

    print("\naddress mapping")
    check("reset vector maps to boot code",
          rom.read(rom.snes_to_pc(0x800000 | rom.vectors()["emu.RESET"]), 1) == b"\x78",
          "first opcode is SEI")
    check("mirror canonicalisation",
          rom.snes_to_pc(0x088AED) == rom.snes_to_pc(0x808AED))
    check("pc<->snes round trip",
          all(rom.snes_to_pc(rom.pc_to_snes(p)) == p
              for p in range(0, len(rom.data), 4093)))

    print("\ndisassembler")
    syms = Symbols.load(os.path.join(ROOT, "romhack", "symbols"))
    res = Tracer(rom, syms).trace()
    check("traces a substantial body of code", len(res.insns) > 15000,
          f"{len(res.insns)} instructions")
    junk = sum(1 for i in res.insns.values()
               if i.mnem in ("BRK", "COP", "WDM", "STP"))
    check("junk-opcode rate under 0.5%", junk / len(res.insns) < 0.005,
          f"{junk}/{len(res.insns)}")
    check("dispatch tables resolve",
          all(any(jt.addr == a for jt in syms.jumptabs)
              for a in (0x808197, 0x8081BF, 0x808B12)))

    print("\ncompression codec")
    blobs = []
    for name in A.REGISTRY:
        t = A.table(rom, name)
        for e in t.entries():
            blobs.append((name, e))
    check("assets decode", len(blobs) > 50, f"{len(blobs)} blobs")

    bad_rt = 0
    smaller = 0
    for name, e in blobs:
        data = A.table(rom, name).read(e.index)
        enc = bytes(compress(data))
        if bytes(decompress(enc, 0)[0]) != data:
            bad_rt += 1
        if len(enc) <= e.comp_len:
            smaller += 1
    check("compress -> decompress is lossless for every asset", bad_rt == 0,
          f"{bad_rt} failures")
    check("every re-encoded asset fits its original slot",
          smaller == len(blobs), f"{smaller}/{len(blobs)}")

    adj = 0
    starts = {rom.snes_to_pc(e.src): e for _, e in blobs}
    for off, e in starts.items():
        if off + e.comp_len in starts:
            adj += 1
    check("consumed length predicts the next blob's address", adj > 20,
          f"{adj} adjacencies")

    print("\noracle (the game's own code)")
    from smktool.oracle import Oracle
    o = Oracle(rom)
    agree = 0
    for name, e in blobs:
        want = A.table(rom, name).read(e.index)
        res = o.call(0x84E09E, a=(e.src >> 16) & 0xFF, y=e.src & 0xFFFF, x=0)
        if res.wram(0x7F0000, len(want)) == want:
            agree += 1
    check("the game's own decompressor agrees with ours on every asset",
          agree == len(blobs), f"{agree}/{len(blobs)}")

    print("\npalettes")
    pt = A.table(rom, "palette")
    ok = True
    for e in pt.entries():
        p = pt.read(e.index)
        if len(p) != 512 or any(p[i] & 0x80 for i in range(1, 512, 2)):
            ok = False
    check("all palettes are 512 bytes of valid BGR555", ok)

    print("\nbuild pipeline")
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "smk.sfc")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "build.py"),
                            "--out", out, "--assets", os.path.join(tmp, "none"), "-q"],
                           capture_output=True, text=True)
        check("clean build succeeds", r.returncode == 0, r.stderr.strip()[:120])
        if r.returncode == 0:
            check("clean build reproduces the base ROM byte-for-byte",
                  Rom.load(out).sha1 == info["sha1"])

        # asset edit -> rebuild -> read back
        imp = os.path.join(tmp, "import")
        os.makedirs(imp)
        pal0 = bytearray(pt.read(0))
        pal0[4], pal0[5] = 0x1F, 0x7C          # a colour that is not the original
        open(os.path.join(imp, "palette_0.bin"), "wb").write(bytes(pal0))
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "build.py"),
                            "--out", out, "--assets", imp, "-q"],
                           capture_output=True, text=True)
        check("build with an edited asset succeeds", r.returncode == 0,
              r.stderr.strip()[:120])
        if r.returncode == 0:
            rom2 = Rom.load(out)
            got = A.table(rom2, "palette").read(0)
            check("edited colour survives the round trip",
                  got[4] == 0x1F and got[5] == 0x7C)
            check("the rest of the palette is untouched",
                  got[6:] == pt.read(0)[6:])
            check("rebuilt ROM checksum is correct", rom2.checksum_ok())

    print("\nROM expansion")
    big = Rom.load(BASE)
    A.expand(big, 0x100000)
    big.fix_checksum()
    check("expands to 1 MB", len(big.data) == 0x100000)
    check("size byte updated", big.header.rom_size_log == 0x0A)
    # the header itself legitimately changes (size byte + checksum)
    hdr = big.header.base
    check("original 512 KB preserved outside the header",
          bytes(big.data[:hdr]) == D[:hdr]
          and bytes(big.data[hdr + 0x40:0x80000]) == D[hdr + 0x40:0x80000])
    check("expanded image still identifies as HiROM",
          big.header.mapper == "hirom")

    print(f"\n{len(PASS)} passed, {len(FAIL)} failed")
    if FAIL:
        print("failed: " + ", ".join(FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
