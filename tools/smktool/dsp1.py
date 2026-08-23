"""The four DSP-1 commands Super Mario Kart actually uses.

Located by scanning for accesses to the data register at $6000 (see
docs/NOTES.md entry 008).  This is not a general DSP-1: implementing only
what the game issues keeps the semantics checkable.

Protocol: the 65816 writes a command byte to DR, then the parameters as
16-bit little-endian words, then polls the status register at $7000 for
bit 7 and reads the results back from DR.
"""
from __future__ import annotations
import math

# parameter / result word counts per command
SHAPES = {
    0x00: (2, 1),    # multiply
    0x04: (2, 2),    # sin/cos of an angle, scaled by a radius
    0x0C: (3, 2),    # 2D rotate
    0x28: (3, 1),    # vector length
}


def s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


class DSP1:
    def __init__(self):
        self.cmd = None
        self.params: list[int] = []
        self.results: list[int] = []
        self.out_index = 0
        self._pending_lo = None
        self.calls: list[tuple[int, list[int], list[int]]] = []   # for inspection

    # ---- bus ----
    def read(self, addr: int) -> int:
        if (addr & 0xF000) == 0x7000:
            return 0x80                      # always "ready"
        if self.out_index < len(self.results) * 2:
            w = self.results[self.out_index >> 1]
            b = (w >> 8) & 0xFF if (self.out_index & 1) else w & 0xFF
            self.out_index += 1
            return b
        return 0xFF

    def write(self, addr: int, val: int) -> None:
        if (addr & 0xF000) != 0x6000:
            return
        if self.cmd is None:
            self.cmd = val
            self.params = []
            self.results = []
            self.out_index = 0
            self._pending_lo = None
            if val not in SHAPES:
                raise NotImplementedError(
                    f"DSP-1 command ${val:02X} is not implemented; "
                    "Super Mario Kart was only seen to use $00/$04/$0C/$28")
            return
        if self._pending_lo is None:
            self._pending_lo = val
            return
        self.params.append(self._pending_lo | val << 8)
        self._pending_lo = None
        need = SHAPES[self.cmd][0]
        if len(self.params) >= need:
            self.execute()

    # ---- maths ----
    def execute(self) -> None:
        c, p = self.cmd, self.params
        if c == 0x00:                                   # multiply
            r = (s16(p[0]) * s16(p[1])) >> 15
            self.results = [r & 0xFFFF]
        elif c == 0x04:                                 # sin/cos
            angle = s16(p[0]) * math.pi / 32768.0
            radius = s16(p[1])
            self.results = [int(radius * math.sin(angle)) & 0xFFFF,
                            int(radius * math.cos(angle)) & 0xFFFF]
        elif c == 0x0C:                                 # 2D rotate
            angle = s16(p[0]) * math.pi / 32768.0
            x, y = s16(p[1]), s16(p[2])
            ca, sa = math.cos(angle), math.sin(angle)
            self.results = [int(x * ca - y * sa) & 0xFFFF,
                            int(x * sa + y * ca) & 0xFFFF]
        elif c == 0x28:                                 # vector length
            x, y, z = (s16(v) for v in p[:3])
            self.results = [int(math.sqrt(x * x + y * y + z * z)) & 0xFFFF]
        self.calls.append((c, list(p), list(self.results)))
        self.cmd = None
