#!/usr/bin/env python3
"""Render one of the game's sound effects from the CHIP, not the speaker.

Recording the effect and subtracting a baseline leaves the music
smeared underneath it (the driver re-juggles its voices when a sound
plays).  This route never records audio at all:

  * tools/labs/mame/voicedump.lua logs every DSP voice's SRCN, PITCH,
    VOL and ENVX every frame - once with the sound poked, once without;
  * the voices whose state DIFFERS are the effect's (a voice the effect
    stole from the music included - what the music would have played
    there is simply not rendered);
  * each of those voices is rebuilt from the game's own BRR sample
    (tools/labs/brr.py) at the logged pitch and envelope.

    tools/labs/sfxrender.py snap.spc base.log test.log OUT.wav

The result is the effect alone, at the game's own pitch, with no music
in it anywhere.
"""
import sys, wave, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brr

FPS = 60.0988
SR = 32000.0          # the DSP's own output rate; pitch $1000 = 1:1
OUT_SR = 32000

def read_log(path):
    rows = {}
    for line in open(path):
        p = line.strip().split(',')
        if len(p) != 7 or not p[0].isdigit():
            continue
        f, v = int(p[0]), int(p[1])
        rows[(f, v)] = (int(p[2]), int(p[3]), int(p[4], 16), int(p[5], 16), int(p[6]))
    return rows

