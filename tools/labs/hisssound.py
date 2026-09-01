#!/usr/bin/env python3
"""The off-road hiss - the user's "when driving on grass there is a sound
coming up, like S-S-S".

It is never queued, so no amount of tapping the sound entry finds it: it
is DSP voice 5 keying sample $04 over and over while the kart is on
rough ground.  Found by forcing the ground rather than hunting for it -
RAM $0B00 is the tilemap-byte -> surface-class table (NOTES 011), so
overwriting every entry makes the whole course one class and the same
recorded inputs can be run over each in turn (tools/labs/mame/
surfhiss.lua).  Of fourteen classes only two hiss, and at different
pitches:

    class $5A (grass)  sample $04 at pitch $0600  (12000 Hz)
    class $58 (sand)   sample $04 at pitch $0400  ( 8000 Hz)

and the voice is re-keyed about every 10 frames, which is the S-S-S.
One clean cycle of its ENVX, logged per frame, is the envelope below.

    tools/labs/hisssound.py snap.spc PITCH out.wav
"""
import sys, os, wave, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brr

SRCN = 0x04
FPS = 60.0988
OUT_SR = 32000
# MEASURED, frames 2870..2879 of the forced-grass run: one whole cycle of
# voice 5's ENVX, from the key-on to the release.
ENV = [34, 68, 100, 118, 81, 72, 63, 57, 50, 0]

def main():
    snap, pitch, out = sys.argv[1], int(sys.argv[2], 0), sys.argv[3]
    pcm, loop = brr.load_samples(snap, [SRCN])[SRCN]
    if loop is None:
        loop = 0
    rate = (pitch & 0x3FFF) / 4096.0 * OUT_SR
    step = rate / OUT_SR
    n = int(round(len(ENV) / FPS * OUT_SR))      # one 10-frame period
    per_frame = n / float(len(ENV))
    buf = []
    phase = 0.0
    for i in range(n):
        j = int(phase)
        if j >= len(pcm):                        # the sample's own loop
            phase = loop + (phase - len(pcm))
            j = int(phase)
        # the envelope, linearly between the frames it was logged at
        t = i / per_frame
        k = int(t)
        a = ENV[k % len(ENV)]
        b = ENV[(k + 1) % len(ENV)]
        e = (a + (b - a) * (t - k)) / 127.0
        buf.append(int(max(-32768, min(32767, pcm[j] * e))))
        phase += step
    w = wave.open(out, 'wb')
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(OUT_SR)
    w.writeframes(struct.pack('<%dh' % len(buf), *buf))
    w.close()
    print('%s: SRCN $%02X at pitch $%04X (%.0f Hz), %d frames = %.3f s, peak %d'
          % (os.path.basename(out), SRCN, pitch, rate, len(ENV),
             len(buf) / float(OUT_SR), max(abs(x) for x in buf)))

main()
