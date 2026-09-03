#!/usr/bin/env python3
"""Every compressed graphics stream the game loads, found statically.

    tools/labs/decompsites.py [--render DIR]

The two decompressors are $84:E09E (writes WRAM bank $7F) and $84:DF38
(bank $7E), and every caller sets the same three registers immediately
before the JSL:

    LDY #$9C19      source ADDRESS
    LDA #$0084      source BANK
    LDX #$6C00      destination in $7F
    JSL $84E09E

The convention was verified against two streams the port already decodes
($87:FDBA is SURF_BLOB, $C1:12F8 is SMK_ICON_SRC), and the site above is
the mole's (NOTES 272).  The three loads come in any order, so this
scans for the JSL and reads backwards.

Why this and not a search of the ROM for the tiles: tools/labs/findart.py
looked for live tiles in every decompressible stream and missed the mole
twice over - it scanned only banks $C0-$CF (the mole is in $84) and
stepped two bytes at a time from even offsets (the mole starts at odd
$9C19).  A stream that no caller ever decompresses is not art; a stream
some caller decompresses is exactly the candidate set.
"""
import sys, os, argparse

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from smktool.rom import Rom
from smktool.compress import decompress

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

DECOMP = {0x84E09E: 0x7F, 0x84DF38: 0x7E}
BACK = 24                      # how far back to look for the three loads


def sites(data, rom):
    """[(call_snes, src_snes, dst, wram_bank)] for every static caller"""
    out = []
    for pc in range(len(data) - 4):
        if data[pc] != 0x22:
            continue
        tgt = data[pc + 1] | (data[pc + 2] << 8) | (data[pc + 3] << 16)
        if tgt not in DECOMP:
            continue
        y = a = x = None
        p = pc - 3
        # walk back over three-byte immediate loads only; anything else stops
        while p >= 0 and p >= pc - BACK:
            op, imm = data[p], data[p + 1] | (data[p + 2] << 8)
            if   op == 0xA0 and y is None: y = imm
            elif op == 0xA9 and a is None: a = imm
            elif op == 0xA2 and x is None: x = imm
            else: break
            p -= 3
        if y is None or a is None or x is None:
            continue
        try:
            call = rom.pc_to_snes(pc)
        except Exception:
            call = pc
        out.append((call, ((a & 0xFF) << 16) | y, x, DECOMP[tgt]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=os.path.join(ROOT, "rom", "smk_usa.sfc"))
    ap.add_argument("--render", help="write each stream as a 16-wide tile sheet here")
    args = ap.parse_args()

    rom = Rom.load(args.rom)
    data = bytes(rom.data)
    found = sites(data, rom)
    seen = {}
    for call, src, dst, wb in found:
        seen.setdefault((src, dst, wb), []).append(call)
    print("%d static decompression call sites, %d distinct (source -> destination)"
          % (len(found), len(seen)))
    for (src, dst, wb), calls in sorted(seen.items()):
        try:
            out, _ = decompress(data, rom.snes_to_pc(src), max_out=0x20000)
            note = "%5d bytes (%3d tiles)" % (len(out), len(out) // 32)
        except Exception as e:
            note = "UNDECODABLE (%s)" % e
        print("  $%02X:%04X -> $%02X:%04X  %-24s  called from %s"
              % (src >> 16, src & 0xFFFF, wb, dst, note,
                 " ".join("$%06X" % c for c in calls)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