def render_all(spc, test, frames, out_p):
    """Every voice, the whole window, mixed - no baseline, no filtering."""
    samples = brr.load_samples(spc)
    f0, f1 = frames[0], frames[-1]
    nout = int((f1 - f0 + 2) / FPS * OUT_SR)
    mix = np.zeros((nout, 2))
    for v in range(8):
        phase, cur = 0.0, None
        for f in frames:
            t = test.get((f, v))
            if t is None:
                continue
            vl, vr, pitch, srcn, envx = t
            samp = samples.get(srcn)
            if samp is None or envx == 0:
                if envx == 0:
                    cur = None
                continue
            pcm, loop = samp
            if srcn != cur:
                cur, phase = srcn, 0.0
            rate = (pitch & 0x3FFF) / 4096.0 * SR
            step = rate / OUT_SR
            n = int(OUT_SR / FPS)
            s0 = int((f - f0) / FPS * OUT_SR)
            nx = test.get((f + 1, v))
            gl, gr = (envx / 127.0) * (vl / 127.0), (envx / 127.0) * (vr / 127.0)
            e2 = nx[4] if nx else envx
            gl2 = (e2 / 127.0) * ((nx[0] if nx else vl) / 127.0)
            gr2 = (e2 / 127.0) * ((nx[1] if nx else vr) / 127.0)
            for i in range(n):
                j = int(phase)
                if j >= len(pcm):
                    if loop is None:
                        break
                    phase = loop + (phase - len(pcm)); j = int(phase)
                    if j >= len(pcm):
                        break
                k = s0 + i
                if 0 <= k < nout:
                    val = pcm[j] / 32768.0
                    a = i / float(n)
                    mix[k, 0] += val * (gl + (gl2 - gl) * a)
                    mix[k, 1] += val * (gr + (gr2 - gr) * a)
                phase += step
    peak = np.abs(mix).max()
    if peak < 1e-4:
        print('%s: silent' % os.path.basename(out_p)); return
    mix *= 22000.0 / peak
    fade = min(512, len(mix) // 8)
    if fade > 1:
        r = np.linspace(0, 1, fade)[:, None]
        mix[:fade] *= r; mix[-fade:] *= r[::-1]
    w = wave.open(out_p, 'wb')
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(OUT_SR)
    w.writeframes(mix.astype('<i2').tobytes()); w.close()
    print('%s: %.2f s, every voice, no subtraction' % (os.path.basename(out_p), len(mix) / OUT_SR))


def main():
    spc, base_p, test_p, out_p = sys.argv[1:5]
    # base "-" renders EVERY voice for the whole window and subtracts
    # nothing: that is how a passage the game plays on its own - the
    # countdown jingle over Lakitu's lights - is captured (NOTES 216).
    test = read_log(test_p)
    base = {} if base_p == '-' else read_log(base_p)
    if base_p == '-':
        frames = sorted({f for (f, v) in test})
        render_all(spc, test, frames, out_p)
        return
    frames = sorted({f for (f, v) in test})
    if not frames:
        print('no data'); return
    f0, f1 = frames[0], frames[-1]
    # The effect's voices are the ones that change IN THE FIRST FEW
    # FRAMES after the poke.  Later differences are the music drifting:
    # once a voice is stolen the song's own notes land elsewhere, and
    # rendering those would put the music back in by the side door.
    fire = None
    for line in open(test_p):
        if line.startswith('FIRE '):
            fire = int(line.split()[1])
    if fire is None:
        fire = f0 + 4
    # A voice belongs to the EFFECT when it starts playing a sample the
    # baseline is not playing anywhere at that moment.  Anything else
    # that differs is the song drifting because a voice was taken from
    # it, and rendering that would put the music back in by the side
    # door - which is what the first cut of this did.
    # MEASURED: the driver answers a request on VOICE 3 - every one of
    # the effects whose sample the music happened not to be playing was
    # found there - so voice 3 is taken whenever it differs from the
    # baseline after the poke.  Other voices count only when they start
    # a sample the baseline is not playing anywhere at that moment;
    # anything else that differs is the song drifting because a voice
    # was taken from it, and rendering that would put the music back in
    # by the side door (which is what the first cut of this did).
    SFX_VOICE = 3
    touched = {}
    for f in range(fire, min(fire + 12, f1 + 1)):
        live = {base[(f, u)][3] for u in range(8) if (f, u) in base}
        for v in range(8):
            t, b = test.get((f, v)), base.get((f, v))
            if t is None or b is None or v in touched:
                continue
            if v == SFX_VOICE:
                if t != b and (t[3] != b[3] or t[4] > 0):
                    touched[v] = f
            elif t[3] != b[3] and t[3] not in live:
                touched[v] = f
    srcns = {test[(f, v)][3] for v, f in touched.items() for f in [f]}
    samples = brr.load_samples(spc)
    nout = int((f1 - f0 + 2) / FPS * OUT_SR)
    mix = np.zeros((nout, 2))
    for v, first in sorted(touched.items()):
        phase, cur_srcn = 0.0, None
        quiet = same = 0
        first_srcn = test[(first, v)][3]
        kit = {test[(f2, v)][3] for f2 in range(first, min(first + 4, f1 + 1))
               if (f2, v) in test}
        for f in range(first, f1 + 1):
            t = test.get((f, v))
            if t is None:
                continue
            vl, vr, pitch, srcn, envx = t
            b = base.get((f, v))
            # The effect is over on this voice only when the baseline is
            # playing EXACTLY the same thing again, and stays that way -
            # not at the first silent frame.  The user: "some sounds are
            # just a part of the whole sound": several of these are two
            # or three bursts with gaps between them, and stopping at the
            # first gap cut them into fragments (NOTES 215).
            # A frame the baseline plays IDENTICALLY is the music, not
            # the effect: skip it entirely rather than render it.  The
            # first cut of this rendered eight such frames before giving
            # up, which appended a music note to every effect - the
            # user heard it as a phantom second tone on the coin.
            if b is not None and t == b:
                same += 1
                if same >= 8 and f > first + 4:
                    break                  # the music has the voice back
                continue
            same = 0
            # The effect is what this voice plays with the SAMPLE it
            # started on (the pitch may move and it may be re-keyed - the
            # coin is one sample struck twice, low then high).  A
            # different sample is the music coming back, whatever the
            # baseline says, and rendering on from there put a musical
            # tail on everything (NOTES 216).
            if f > first + 3 and srcn not in kit:
                break
            if envx == 0 and f > first + 2:
                quiet += 1
                if quiet >= 8:
                    break                  # a moment of nothing: it is over
            else:
                quiet = 0
            samp = samples.get(srcn)
            if samp is None:
                continue
            pcm, loop = samp
            if srcn != cur_srcn:            # a key-on: back to the start
                cur_srcn, phase = srcn, 0.0
            rate = (pitch & 0x3FFF) / 4096.0 * SR
            step = rate / OUT_SR
            n = int(OUT_SR / FPS)
            s0 = int((f - f0) / FPS * OUT_SR)
            # ENVX is only read once a frame, so the gain is walked to
            # the NEXT frame's value across this one: a 60 Hz staircase
            # on a four-frame decay is audible as a rattle
            nx = test.get((f + 1, v))
            e2 = nx[4] if nx else 0
            gl = (envx / 127.0) * (vl / 127.0)
            gr = (envx / 127.0) * (vr / 127.0)
            gl2 = (e2 / 127.0) * ((nx[0] if nx else vl) / 127.0)
            gr2 = (e2 / 127.0) * ((nx[1] if nx else vr) / 127.0)
            for i in range(n):
                j = int(phase)
                if j >= len(pcm):
                    if loop is None:
                        break
                    phase = loop + (phase - len(pcm))
                    j = int(phase)
                    if j >= len(pcm):
                        break
                k = s0 + i
                if 0 <= k < nout:
                    val = pcm[j] / 32768.0
                    a = i / float(n)
                    mix[k, 0] += val * (gl + (gl2 - gl) * a)
                    mix[k, 1] += val * (gr + (gr2 - gr) * a)
                phase += step
    peak = np.abs(mix).max()
    if peak < 1e-4:
        print('%s: silent' % os.path.basename(out_p)); return
    mix *= 22000.0 / peak
    # trim the silence at both ends, then a short fade
    env = np.abs(mix).max(axis=1)
    on = np.where(env > 22000 * 0.01)[0]
    if len(on):
        mix = mix[max(0, on[0] - 64): on[-1] + 256]
    fade = min(256, len(mix) // 8)
    if fade > 1:
        r = np.linspace(0, 1, fade)[:, None]
        mix[:fade] *= r; mix[-fade:] *= r[::-1]
    w = wave.open(out_p, 'wb')
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(OUT_SR)
    w.writeframes(mix.astype('<i2').tobytes()); w.close()
    print('%s: %.2f s from voice(s) %s, sample(s) %s' %
          (os.path.basename(out_p), len(mix) / OUT_SR,
           ','.join(str(v) for v in sorted(touched)),
           ','.join('$%02X' % s for s in sorted(srcns))))

main()
