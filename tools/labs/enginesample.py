#!/usr/bin/env python3
"""Write each driver-pair's ENGINE loop out of the game's own BRR.

The engine is not one sound.  Measured on the chip with the rev word
pinned (NOTES 234), the four pairs each key a different sample on DSP
voice 7 and walk it with a different law:

    Mario, Luigi   SRCN $02   P = $4700 + 34 * rev
    Bowser, DK     SRCN $03   P = $4800 + 38 * rev
    Yoshi, Koopa   SRCN $18   P = $4600 + 29 * rev
    Peach, Toad    SRCN $17   P = $4600 + 19 * rev

(The engine pairs are NOT the stat pairs - Peach rides with Toad here,
not with Yoshi.)  Each file is the sample's own loop region, RAW: no
normalisation, so one driver staying quieter than another stays the
game's decision, not ours.  The port resamples it per frame at the rate
its law asks for.

    tools/labs/enginesample.py snapshot.spc [outdir]     # default rom/sfx
"""
import sys, os, wave, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brr

ENGINES = [(0x02, 'Mario, Luigi'), (0x03, 'Bowser, DK'),
           (0x17, 'Peach, Toad'),  (0x18, 'Yoshi, Koopa')]
OUT_SR = 32000

snap = sys.argv[1]
outdir = sys.argv[2] if len(sys.argv) > 2 else 'rom/sfx'
samples = brr.load_samples(snap, [s for s, _ in ENGINES])

for srcn, who in ENGINES:
    if srcn not in samples:
        print('SRCN $%02X not in %s - is this an in-race snapshot?' % (srcn, snap))
        continue
    pcm, loop = samples[srcn]
    if loop is None:
        loop = 0
    region = pcm[loop:]
    path = os.path.join(outdir, 'engine%02X.wav' % srcn)
    w = wave.open(path, 'wb')
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(OUT_SR)
    w.writeframes(struct.pack('<%dh' % len(region), *region))
    w.close()
    print('%s: SRCN $%02X (%s) %d samples, peak %d'
          % (os.path.basename(path), srcn, who, len(region),
             max(abs(x) for x in region)))
