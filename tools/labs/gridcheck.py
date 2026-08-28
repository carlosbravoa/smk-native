"""Diff the decoded grid (src/course.c's table walk) against what the game
itself put on the track, for every course gridtable.py has measured.

    python3 tools/labs/gridtable.py        # writes tmp/gridtable.json
    python3 tools/labs/gridcheck.py
"""
import json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
rom = open(os.path.join(ROOT, "rom", "smk_usa.sfc"), "rb").read()


def pc(a): return (((a >> 16) & 0x3F) << 16) | (a & 0xFFFF)
def w(a): p = pc(a); return rom[p] | rom[p + 1] << 8
def s16(v): return v - 65536 if v > 32767 else v


def record(track):
    ent = w(0x818A79 + track * 2)
    rec = w(0x810000 | ent)
    return (w(0x810000 | ((rec + 2) & 0xFFFF)),
            w(0x810000 | ((rec + 4) & 0xFFFF)),
            s16(w(0x810000 | ((rec + 6) & 0xFFFF))))


def slot(track, i):
    x0, y0, st = record(track)
    return (x0 + (st if (i & 1) else 0), y0 + 24 * i, 0)


data = json.load(open(os.path.join(ROOT, "tmp", "gridtable.json")))
bad = 0
for t in sorted(data, key=int):
    karts = data[t]
    # the game fills the grid front to back down the order at $010E, and
    # the measurement is indexed by kart BLOCK, so match by position.
    want = [slot(int(t), i) for i in range(8)]
    got = sorted([tuple(k) for k in karts], key=lambda k: k[1])
    ok = got == sorted(want, key=lambda k: k[1])
    x0, y0, st = record(int(t))
    print("track %2s  x0 %4d y0 %4d step %4d   %s"
          % (t, x0, y0, st, "MATCH" if ok else "DIFF"))
    if not ok:
        bad += 1
        for a, b in zip(got, sorted(want, key=lambda k: k[1])):
            print("     game %-16s  decoded %-16s %s"
                  % (a, b, "" if a == b else "  <-"))
print("\n%d/%d courses match" % (len(data) - bad, len(data)))
sys.exit(1 if bad else 0)
