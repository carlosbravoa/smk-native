"""The DSP-1 coprocessor (NEC uPD77C25 with Nintendo's mask program).

A full-command-set model.  The command interface - which commands exist,
their parameter/result counts, and what each computes - is documented
hardware behaviour; the arithmetic below is our own implementation of that
behaviour, derived from the geometry each command performs.  It is NOT
bit-exact to the real chip's fixed-point pipeline; where that matters it is
called out, and docs/NOTES.md tracks what has been verified.

Why completeness matters more than precision here: one unknown command used
to desynchronise the parameter stream, after which *every* later value was
misread (NOTES 038).  With every command's shape known, the stream stays in
sync even where the maths is only approximate.

Conventions, validated for command $04 against the game's own motion code:
  - angles: 65536 = one full turn
  - trig results: radius * sin(angle), unshifted
Protocol: command byte to DR ($6000), parameters as 16-bit little-endian
words, poll SR ($7000) bit 7, read results back LSB-first.
"""
from __future__ import annotations
import math


def s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def u16(v: float | int) -> int:
    return int(v) & 0xFFFF


def clamp16(v: float | int) -> int:
    v = int(v)
    if v > 0x7FFF:  v = 0x7FFF
    if v < -0x8000: v = -0x8000
    return v & 0xFFFF


ANG = 2.0 * math.pi / 65536.0


