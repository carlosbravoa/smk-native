#!/usr/bin/env python3
"""How much of the game's sound map have we actually seen?

The ROM has a fixed number of places that ask for a sound: every JSL
into the play-sound entries ($81:F5xx).  tools/labs/mame/sfxwho.lua
reports which of them FIRED in a recording, with the id.  Comparing the
two says exactly what is covered and what is not - so "do we need to
replay every course?" has an answer instead of a guess.

    tools/labs/sfxcoverage.py [tmp/who/*.log]
"""
import sys, glob, os, collections

ROM = os.environ.get('SMK_ROM', 'rom/smk_usa.sfc')

def stubs():
    """Bank $84 holds a table of tiny "play sound N" stubs - LDA #id then
    a JML through the sound vector - and most of the game asks for its
    sounds by calling one of THOSE, not the $81:F5xx entry directly.  A
    coverage count that ignores them misses most of the map."""
    d = open(ROM, 'rb').read()
    off = 512 if len(d) % 0x8000 == 512 else 0
    body = d[off:]
    base = 0x04 * 0x10000
    out = {}
    for a in range(0xD600, 0xDC00):
        i = base + a
        if body[i] == 0xA9 and body[i + 2] == 0x00 and body[i + 3] in (0xDC, 0x5C, 0x6C, 0x7C):
            out[a] = body[i + 1]
    return out


def stub_callers(st):
    d = open(ROM, 'rb').read()
    off = 512 if len(d) % 0x8000 == 512 else 0
    body = d[off:]
    sites = {}
    for a, sid in st.items():
        tgt = bytes([a & 0xFF, (a >> 8) & 0xFF, 0x84])
        i = 0
        while True:
            i = body.find(b'\x22' + tgt, i + 1)
            if i < 0:
                break
            bank = 0x80 + ((i >> 16) & 0x3F)
            sites['%02X:%04X' % (bank, i & 0xFFFF)] = (sid, 0x8400 | (a & 0xFF))
    return sites


def call_sites():
    d = open(ROM, 'rb').read()
    off = 512 if len(d) % 0x8000 == 512 else 0
    body = d[off:]
    sites = {}
    for lo in range(0xF500, 0xF600):
        tgt = bytes([lo & 0xFF, (lo >> 8) & 0xFF, 0x81])
        i = 0
        while True:
            i = body.find(b'\x22' + tgt, i + 1)
            if i < 0:
                break
            idv = None
            for back in range(3, 14):
                j = i - back
                if j >= 0 and body[j] == 0xA9:
                    v = body[j + 1] | body[j + 2] << 8
                    if v < 0x200:
                        idv = v
                        break
            bank = 0x80 + ((i >> 16) & 0x3F)
            sites['%02X:%04X' % (bank, i & 0xFFFF)] = (idv, lo)
    return sites

def observed(paths):
    seen = collections.defaultdict(collections.Counter)
    per_session = collections.defaultdict(set)
    for p in paths:
        name = os.path.basename(p)[:-4]
        for line in open(p):
            q = line.strip().split(',')
            if len(q) == 4 and q[0].isdigit():
                site = q[2]
                seen[site][int(q[1], 16)] += 1
                per_session[name].add(site)
    return seen, per_session

def main():
    paths = sys.argv[1:] or sorted(glob.glob('tmp/who/*.log'))
    sites = call_sites()
    st = stubs()
    sites.update(stub_callers(st))
    seen, per_session = observed(paths)
    print('%d sound stubs in bank $84 covering ids %s\n'
          % (len(st), ' '.join('$%02X' % v for v in sorted(set(st.values())))))
    hit = {s for s in seen}
    print('%d call sites in the ROM; %d seen firing in %d recordings\n'
          % (len(sites), len(hit & set(sites)), len(paths)))
    print('SEEN:')
    for s in sorted(sites):
        if s in hit:
            ids = ' '.join('$%02X' % i for i in sorted(seen[s]))
            print('  $%s  %-6s  %d times' % (s, ids, sum(seen[s].values())))
    print('\nNOT SEEN (nothing in the recordings ever reached these):')
    for s in sorted(sites):
        if s not in hit:
            idv, entry = sites[s]
            print('  $%s  %s   via $81:%04X'
                  % (s, ('$%02X' % idv) if idv is not None else 'computed', entry))
    extra = hit - set(sites)
    if extra:
        print('\nfired from somewhere the static scan does not list (computed target?):')
        for s in sorted(extra):
            print('  $%s  %s' % (s, ' '.join('$%02X' % i for i in sorted(seen[s]))))
    print('\nper recording, how many distinct sites it exercised:')
    for name in sorted(per_session, key=lambda k: -len(per_session[k])):
        print('  %-12s %2d' % (name, len(per_session[name])))

main()
