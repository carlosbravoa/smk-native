#!/usr/bin/env python3
"""Decode the game's own BRR samples out of an SPC snapshot.

The SPC700's samples are 9-byte BRR blocks: a header (shift, filter, and
the END/LOOP flags) then 16 four-bit deltas run through one of four
filters.  The DSP's DIR register points at a directory of (start, loop)
address pairs, one per sample number (SRCN), so a voice's SRCN is all
that is needed to find its sample.

    tools/labs/brr.py snapshot.spc [srcn ...]   # decode, report, write wavs
"""
import sys, os, wave, struct

def load_spc(path):
    d = open(path, 'rb').read()
    ram = d[0x100:0x10100]
    dsp = d[0x10100:0x10180]
    return ram, dsp

FILTERS = [(0, 0), (15/16, 0), (61/32, -15/16), (115/64, -13/16)]

def decode_brr(ram, addr, limit=0x10000):
    """-> (samples, loop_block_index, end_addr).  One block = 16 samples."""
    out = []
    p1 = p2 = 0
    loop_at = None
    a = addr
    blocks = 0
    while blocks < 2000:
        h = ram[a]
        shift, filt = h >> 4, (h >> 2) & 3
        end, loop = h & 1, (h >> 1) & 1
        f1, f2 = FILTERS[filt]
        for i in range(8):
            b = ram[(a + 1 + i) & 0xFFFF]
            for nib in (b >> 4, b & 15):
                s = nib - 16 if nib > 7 else nib
                s = (s << shift) >> 1 if shift else (s >> 4)
                v = s + f1 * p1 + f2 * p2
                v = max(-32768, min(32767, int(v)))
                out.append(v)
                p2, p1 = p1, v
        blocks += 1
        a = (a + 9) & 0xFFFF
        if end:
            return out, loop, a
    return out, 0, a

def load_samples(path, want=None):
    """-> {srcn: (pcm, loop_sample_index_or_None)} for a snapshot."""
    ram, dsp = load_spc(path)
    dirbase = dsp[0x5D] << 8
    out = {}
    for srcn in (want if want is not None else range(64)):
        e = dirbase + srcn * 4
        start = ram[e] | ram[e + 1] << 8
        loop = ram[e + 2] | ram[e + 3] << 8
        if start in (0, 0xFFFF):
            continue
        pcm, has_loop, end = decode_brr(ram, start)
        li = None
        if has_loop and loop >= start:
            li = ((loop - start) // 9) * 16
            if li >= len(pcm):
                li = None
        out[srcn] = (pcm, li)
    return out


def main():
    path = sys.argv[1]
    ram, dsp = load_spc(path)
    dir_page = dsp[0x5D]
    dirbase = dir_page << 8
    print('DIR page $%02X -> $%04X' % (dir_page, dirbase))
    want = [int(x, 0) for x in sys.argv[2:]] or list(range(32))
    os.makedirs('tmp/brr', exist_ok=True)
    for srcn in want:
        e = dirbase + srcn * 4
        start = ram[e] | ram[e + 1] << 8
        loop = ram[e + 2] | ram[e + 3] << 8
        if start == 0 or start == 0xFFFF:
            continue
        pcm, has_loop, end = decode_brr(ram, start)
        loop_samples = ((end - loop) // 9) * 16 if loop >= start else 0
        print('SRCN %02X: start $%04X loop $%04X end $%04X  %5d samples'
              '  loop %5d samples  looping %d'
              % (srcn, start, loop, end, len(pcm), loop_samples, has_loop))
        w = wave.open('tmp/brr/%02X.wav' % srcn, 'wb')
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(32000)
        w.writeframes(b''.join(struct.pack('<h', s) for s in pcm)); w.close()

if __name__ == "__main__":
    main()