class DSP1:
    #   command: (n_params, n_results)
    SHAPES = {
        0x00: (2, 1),    # multiply
        0x20: (2, 1),    # multiply (alias on this program)
        0x10: (2, 2),    # inverse (mantissa, exponent)
        0x04: (2, 2),    # sin/cos of angle, scaled by radius
        0x0C: (3, 2),    # 2D rotate
        0x1C: (6, 3),    # 3D rotate
        0x08: (3, 2),    # vector squared -> 32-bit (LSW, MSW)
        0x18: (4, 1),    # range: x^2+y^2+z^2 - r^2
        0x28: (3, 1),    # vector length
        0x38: (3, 1),    # vector length (alias)
        0x01: (4, 0),    # set attitude matrix A (m, az, ay, ax)
        0x11: (4, 0),    #   ...B
        0x21: (4, 0),    #   ...C
        0x03: (3, 3),    # objective A: global -> object frame
        0x13: (3, 3),    0x23: (3, 3),
        0x0D: (3, 3),    # subjective A: object -> global frame
        0x1D: (3, 3),    0x2D: (3, 3),
        0x0B: (3, 1),    # scalar A: inner product with attitude row
        0x1B: (3, 1),    0x2B: (3, 1),
        0x14: (6, 3),    # gyrate (APPROXIMATE - see method)
        0x02: (7, 4),    # projection parameter (camera setup)
        0x0A: (1, 4),    # raster: per-scanline Mode 7 matrix
        0x06: (3, 3),    # project world point -> screen H, V, size
        0x0E: (2, 2),    # target: screen point -> world X, Y
        0x0F: (1, 1),    # memory test -> 0
        0x2F: (1, 1),    # memory size
        0x1F: (1, 1024), # ROM dump (zeros here; no game data)
    }

    def __init__(self):
        self.cmd = None
        self.params: list[int] = []
        self.results: list[int] = []
        self.out_index = 0
        self._pending_lo = None
        # attitude matrices A, B, C as 3x3 float, identity to start
        self.mat = [[[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]] for _ in range(3)]
        # projection state from command $02
        self.fx = self.fy = self.fz = 0.0
        self.lfe = 1.0
        self.les = 1.0
        self.aas = 0.0          # azimuth, radians
        self.azs = 0.0          # tilt below horizontal, radians
        self.hoff = 0.0         # horizon offset on screen
        # bookkeeping
        # raster streaming ($0A): one Vs, then a result group per scanline,
        # auto-advancing as each group is read out; a written word of $8000
        # is the terminator - after one, the next known command byte at a
        # word boundary ends the mode.  Read off the game's own transaction
        # structure (docs/NOTES.md 039).
        self.raster = False
        self.raster_vs = 0
        self.raster_vs_pending = False
        self.calls: list[tuple[int, list[int], list[int]]] = []
        self.seen: dict[int, int] = {}
        self.unknown: dict[int, int] = {}
        self.trace_calls = False

    # ------------------------------------------------------------------ bus
    def read(self, addr: int) -> int:
        if (addr & 0xF000) == 0x7000:
            return 0x80                        # RQM: always ready
        if self.raster and self.out_index >= len(self.results) * 2:
            # the group for this scanline is consumed: advance to the next
            self.raster_vs += 1
            self.results = self._raster_group(self.raster_vs)
            self.out_index = 0
        if self.out_index < len(self.results) * 2:
            w = self.results[self.out_index >> 1]
            b = (w >> 8) & 0xFF if (self.out_index & 1) else w & 0xFF
            self.out_index += 1
            return b
        return 0xFF

    def write(self, addr: int, val: int) -> None:
        if (addr & 0xF000) != 0x6000:
            return
        val &= 0xFF
        if self.raster:
            # MEASURED protocol (the game's own reader at $81:F97D,
            # NOTES 083): the host writes command $0A and then exactly ONE
            # Vs word; every further result group comes from the DSP
            # auto-incrementing the line internally while the host only
            # READS.  The stream ends when the host writes the $8000
            # sentinel word; the next byte after that is a command.  The
            # old model treated every raster-mode write as another Vs,
            # which desynced the whole command stream.
            if self._pending_lo is None:
                self._pending_lo = val
                return
            word = self._pending_lo | val << 8
            self._pending_lo = None
            if self.raster_vs_pending:
                self.raster_vs_pending = False
                self.raster_vs = s16(word)
                self.results = self._raster_group(self.raster_vs)
                self.out_index = 0
            elif word == 0x8000:
                self.raster = False
            # any other word while rastering is ignored (not observed)
            return
        if self.cmd is None:
            self.seen[val] = self.seen.get(val, 0) + 1
            if val not in self.SHAPES:
                # Not a documented command.  Recorded, consumes nothing:
                # this cannot desync the stream, only miss work.
                self.unknown[val] = self.unknown.get(val, 0) + 1
                return
            self.cmd = val
            self.params = []
            self.results = []
            self.out_index = 0
            self._pending_lo = None
            if self.SHAPES[val][0] == 0:
                self.execute()
            return
        if self._pending_lo is None:
            self._pending_lo = val
            return
        self.params.append(self._pending_lo | val << 8)
        self._pending_lo = None
        if len(self.params) >= self.SHAPES[self.cmd][0]:
            self.execute()

    # ---------------------------------------------------------------- maths
    def execute(self) -> None:
        c, p = self.cmd, self.params
        fn = getattr(self, "_op_%02x" % c, None)
        if fn is None:
            n = self.SHAPES[c][1]
            self.results = [0] * n
        else:
            self.results = [w & 0xFFFF for w in fn([s16(v) for v in p])]
        want = self.SHAPES[c][1]
        if len(self.results) != want:            # a shape bug would desync
            self.results = (self.results + [0] * want)[:want]
        if self.trace_calls:
            self.calls.append((c, list(p), list(self.results)))
        self.cmd = None

    # --- arithmetic core ---
    def _op_00(self, p):                                   # multiply
        return [(p[0] * p[1]) >> 15]
    _op_20 = _op_00

    def _op_10(self, p):                                   # inverse
        a, b = p
        if a == 0:
            return [0x7FFF, 0x002F]
        sign = -1 if a < 0 else 1
        if b > 30: b = 30                 # hardware exponents are tiny;
        if b < -30: b = -30               # clamp so garbage can't overflow
        value = abs(a) / 32768.0 * (2.0 ** b)
        inv = 1.0 / value
        e = math.ceil(math.log2(inv)) if inv > 0 else 0
        m = inv / (2.0 ** e)                    # in (0.5, 1]
        A = min(int(m * 32768.0), 0x7FFF) * sign
        return [A, e]

    def _op_04(self, p):                                   # sin/cos
        ang, r = p[0] * ANG, p[1]
        return [int(r * math.sin(ang)), int(r * math.cos(ang))]

    def _op_0c(self, p):                                   # 2D rotate
        ang, x, y = p[0] * ANG, p[1], p[2]
        ca, sa = math.cos(ang), math.sin(ang)
        # documented direction: X2 = X cos + Y sin ; Y2 = -X sin + Y cos
        return [clamp16(x * ca + y * sa), clamp16(-x * sa + y * ca)]

    def _op_1c(self, p):                                   # 3D rotate
        az, ay, ax, x, y, z = p
        v = self._rot3(az * ANG, ay * ANG, ax * ANG, (x, y, z))
        return [clamp16(v[0]), clamp16(v[1]), clamp16(v[2])]

    @staticmethod
    def _rot3(az, ay, ax, v):
        x, y, z = v
        ca, sa = math.cos(az), math.sin(az)                 # about Z
        x, y = x * ca + y * sa, -x * sa + y * ca
        ca, sa = math.cos(ay), math.sin(ay)                 # about Y
        x, z = x * ca - z * sa, x * sa + z * ca
        ca, sa = math.cos(ax), math.sin(ax)                 # about X
        y, z = y * ca + z * sa, -y * sa + z * ca
        return (x, y, z)

    def _op_08(self, p):                                   # vector squared
        s2 = (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) << 1
        return [s2 & 0xFFFF, (s2 >> 16) & 0xFFFF]

    def _op_18(self, p):                                   # range
        d = (p[0] * p[0] + p[1] * p[1] + p[2] * p[2] - p[3] * p[3]) >> 15
        return [clamp16(d)]

    def _op_28(self, p):                                   # vector length
        return [clamp16(math.sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]))]
    _op_38 = _op_28

    # --- attitude matrices ---
    def _set_attitude(self, idx, p):
        m, az, ay, ax = p
        scale = m / 32768.0
        # rows of Rz*Ry*Rx, scaled: rotate basis vectors through _rot3
        rows = []
        for basis in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
            rows.append([c * scale for c in
                         self._rot3(az * ANG, ay * ANG, ax * ANG, basis)])
        self.mat[idx] = rows
        return []

    def _op_01(self, p): return self._set_attitude(0, p)
    def _op_11(self, p): return self._set_attitude(1, p)
    def _op_21(self, p): return self._set_attitude(2, p)

    def _mat_vec(self, idx, v, transpose=False):
        M = self.mat[idx]
        out = []
        for i in range(3):
            if transpose:
                out.append(M[0][i] * v[0] + M[1][i] * v[1] + M[2][i] * v[2])
            else:
                out.append(M[i][0] * v[0] + M[i][1] * v[1] + M[i][2] * v[2])
        return [clamp16(c) for c in out]

    def _op_03(self, p): return self._mat_vec(0, p)
    def _op_13(self, p): return self._mat_vec(1, p)
    def _op_23(self, p): return self._mat_vec(2, p)
    def _op_0d(self, p): return self._mat_vec(0, p, transpose=True)
    def _op_1d(self, p): return self._mat_vec(1, p, transpose=True)
    def _op_2d(self, p): return self._mat_vec(2, p, transpose=True)

    def _scalar(self, idx, p):
        M = self.mat[idx]
        return [clamp16(M[2][0] * p[0] + M[2][1] * p[1] + M[2][2] * p[2])]

    def _op_0b(self, p): return self._scalar(0, p)
    def _op_1b(self, p): return self._scalar(1, p)
    def _op_2b(self, p): return self._scalar(2, p)

    def _op_14(self, p):
        """Gyrate: angular-rate frame conversion.

        APPROXIMATE: modelled as a plain passthrough of the rate terms.
        The documented command converts object-frame angular velocity to
        global Euler-angle rates through the attitude matrix; until a
        consumer in this game is found and checked, the simple form is
        deliberately kept - it is stream-shape-correct and visibly labelled.
        """
        return [clamp16(p[3]), clamp16(p[4]), clamp16(p[5])]

    # --- projection group ---
    def _op_02(self, p):
        """Projection setup - the snes9x DSP1_Parameter flow in floats
        (NOTES 083).  Angles are 65536 = full turn.  Derived state:

            N = (sinAzs*-sinAas, sinAzs*cosAas, cosAzs)   view normal
            Centre = F + Lfe*N ;  G (eye) = Centre - Les*N
            VOffset = Les*cosAzs
            raster(Vs): s = CentreZ / (Vs*sinAzs + VOffset)
                        A =  s*cosAas   B = -s'*sinAas
                        C =  s*sinAas   D =  s'*cosAas
            (s' uses the secant-corrected scale; equal to s in the float
            model)  - matching snes9x's DSP1_Raster arithmetic with the
            2^-7 exponent folded into the 8.8 output scale.
        """
        fx, fy, fz, lfe, les, aas, azs = p
        self.fx, self.fy, self.fz = float(fx), float(fy), float(fz)
        self.lfe = float(lfe)
        self.les = float(les)
        self.aas = aas * ANG
        self.azs = azs * ANG
        sa, ca = math.sin(self.aas), math.cos(self.aas)
        sz, cz = math.sin(self.azs), math.cos(self.azs)
        self.sin_aas, self.cos_aas = sa, ca
        self.sin_azs, self.cos_azs = sz, cz
        nx, ny, nz = sz * -sa, sz * ca, cz
        self.centre = (self.fx + self.lfe * nx,
                       self.fy + self.lfe * ny,
                       self.fz + self.lfe * nz)
        self.eye = (self.centre[0] - self.les * nx,
                    self.centre[1] - self.les * ny,
                    self.centre[2] - self.les * nz)
        self.voffset = self.les * cz
        # view basis for op06/op0e (unchanged semantics)
        self.v_view = (sz * sa, -sz * ca, -cz) if False else (-nx, -ny, -nz)
        # forward on the ground for op0e
        self.v_right = (ca, sa, 0.0)
        vw, rt = self.v_view, self.v_right
        self.v_up = (vw[1] * rt[2] - vw[2] * rt[1],
                     vw[2] * rt[0] - vw[0] * rt[2],
                     vw[0] * rt[1] - vw[1] * rt[0])
        self.hoff = 0.0
        # results: Vof, Vva, Cx, Cy (screen-centre projection of Centre)
        return [0, 0, clamp16(self.centre[0]), clamp16(self.centre[1])]

    def _ground_depth(self, vs: float) -> float:
        """Depth along the view axis of the ground at screen row vs
        (the same denominator as the raster law)."""
        cz = self.centre[2] if hasattr(self, "centre") else 0.0
        den = vs * getattr(self, "sin_azs", 0.0) + getattr(self, "voffset", 1.0)
        if den <= 1e-6 or cz <= 0.0:
            return 32767.0
        d = cz / den * self.les
        return d if d > 0 else 32767.0

    def _op_0a(self, p):                                   # raster
        self.raster = True
        self.raster_vs_pending = False
        self.raster_vs = p[0]
        return self._raster_group(self.raster_vs)

    def _raster_group(self, vs: int):
        """Mode 7 matrix for one scanline - snes9x DSP1_Raster in floats.
        Output is the SNES 8.8 latch value (the DSP's 2^-7 exponent folds
        into the Q15->8.8 conversion: An_q15 * 2^-7 == s * 256)."""
        cz = self.centre[2] if hasattr(self, "centre") else 0.0
        den = float(vs) * getattr(self, "sin_azs", 0.0) \
            + getattr(self, "voffset", 0.0)
        if abs(den) < 1e-6 or cz <= 0.0:
            sc = 32767.0
        else:
            sc = cz / den * 256.0
        sa = getattr(self, "sin_aas", 0.0)
        ca = getattr(self, "cos_aas", 1.0)
        return [clamp16(sc * ca), clamp16(-sc * sa),
                clamp16(sc * sa), clamp16(sc * ca)]

    def _op_06(self, p):                                   # project
        x, y, z = p
        if not hasattr(self, "eye"):
            return [0x7FFF, 0x7FFF, 0]
        d = (x - self.eye[0], y - self.eye[1], z - self.eye[2])
        dot = lambda a, b: a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
        depth = dot(d, self.v_view)
        if depth < 1.0:
            return [0x7FFF, 0x7FFF, 0]
        h = self.les * dot(d, self.v_right) / depth
        v = self.les * dot(d, self.v_up) / depth
        m = 256.0 * self.les / depth                        # 8.8 size
        return [clamp16(h), clamp16(v), clamp16(m)]

    def _op_0e(self, p):                                   # target
        h, v = p
        if not hasattr(self, "eye"):
            return [0, 0]
        depth = self._ground_depth(float(v))
        lat = h * depth / self.les
        x = self.eye[0] + self.v_view[0] * depth + self.v_right[0] * lat
        y = self.eye[1] + self.v_view[1] * depth + self.v_right[1] * lat
        return [clamp16(x), clamp16(y)]

    # --- housekeeping ---
    def _op_0f(self, p): return [0x0000]
    def _op_2f(self, p): return [0x0100]
    def _op_1f(self, p): return [0] * 1024
