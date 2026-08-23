"""Super Mario Kart graphics compression.

Reverse engineered from the decompressors at $84E09E (output to WRAM bank
$7F) and $84DF38 (output to bank $7E).  The two routines are byte-identical
apart from the destination bank, so one codec covers both.

Stream format
-------------
A stream is a sequence of commands, terminated by a $FF byte.

    byte0 == $FF                  end of stream

    (byte0 & $E0) != $E0          short header
                                  cmd = byte0 >> 5          (0..6)
                                  len = (byte0 & $1F) + 1   (1..32)

    (byte0 & $E0) == $E0          long header, consumes byte1
                                  cmd = (byte0 >> 2) & 7    (0..7)
                                  len = (((byte0 & 3) << 8) | byte1) + 1
                                        (1..1024)

Commands
--------
    0  literal      copy `len` bytes straight from the stream
    1  byte fill    one byte, repeated `len` times
    2  word fill    two bytes, alternating, for `len` bytes total
    3  inc fill     one byte, incrementing by 1, for `len` bytes
    4  copy abs     16-bit offset from the start of the output, `len` bytes
    5  copy abs ^   as 4, but every byte is EOR $FF
    6  copy rel     8-bit distance back from the current output position
    7  copy rel ^   as 6, but every byte is EOR $FF

Commands 4-7 copy byte-by-byte through the output buffer, so an overlapping
run (distance < length) repeats - the usual LZ behaviour, and the game relies
on it.  Command 7 is unreachable from a short header because $E0 is the long
header escape; it only occurs in the long form.
"""
from __future__ import annotations

END = 0xFF


class CompressionError(ValueError):
    pass


