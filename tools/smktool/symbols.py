"""Annotation database: labels, entry points, data regions, flag hints.

Plain-text, line-oriented, diff-friendly.  One directive per line:

    label   $80803A  Main_Init            ; a code/data label
    ram     $7E0100  Frame_Counter        ; RAM / DP / hardware symbol
    entry   $80803A  [M=1 X=1]            ; force a trace entry point
    flags   $80A100  M=0 X=1              ; assert flags at an address
    data    $818000  $2000  Tileset_Foo   ; a data region (addr, length, name)
    comment $80803A  text ...             ; inline comment
    jumptab $80A000  16 abs  Name M=0 X=0 ; jump table (addr, count, kind)

Kinds for jumptab: abs (2-byte, same bank), long (3-byte), rts (2-byte, -1).
"""
from __future__ import annotations
import glob, os, re
from dataclasses import dataclass, field


@dataclass
class DataRegion:
    addr: int
    length: int
    name: str
    kind: str = "bytes"      # bytes|words|longs|ptr16|ptr24|gfx|text


@dataclass
class JumpTable:
    addr: int
    count: int
    kind: str       # abs | long | rts
    name: str = ""
    m: int = 1
    x: int = 1


@dataclass
class Symbols:
    labels: dict[int, str] = field(default_factory=dict)
    ram: dict[int, str] = field(default_factory=dict)
    entries: list[tuple[int, int, int]] = field(default_factory=list)  # addr,m,x
    flags: dict[int, tuple[int, int]] = field(default_factory=dict)
    data: list[DataRegion] = field(default_factory=list)
    comments: dict[int, str] = field(default_factory=dict)
    jumptabs: list[JumpTable] = field(default_factory=list)

    # ---- io ----
    @classmethod
    def load(cls, *paths: str) -> "Symbols":
        s = cls()
        files: list[str] = []
        for p in paths:
            if os.path.isdir(p):
                files += sorted(glob.glob(os.path.join(p, "**", "*.sym"), recursive=True))
            elif os.path.exists(p):
                files.append(p)
        for f in files:
            s.read(f)
        return s

    def read(self, path: str) -> None:
        with open(path) as fh:
            for lineno, raw in enumerate(fh, 1):
                line = raw.split(";", 1)[0].strip()
                if not line:
                    continue
                try:
                    self._directive(line)
                except Exception as e:
                    raise ValueError(f"{path}:{lineno}: {e}\n  {raw.rstrip()}") from None

    @staticmethod
    def _num(t: str) -> int:
        t = t.strip()
        if t.startswith("$"):
            return int(t[1:].replace(":", ""), 16)
        if t.lower().startswith("0x"):
            return int(t, 16)
        return int(t, 0)

    def _directive(self, line: str) -> None:
        p = line.split()
        d = p[0].lower()
        if d == "label":
            self.labels[self._num(p[1])] = p[2]
        elif d == "ram":
            self.ram[self._num(p[1])] = p[2]
        elif d == "entry":
            m, x = 1, 1
            for t in p[2:]:
                if t.upper().startswith("M="): m = int(t[2:])
                if t.upper().startswith("X="): x = int(t[2:])
            self.entries.append((self._num(p[1]), m, x))
        elif d == "flags":
            m = x = None
            for t in p[2:]:
                if t.upper().startswith("M="): m = int(t[2:])
                if t.upper().startswith("X="): x = int(t[2:])
            self.flags[self._num(p[1])] = (m, x)
        elif d == "data":
            kind = p[4] if len(p) > 4 else "bytes"
            self.data.append(DataRegion(self._num(p[1]), self._num(p[2]),
                                        p[3] if len(p) > 3 else "", kind))
        elif d == "comment":
            self.comments[self._num(p[1])] = line.split(None, 2)[2]
        elif d == "jumptab":
            m, x, name = 1, 1, ""
            for t in p[4:]:
                if t.upper().startswith("M="):   m = int(t[2:])
                elif t.upper().startswith("X="): x = int(t[2:])
                else:                            name = t
            self.jumptabs.append(JumpTable(self._num(p[1]), self._num(p[2]),
                                           p[3], name, m, x))
        else:
            raise ValueError(f"unknown directive {d!r}")

    # ---- queries ----
    def name_for(self, addr: int) -> str | None:
        return self.labels.get(addr)

    def ram_name(self, addr: int) -> str | None:
        return self.ram.get(addr)

    def in_data(self, addr: int) -> DataRegion | None:
        for r in self.data:
            if r.addr <= addr < r.addr + r.length:
                return r
        return None
