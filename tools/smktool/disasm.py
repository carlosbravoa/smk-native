"""Tracing 65816 disassembler with context-sensitive M/X flag propagation.

The hard part of 65816 disassembly is that instruction *length* depends on the
M and X processor flags, so a wrong guess desynchronises the whole stream.
This tracer follows real control flow and computes flag state per path.

Subroutine calls are handled with context-sensitive summaries: for each
(callee, entry-flags) pair we record the flag states observed at its RTS/RTL,
and resume a call site only with the exits produced by *that* entry context.
Without this, a small helper called from both 8-bit and 16-bit code
cross-contaminates every one of its callers.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from collections import defaultdict

from .opcodes import OPCODES, TERMINATORS, BRANCHES, CALLS, operand_size
from .rom import Rom, MappingError
from .symbols import Symbols


def s8(v: int) -> int:  return v - 0x100 if v & 0x80 else v
def s16(v: int) -> int: return v - 0x10000 if v & 0x8000 else v


@dataclass
class Insn:
    addr: int
    size: int
    op: int
    mnem: str
    mode: str
    operand: int
    m: int
    x: int
    target: int | None = None


@dataclass
class TraceResult:
    insns: dict[int, Insn] = field(default_factory=dict)
    xrefs: dict[int, set[int]] = field(default_factory=lambda: defaultdict(set))
    conflicts: set[int] = field(default_factory=set)
    indirect: set[int] = field(default_factory=set)
    unmapped: set[int] = field(default_factory=set)
    funcs: set[int] = field(default_factory=set)
    unresolved_calls: set[int] = field(default_factory=set)

    def coverage(self, rom: Rom) -> set[int]:
        pcs: set[int] = set()
        for i in self.insns.values():
            try:
                base = rom.snes_to_pc(i.addr)
            except MappingError:
                continue
            pcs.update(range(base, base + i.size))
        return pcs


class Tracer:
    MAX_STACK = 8

    def __init__(self, rom: Rom, syms: Symbols | None = None):
        self.rom = rom
        self.syms = syms or Symbols()
        self.res = TraceResult()
        self.jumptab_targets: dict[int, list[int]] = {}
        self.parents: dict[tuple, tuple] = {}

    # ------------------------------------------------------------------
    def trace(self, entries: list[tuple[int, int, int]] | None = None) -> TraceResult:
        ents = list(entries or [])
        ents += self.syms.entries
        ents += self._jumptable_entries()
        if not ents:
            ents = [(v, 1, 1) for v in self._vector_entries()]

        work: list[tuple] = []
        seen: set[tuple] = set()
        # ctx = (callee_entry_addr, entry_m, entry_x)
        summaries: dict[tuple, set[tuple[int, int]]] = defaultdict(set)
        callers: dict[tuple, set[tuple]] = defaultdict(set)
        cur: list = [None]

        def push(addr, m, x, stk, ctx, why=""):
            addr = self.canon(addr)
            key = (addr, m, x, stk)
            if key in seen:
                return False
            seen.add(key)
            self.parents.setdefault(key, (cur[0], why))
            work.append((addr, m, x, stk, ctx))
            return True

        for a, m, x in ents:
            self.res.funcs.add(a)
            push(a, m, x, (), (a, m, x), "entry")

        while True:
            self._drain(work, seen, summaries, callers, push, cur)
            # Fixpoint fallback: a callee that never produced a summary (it
            # jumps away, is data, or was never fully traced) still has to let
            # its call sites continue.  Resume them with the call-site flags.
            progress = False
            for ctx, sites in list(callers.items()):
                if summaries.get(ctx):
                    continue
                for (ret, stk, _pctx, cm, cx) in list(sites):
                    self.res.unresolved_calls.add(ctx[0])
                    cur[0] = None
                    if push(ret, cm, cx, stk, _pctx, "call-return(assumed-unchanged)"):
                        progress = True
            if not progress and not work:
                break

        return self.res

    # ------------------------------------------------------------------
    def _drain(self, work, seen, summaries, callers, push, cur):
        while work:
            addr, m, x, stk, ctx = work.pop()
            cur[0] = (addr, m, x, stk)

            hint = self.syms.flags.get(addr)
            if hint:
                m = m if hint[0] is None else hint[0]
                x = x if hint[1] is None else hint[1]

            if self.syms.in_data(addr) is not None:
                continue

            try:
                pc = self.rom.snes_to_pc(addr)
            except MappingError:
                self.res.unmapped.add(addr)
                continue

            ins = self.decode(addr, pc, m, x)
            if ins is None:
                continue
            prev = self.res.insns.get(addr)
            if prev is not None and (prev.m, prev.x) != (m, x):
                self.res.conflicts.add(addr)
            self.res.insns[addr] = ins

            nm, nx, nstk = m, x, stk
            mn, mode = ins.mnem, ins.mode

            if mn == "REP":
                if ins.operand & 0x20: nm = 0
                if ins.operand & 0x10: nx = 0
            elif mn == "SEP":
                if ins.operand & 0x20: nm = 1
                if ins.operand & 0x10: nx = 1
            elif mn == "XCE":
                nm = nx = 1
            elif mn == "PHP":
                nstk = (stk + ((m, x),))[-self.MAX_STACK:]
            elif mn == "PLP" and stk:
                nm, nx = stk[-1]
                nstk = stk[:-1]

            nxt = (addr & 0xFF0000) | ((addr + ins.size) & 0xFFFF)

            if mn in BRANCHES:
                self.res.xrefs[ins.target].add(addr)
                push(ins.target, nm, nx, nstk, ctx, "branch")
                push(nxt, nm, nx, nstk, ctx, "fallthrough")
            elif mn in CALLS:
                if mode == "iax":
                    self.res.indirect.add(addr)
                    push(nxt, nm, nx, nstk, ctx, "after-indirect-call")
                else:
                    t = self.canon(ins.target)
                    self.res.xrefs[t].add(addr)
                    self.res.funcs.add(t)
                    cctx = (t, nm, nx)
                    callers[cctx].add((nxt, nstk, ctx, nm, nx))
                    push(t, nm, nx, (), cctx, "call from $%06X" % addr)
                    for (em, ex) in list(summaries.get(cctx, ())):
                        push(nxt, em, ex, nstk, ctx, "return of $%06X" % t)
            elif mn in ("JMP", "JML"):
                if mode in ("ind", "iax", "ial"):
                    self.res.indirect.add(addr)
                else:
                    self.res.xrefs[ins.target].add(addr)
                    push(ins.target, nm, nx, nstk, ctx, "jmp")
            elif mn in ("BRA", "BRL"):
                self.res.xrefs[ins.target].add(addr)
                push(ins.target, nm, nx, nstk, ctx, "bra")
            elif mn in ("RTS", "RTL"):
                if (nm, nx) not in summaries[ctx]:
                    summaries[ctx].add((nm, nx))
                    for (ret, rstk, rctx, _cm, _cx) in list(callers.get(ctx, ())):
                        push(ret, nm, nx, rstk, rctx, "rts of $%06X" % ctx[0])
            elif mn in ("RTI", "STP"):
                pass
            else:
                push(nxt, nm, nx, nstk, ctx, "seq")

    def canon(self, addr: int) -> int:
        """Collapse ROM mirrors onto one canonical SNES address.

        A 512 KB HiROM answers at $00-$0F, $80-$8F (upper halves) and
        $40-$47, $C0-$C7 (full banks) for the same bytes; SMK really does call
        into several of those aliases.  Without this every mirrored routine
        would be disassembled more than once."""
        try:
            return self.rom.pc_to_snes(self.rom.snes_to_pc(addr))
        except MappingError:
            return addr

    # ------------------------------------------------------------------
    def decode(self, addr: int, pc: int, m: int, x: int) -> Insn | None:
        if pc >= len(self.rom.data):
            return None
        op = self.rom.data[pc]
        mnem, mode = OPCODES[op]
        n = operand_size(mode, m, x)
        if pc + 1 + n > len(self.rom.data):
            return None
        val = 0
        for i in range(n):
            val |= self.rom.data[pc + 1 + i] << (8 * i)
        ins = Insn(addr, 1 + n, op, mnem, mode, val, m, x)
        bank = addr & 0xFF0000
        if mode == "rel":
            ins.target = bank | ((addr + 2 + s8(val)) & 0xFFFF)
        elif mode == "rell":
            ins.target = bank | ((addr + 3 + s16(val)) & 0xFFFF)
        elif mode == "abs" and mnem in ("JMP", "JSR"):
            ins.target = bank | val
        elif mode == "abl" and mnem in ("JML", "JSL"):
            ins.target = val
        return ins

    def _jumptable_entries(self) -> list[tuple[int, int, int]]:
        out: list[tuple[int, int, int]] = []
        for jt in self.syms.jumptabs:
            width = 3 if jt.kind == "long" else 2
            try:
                pc = self.rom.snes_to_pc(jt.addr)
            except MappingError:
                continue
            bank = jt.addr & 0xFF0000
            targets = []
            for i in range(jt.count):
                o = pc + i * width
                if jt.kind == "long":
                    t = self.rom.u24(o)
                else:
                    t = bank | self.rom.u16(o)
                    if jt.kind == "rts":
                        t = bank | ((t + 1) & 0xFFFF)
                t = self.canon(t)
                targets.append(t)
                out.append((t, jt.m, jt.x))
                self.res.xrefs[t].add(jt.addr + i * width)
                self.res.funcs.add(t)
            self.jumptab_targets[jt.addr] = targets
        return out

    def _vector_entries(self) -> list[int]:
        v = self.rom.vectors()
        out = []
        for k in ("emu.RESET", "native.NMI", "native.IRQ", "native.COP",
                  "native.BRK", "native.ABORT"):
            a = v.get(k, 0)
            if 0x8000 <= a <= 0xFFFF:
                out.append(0x800000 | a)
        return sorted(set(out))

    # ------------------------------------------------------------------
    def explain(self, addr: int, m: int | None = None, x: int | None = None,
                limit: int = 30) -> list[str]:
        out: list[str] = []
        for k in [k for k in self.parents if k[0] == addr
                  and (m is None or k[1] == m) and (x is None or k[2] == x)]:
            out.append("state $%06X M=%d X=%d" % (k[0], k[1], k[2]))
            c, n = k, 0
            while n < limit:
                par = self.parents.get(c)
                if not par or par[0] is None:
                    out.append("    <- %s" % (par[1] if par else "?"))
                    break
                p, why = par
                out.append("    <- $%06X M=%d X=%d  [%s]" % (p[0], p[1], p[2], why))
                c, n = p, n + 1
            out.append("")
        return out