def decompress(data: bytes, offset: int = 0, max_out: int = 0x10000,
               strict: bool = True) -> tuple[bytearray, int]:
    """Decode one stream. Returns (output, bytes_consumed)."""
    out = bytearray()
    p = offset
    n = len(data)
    while True:
        if p >= n:
            raise CompressionError(f"ran off the end of the ROM at ${p:X}")
        b0 = data[p]
        if b0 == END:
            p += 1
            break
        if (b0 & 0xE0) != 0xE0:
            cmd = b0 >> 5
            length = (b0 & 0x1F) + 1
            p += 1
        else:
            if p + 1 >= n:
                raise CompressionError("truncated long header")
            cmd = (b0 >> 2) & 7
            length = (((b0 & 3) << 8) | data[p + 1]) + 1
            p += 2

        if len(out) + length > max_out:
            raise CompressionError(
                f"output would exceed {max_out:#x} bytes (cmd {cmd}, len {length})")

        if cmd == 0:                                   # literal
            if p + length > n:
                raise CompressionError("literal runs past end of ROM")
            out += data[p:p + length]
            p += length
        elif cmd == 1:                                 # byte fill
            out += bytes([data[p]]) * length
            p += 1
        elif cmd == 2:                                 # word fill
            a, b = data[p], data[p + 1]
            out += bytes((a, b) * ((length + 1) // 2))[:length]
            p += 2
        elif cmd == 3:                                 # incrementing fill
            v = data[p]
            out += bytes((v + i) & 0xFF for i in range(length))
            p += 1
        elif cmd in (4, 5):                            # absolute back-reference
            src = data[p] | data[p + 1] << 8
            p += 2
            _copy(out, src, length, invert=(cmd == 5), strict=strict)
        elif cmd in (6, 7):                            # relative back-reference
            dist = data[p]
            p += 1
            src = len(out) - dist
            _copy(out, src, length, invert=(cmd == 7), strict=strict)
        else:                                          # unreachable
            raise CompressionError(f"bad command {cmd}")
    return out, p - offset


def _copy(out: bytearray, src: int, length: int, invert: bool, strict: bool) -> None:
    if src < 0:
        if strict:
            raise CompressionError(f"back-reference before start of output ({src})")
        src = 0
    for i in range(length):
        j = src + i
        if j >= len(out):
            if strict:
                raise CompressionError("back-reference reads past output")
            out.append(0)
            continue
        out.append(out[j] ^ 0xFF if invert else out[j])


# ---------------------------------------------------------------------------
# Length values worth trying for a command: the longest run available, and
# 32, because 32 is the last length that still fits a one-byte header.
def _lengths(L: int) -> tuple[int, ...]:
    return (L,) if L <= 32 else (L, 32)


LIT_LENGTHS = tuple(range(1, 33)) + (64, 128, 256, 512, 1024)


def compress(src: bytes) -> bytearray:
    """Encode a stream the game's decompressor accepts.

    Match finding is hash-indexed and covers both plain and inverted
    back-references (commands 5 and 7 - the original encoder plainly used
    them, and skipping them costs badly on tile data).  The parse is a
    shortest-path dynamic program over encoded size rather than a greedy
    scan, so a long match is not taken when two shorter commands encode the
    remainder more cheaply.

    Always verify with `decompress(compress(x)) == x`; the codec is only
    useful if it round-trips.
    """
    n = len(src)
    if n == 0:
        return bytearray([END])
    inverted = bytes(b ^ 0xFF for b in src)

    # ---- index every 3-byte key, plain and inverted ----
    plain: dict[bytes, list[int]] = {}
    inv: dict[bytes, list[int]] = {}
    for i in range(n - 2):
        plain.setdefault(src[i:i + 3], []).append(i)
        inv.setdefault(inverted[i:i + 3], []).append(i)

    # ---- candidate commands at each position ----
    def candidates(i: int) -> list[tuple[int, int, int, bytes]]:
        """(length, cost, cmd, payload)"""
        out: list[tuple[int, int, int, bytes]] = []

        def add(L: int, cmd: int, payload: bytes) -> None:
            for length in _lengths(L):
                if length >= 1:
                    out.append((length, len(_header(cmd, length)) + len(payload),
                                cmd, payload))

        run = 1
        while i + run < n and src[i + run] == src[i] and run < 1024:
            run += 1
        if run >= 3:
            add(run, 1, bytes([src[i]]))

        if i + 1 < n:
            wr = 2
            while i + wr < n and src[i + wr] == src[i + (wr & 1)] and wr < 1024:
                wr += 1
            if wr >= 4:
                add(wr, 2, src[i:i + 2])

        ir = 1
        while i + ir < n and src[i + ir] == ((src[i] + ir) & 0xFF) and ir < 1024:
            ir += 1
        if ir >= 3:
            add(ir, 3, bytes([src[i]]))

        for tbl, buf, cmd_abs, cmd_rel in ((plain, src, 4, 6),
                                           (inv, inverted, 5, 7)):
            L, pos = _match(tbl, buf, src, i, n)
            if L >= 3:
                dist = i - pos
                if 0 < dist <= 0xFF:
                    add(L, cmd_rel, bytes([dist]))
                if pos <= 0xFFFF:
                    add(L, cmd_abs, bytes([pos & 0xFF, pos >> 8]))
        return out

    # ---- shortest path from the end ----
    INF = float("inf")
    cost = [INF] * (n + 1)
    choice: list[tuple | None] = [None] * (n + 1)
    cost[n] = 0

    for i in range(n - 1, -1, -1):
        best, pick = INF, None
        for L in LIT_LENGTHS:
            if i + L > n:
                break
            c = len(_header(0, L)) + L + cost[i + L]
            if c < best:
                best, pick = c, (L, 0, src[i:i + L])
        for L, c0, cmd, payload in candidates(i):
            if i + L > n:
                continue
            c = c0 + cost[i + L]
            if c < best:
                best, pick = c, (L, cmd, payload)
        cost[i], choice[i] = best, pick

    out = bytearray()
    i = 0
    while i < n:
        L, cmd, payload = choice[i]
        out.extend(_header(cmd, L))
        out.extend(payload)
        i += L
    out.append(END)
    return out


MAX_CHAIN = 48          # positions examined per key; caps worst-case time


def _match(table, buf, src, i, n) -> tuple[int, int]:
    """Longest run at `i` matching `buf` at some earlier position.

    Overlapping runs are legal - the decompressor copies one byte at a time,
    so a distance shorter than the length simply repeats.
    """
    cands = table.get(src[i:i + 3])
    if not cands:
        return 0, 0
    best_len, best_pos = 0, 0
    for pos in reversed(cands):
        if pos >= i:
            continue
        L = 0
        while L < 1024 and i + L < n and buf[pos + L] == src[i + L]:
            L += 1
        if L > best_len:
            best_len, best_pos = L, pos
            if L >= 1024:
                break
        if i - pos > 0xFFFF:
            break
    return best_len, best_pos


def _header(cmd: int, length: int) -> bytes:
    if length < 1 or length > 1024:
        raise CompressionError(f"length {length} out of range")
    if length <= 32 and cmd <= 6:
        return bytes([(cmd << 5) | (length - 1)])
    v = length - 1
    return bytes([0xE0 | (cmd << 2) | (v >> 8), v & 0xFF])





# ---------------------------------------------------------------------------
def stream_size(data: bytes, offset: int, max_out: int = 0x20000
                ) -> tuple[int, int] | None:
    """Validate a stream without materialising it.

    Returns (output_size, bytes_consumed) or None if the stream is not valid
    at this offset.  Fast enough to sweep an entire ROM.
    """
    p, n, out = offset, len(data), 0
    while True:
        if p >= n:
            return None
        b0 = data[p]
        if b0 == END:
            return out, p + 1 - offset
        if (b0 & 0xE0) != 0xE0:
            cmd, length, p = b0 >> 5, (b0 & 0x1F) + 1, p + 1
        else:
            if p + 1 >= n:
                return None
            cmd = (b0 >> 2) & 7
            length = (((b0 & 3) << 8) | data[p + 1]) + 1
            p += 2
        if out + length > max_out:
            return None
        if cmd == 0:
            p += length
            if p > n:
                return None
        elif cmd == 1 or cmd == 3:
            p += 1
        elif cmd == 2:
            p += 2
        elif cmd in (4, 5):
            if p + 1 >= n:
                return None
            src = data[p] | data[p + 1] << 8
            if src >= out:
                return None          # reads uninitialised output
            p += 2
        else:
            if p >= n:
                return None
            dist = data[p]
            if dist == 0 or dist > out:
                return None
            p += 1
        out += length


def scan(data: bytes, start: int = 0, end: int | None = None,
         min_out: int = 512, min_in: int = 64) -> list[tuple[int, int, int]]:
    """Sweep for offsets that hold a plausible compressed stream.

    Returns (offset, output_size, consumed), largest output first.
    """
    end = len(data) if end is None else end
    hits = []
    for off in range(start, end):
        r = stream_size(data, off)
        if r and r[0] >= min_out and r[1] >= min_in:
            hits.append((off, r[0], r[1]))
    return hits
