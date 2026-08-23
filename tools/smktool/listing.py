"""Format traced instructions as asar-assemblable source."""
from __future__ import annotations
from .opcodes import FORMAT, BRANCHES, CALLS
from .rom import Rom, MappingError
from .symbols import Symbols
from .disasm import TraceResult, Insn

# operand width suffix so asar reproduces the exact encoding
SUFFIX = {
    "dp": ".b", "dpx": ".b", "dpy": ".b", "idp": ".b", "idx": ".b", "idy": ".b",
    "idl": ".b", "idly": ".b", "sr": ".b", "sry": ".b",
    "abs": ".w", "abx": ".w", "aby": ".w", "ind": ".w", "iax": ".w", "ial": ".w",
    "abl": ".l", "alx": ".l",
}
NO_SUFFIX = {"imp", "acc", "rel", "rell", "bm", "imm8"}
# instructions where asar must not see a width suffix on a label operand
BRANCHY = BRANCHES | {"BRA", "BRL", "PER"}


class Formatter:
    def __init__(self, rom: Rom, res: TraceResult, syms: Symbols,
                 show_bytes: bool = True, raw: bool = False):
        self.rom, self.res, self.syms = rom, res, syms
        self.show_bytes = show_bytes
        self.raw = raw            # raw=True: no labels, no comments (round-trip)
        self.branch_labels: dict[int, str] = {}   # raw mode: rel/rell targets
        self.auto: dict[int, str] = {}
        if not raw:
            self._make_labels()

    def _make_labels(self) -> None:
        for addr in self.res.xrefs:
            if addr in self.syms.labels:
                continue
            if addr in self.res.funcs:
                self.auto[addr] = "SUB_%06X" % addr
            elif addr in self.res.insns:
                self.auto[addr] = "CODE_%06X" % addr
            else:
                self.auto[addr] = "DATA_%06X" % addr

    def label(self, addr: int) -> str | None:
        if self.raw:
            return None
        return self.syms.labels.get(addr) or self.auto.get(addr)

    def ref(self, addr: int) -> str:
        """Render an address as a label if we have one, else raw hex."""
        n = self.label(addr)
        return n if n else "$%06X" % addr

    def operand_text(self, i: Insn) -> str:
        mode, mn = i.mode, i.mnem
        if mode in ("rel", "rell"):
            if self.raw:
                # asar computes a displacement only for labels; a numeric
                # operand is taken as the literal displacement byte.
                return " " + self.branch_labels[i.target]
            return " " + self.ref(i.target)
        if mode in ("abs", "abl") and mn in ("JMP", "JSR", "JML", "JSL"):
            n = self.label(i.target)
            if n:
                return " " + n
            return (" $%04X" if mode == "abs" else " $%06X") % i.operand
        if mode in ("immM", "immX"):
            w = i.size - 1
            return (" #$%02X" if w == 1 else " #$%04X") % i.operand
        if mode == "bm":
            # object code is `54/44 dstbank srcbank`; asar emits the two
            # operands in the order written, so keep memory order.
            dst, src = i.operand & 0xFF, i.operand >> 8
            return " $%02X,$%02X" % (dst, src)
        txt = FORMAT[mode].format(i.operand)
        # annotate known RAM/hardware names
        if not self.raw and mode in ("abs", "abx", "aby", "dp", "dpx", "dpy",
                                     "idp", "idx", "idy", "idl", "idly"):
            base = i.operand if mode.startswith("ab") else i.operand
            nm = self.syms.ram_name(base) or self.syms.ram_name(0x7E0000 | base)
            if nm:
                txt = txt.replace("$%04X" % i.operand, nm).replace("$%02X" % i.operand, nm)
        return txt

    def mnemonic(self, i: Insn) -> str:
        mn = i.mnem.lower()
        if i.mode in NO_SUFFIX:
            return mn
        if i.mode in ("immM", "immX"):
            return mn + (".b" if i.size - 1 == 1 else ".w")
        if i.mnem in ("JMP", "JSR", "JML", "JSL") and self.label(i.target or -1):
            return mn                       # let asar size it from the label
        return mn + SUFFIX.get(i.mode, "")

    def line(self, i: Insn) -> str:
        pc = self.rom.snes_to_pc(i.addr)
        raw = " ".join("%02X" % b for b in self.rom.data[pc:pc + i.size])
        text = "    %-8s%s" % (self.mnemonic(i), self.operand_text(i))
        if self.raw:
            return "    %-8s%s" % (self.mnemonic(i), self.operand_text(i))
        cmt = self.syms.comments.get(i.addr, "")
        if self.show_bytes:
            note = "%06X %-11s M=%d X=%d" % (i.addr, raw, i.m, i.x)
            text = "%-40s; %s" % (text, note)
            if cmt:
                text += "  " + cmt
        elif cmt:
            text = "%-40s; %s" % (text, cmt)
        return text

    def render(self, start: int | None = None, end: int | None = None) -> str:
        addrs = sorted(a for a in self.res.insns
                       if (start is None or a >= start) and (end is None or a < end))
        out: list[str] = []
        prev_end = None
        for a in addrs:
            i = self.res.insns[a]
            if prev_end is not None and a != prev_end:
                out.append("")
                out.append("org $%06X" % a)
            elif prev_end is None:
                out.append("org $%06X" % a)
            lbl = self.label(a)
            if lbl:
                out.append("")
                xr = self.res.xrefs.get(a)
                if xr:
                    out.append("; xrefs: " + ", ".join("$%06X" % x for x in sorted(xr)[:8])
                               + (" ..." if len(xr) > 8 else ""))
                out.append(lbl + ":")
            out.append(self.line(i))
            prev_end = (a & 0xFF0000) | ((a + i.size) & 0xFFFF)
        return "\n".join(out) + "\n"
