"""Discover jump tables behind indirect JMP/JSR (abs,x) instructions.

`JMP ($nnnn,x)` fetches its pointer from the *program* bank, so the table
lives at $PB:nnnn.  Two independent signals give the entry count:

  * a bounds check (`CMP/CPX #n` + `BCS/BCC`) shortly before the jump;
  * how far the table runs before an entry stops looking like a code pointer.

Both are reported so a human can confirm before committing a `jumptab` line.
"""
from __future__ import annotations
from dataclasses import dataclass

from .rom import Rom, MappingError
from .disasm import TraceResult


@dataclass
class Candidate:
    site: int            # address of the indirect JMP/JSR
    kind: str            # JMP | JSR
    table: int           # snes address of the table
    scan_count: int      # entries that still look like code pointers
    bound_count: int | None   # from a nearby CMP/CPX immediate
    targets: list[int]
    m: int = 0                # flag state at the dispatch site
    x: int = 0

    @property
    def count(self) -> int:
        if self.bound_count and 0 < self.bound_count <= self.scan_count:
            return self.bound_count
        return self.scan_count

    @property
    def confidence(self) -> str:
        if self.bound_count and self.bound_count == self.scan_count:
            return "high"
        if self.bound_count:
            return "medium"
        return "low"


JUNK = {"BRK", "COP", "WDM", "STP"}

# how far an entry may sit from the rest of the table before we call it a
# different structure entirely
CLUSTER = 0x1000


def plausible_code(tracer, addr: int, m: int, x: int, limit: int = 48) -> bool:
    """Linear-decode from `addr` and judge whether it looks like real code.

    A genuine entry point reaches a terminator (RTS/RTL/RTI/JMP/BRA/...)
    without ever decoding BRK/COP/WDM/STP, which essentially never appear in
    shipped 65816 game code.  A stale pointer or a data byte pair almost
    always trips one of those within a few instructions.
    """
    from .opcodes import TERMINATORS
    a = addr
    for _ in range(limit):
        try:
            pc = tracer.rom.snes_to_pc(a)
        except MappingError:
            return False
        ins = tracer.decode(a, pc, m, x)
        if ins is None or ins.mnem in JUNK:
            return False
        if ins.mnem == "REP":
            if ins.operand & 0x20: m = 0
            if ins.operand & 0x10: x = 0
        elif ins.mnem == "SEP":
            if ins.operand & 0x20: m = 1
            if ins.operand & 0x10: x = 1
        if ins.mnem in TERMINATORS or ins.mnem == "RTI":
            return True
        a = (a & 0xFF0000) | ((a + ins.size) & 0xFFFF)
    return True          # long straight-line run, no junk: accept


def _plausible(rom: Rom, bank: int, ptr: int) -> bool:
    """Does $bank:ptr look like it could be executable ROM?"""
    hi = (bank >> 16) & 0xFF
    if 0x80 <= hi <= 0xBF or hi <= 0x3F:
        if ptr < 0x8000:
            return False
    try:
        rom.snes_to_pc(bank | ptr)
    except MappingError:
        return False
    return True


def find_bound(rom: Rom, res: TraceResult, site: int, back: int = 24) -> int | None:
    """Look back for a `CMP/CPX #n` guarding the dispatch."""
    addrs = [a for a in res.insns if (a >> 16) == (site >> 16) and a < site]
    addrs.sort()
    for a in reversed(addrs[-back:]):
        i = res.insns[a]
        if i.mnem in ("CMP", "CPX", "CPY") and i.mode in ("immM", "immX"):
            v = i.operand
            # the index is normally the entry number * 2
            return v // 2 if v and v % 2 == 0 else v
    return None


def discover(rom: Rom, res: TraceResult, max_entries: int = 128,
             tracer=None) -> list[Candidate]:
    out: list[Candidate] = []
    for site in sorted(res.indirect):
        ins = res.insns.get(site)
        if ins is None or ins.mode != "iax":
            continue
        bank = site & 0xFF0000
        table = bank | ins.operand
        try:
            pc = rom.snes_to_pc(table)
        except MappingError:
            continue
        targets: list[int] = []
        lo = hi = None
        for i in range(max_entries):
            o = pc + i * 2
            if o + 1 >= len(rom.data):
                break
            # stop if the table would run into code we already decoded
            if i and (table + i * 2) in res.insns:
                break
            ptr = rom.u16(o)
            if not _plausible(rom, bank, ptr):
                break
            # a table cannot extend past its own lowest forward target
            fwd = [t & 0xFFFF for t in targets if (t & 0xFFFF) > (table & 0xFFFF)]
            if fwd and (table & 0xFFFF) + i * 2 >= min(fwd):
                break
            # entries of one table cluster tightly; a far outlier ends it
            if len(targets) >= 3 and (ptr < lo - CLUSTER or ptr > hi + CLUSTER):
                break
            if tracer is not None and not plausible_code(
                    tracer, bank | ptr, ins.m, ins.x):
                break
            lo = ptr if lo is None else min(lo, ptr)
            hi = ptr if hi is None else max(hi, ptr)
            targets.append(bank | ptr)
        out.append(Candidate(site, ins.mnem, table, len(targets),
                             find_bound(rom, res, site), targets,
                             ins.m, ins.x))
    return out


# ---------------------------------------------------------------------------
def health(rom: Rom, res: TraceResult) -> dict:
    """Cheap signals that the trace has desynchronised.

    Real 65816 game code contains almost no BRK/COP/WDM/STP, and no two
    instructions ever overlap.  Both rise sharply when the tracer decodes
    data as code or gets a flag wrong.
    """
    junk = {"BRK": 0, "COP": 0, "WDM": 0, "STP": 0}
    for i in res.insns.values():
        if i.mnem in junk:
            junk[i.mnem] += 1

    # overlapping instructions: a start that falls inside another instruction
    starts = sorted(res.insns)
    overlaps = 0
    prev_end = None
    prev_bank = None
    for a in starts:
        bank = a >> 16
        if prev_end is not None and bank == prev_bank and a < prev_end:
            overlaps += 1
        end = (a & 0xFF0000) | ((a + res.insns[a].size) & 0xFFFF)
        if prev_end is None or a >= prev_end or bank != prev_bank:
            prev_end, prev_bank = end, bank
        else:
            prev_end = max(prev_end, end)

    return {
        "instructions": len(res.insns),
        "overlaps": overlaps,
        "conflicts": len(res.conflicts),
        "junk_opcodes": sum(junk.values()),
        "junk_detail": junk,
        "indirect": len(res.indirect),
        "unresolved_calls": len(res.unresolved_calls),
        "coverage_bytes": len(res.coverage(rom)),
    }


def emit_sym(cands: list[Candidate]) -> str:
    """Render discovered tables as .sym directives, ready to review."""
    out = ["; auto-discovered by `smk jumptables` - REVIEW BEFORE TRUSTING",
           "; count comes from the conservative pointer scan.", ""]
    for c in sorted(cands, key=lambda k: k.table):
        out.append("; $%06X %s (%s confidence, scan=%d bound=%s)"
                   % (c.site, c.kind, c.confidence, c.scan_count,
                      c.bound_count if c.bound_count is not None else "-"))
        out.append("jumptab $%06X %d abs JumpTable_%06X M=%d X=%d"
                   % (c.table, c.count, c.table, c.m, c.x))
        out.append("data    $%06X %d JumpTable_%06X ptr16"
                   % (c.table, c.count * 2, c.table))
        out.append("")
    return "\n".join(out)
