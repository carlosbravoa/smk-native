#!/usr/bin/env python3
"""Build a surface's held sound out of the game's own sample and the
DSP's own per-frame envelope and pitch.

The five rough surfaces (NOTES 236/237/241) are each one BRR sample on a
voice the driver keys and re-keys while the kart is on that ground.  They
are NOT steady tones, and rendering them as one was the mistake the user
caught on the bridge ("the sound for racing on a bridge is wrong"):

    $50 bridge    sample $16   TWO pitches, $0400 and $0300, alternating,
                               re-keyed every 5 frames
    $54 MC dirt   sample $00   envelope flat at 127, pitch DITHERED every
                               frame through $0380 $0300 $0400 $0280
    $58 sand      sample $04   one pitch $0400, re-keyed every 10 frames
    $5A grass     sample $04   one pitch $0600, re-keyed every 10 frames
    $5C mud       sample $12   one pitch $0A00, re-keyed every 10 frames

So nothing here is transcribed by hand: the per-frame (envelope, pitch)
comes straight out of a log taken from the chip with the whole course
forced to that class (tools/labs/mame/surfenv.lua), and one whole cycle
of it is rendered as a seamless loop.  A key-on - the phase reset - is
taken to be any frame where the envelope RISES.

    tools/labs/hisssound.py snap.spc SRCN log.txt START FRAMES out.wav
"""
import sys, os, wave, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brr

FPS = 60.0988
OUT_SR = 32000

def read_log(path):
    """-> [(frame, envx, pitch)] from surfenv.lua's `E f v env pitch`"""
    out = []
    for line in open(path):
        p = line.split()
        if len(p) == 5 and p[0] == 'E':
            out.append((int(p[1]), int(p[3]), int(p[4], 16)))
    out.sort()
    return out

def main():
    snap, srcn, log, start, frames, outp = sys.argv[1:7]
    srcn, start, frames = int(srcn, 0), int(start), int(frames)
    rows = [r for r in read_log(log) if r[0] >= start][:frames]
    if len(rows) < frames:
        print('only %d frames from %d in %s' % (len(rows), start, log))
        return
    pcm, loop = brr.load_samples(snap, [srcn])[srcn]
    if loop is None:
        loop = 0
    per_frame = OUT_SR / FPS
    buf = []
    phase = 0.0
    prev_env = None
    for i, (f, env, pitch) in enumerate(rows):
        if prev_env is not None and env > prev_env:
            phase = 0.0                      # a key-on: the sample restarts
        prev_env = env
        step = (pitch & 0x3FFF) / 4096.0 * OUT_SR / OUT_SR
        n = int(round((i + 1) * per_frame)) - int(round(i * per_frame))
        for _ in range(n):
            j = int(phase)
            if j >= len(pcm):
                phase = loop + (phase - len(pcm))
                j = int(phase)
            buf.append(int(max(-32768, min(32767, pcm[j] * env / 127.0))))
            phase += step
    w = wave.open(outp, 'wb')
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(OUT_SR)
    w.writeframes(struct.pack('<%dh' % len(buf), *buf))
    w.close()
    pitches = sorted({p for _, _, p in rows})
    keyons = sum(1 for i in range(1, len(rows)) if rows[i][1] > rows[i - 1][1])
    print('%s: SRCN $%02X, %d frames (%.3f s), %d key-on(s), pitch(es) %s, peak %d'
          % (os.path.basename(outp), srcn, frames, len(buf) / float(OUT_SR),
             keyons, ' '.join('$%04X' % p for p in pitches),
             max(abs(x) for x in buf)))

main()
