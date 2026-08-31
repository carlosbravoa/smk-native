#!/usr/bin/env python3
"""Cut the game's own sound effects out of a pair of MAME captures.

tools/labs/mame/grab.sh records a recorded race twice - once poking a
list of sound ids into the game's own request queue, once poking nothing
(SFX_SILENT=1).  The emulation is deterministic, so subtracting the two
leaves the effect (plus whatever the driver's channel juggling did to
the music underneath it, which is ~14 dB down and stops when the effect
does).  Each id's slot is then trimmed to where the effect is actually
sounding, faded and normalised.

    tools/labs/sfxcut.py base.wav capture.wav ID,ID,... START GAP OUTDIR
"""
import sys, os, wave
import numpy as np

FPS = 60.0988          # the SNES frame rate the capture's frames count in

def read(path):
    w = wave.open(path)
    a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
    return a.reshape(-1, w.getnchannels()).astype(np.int32), w.getframerate()

def main():
    base_p, cap_p, ids_s, start, gap, outdir = sys.argv[1:7]
    ids = [int(t, 16) for t in ids_s.split(',')]
    start, gap = int(start), int(gap)
    b, sr = read(base_p)
    a, _ = read(cap_p)
    n = min(len(a), len(b))
    d = a[:n] - b[:n]
    os.makedirs(outdir, exist_ok=True)
    env_full = np.abs(d).max(axis=1)
    for i, sid in enumerate(ids):
        f0 = start + i * gap
        s0 = int(f0 / FPS * sr)
        s1 = min(n, int((f0 + gap) / FPS * sr))
        if s1 - s0 < sr // 10:
            print('%02X: no room' % sid); continue
        seg = d[s0:s1]
        env = env_full[s0:s1]
        # a 10 ms smoothing window, then the floor from the slot's tail
        k = sr // 100
        sm = np.convolve(env, np.ones(k) / k, mode='same')
        floor = np.median(sm[-sr // 2:]) if len(sm) > sr else 0.0
        peak = sm.max()
        if peak < max(400.0, floor * 2.0):
            print('%02X: silent (peak %.0f, floor %.0f)' % (sid, peak, floor)); continue
        thr = max(peak * 0.05, floor * 1.6, 150.0)
        on = np.where(sm > thr)[0]
        beg, end = int(on[0]), int(on[-1])
        # the effect ENDS at the first long quiet stretch after it starts
        quiet = sm[beg:end] < thr
        run = 0
        for j, q in enumerate(quiet):
            run = run + 1 if q else 0
            if run > sr // 4:                     # a quarter second of nothing
                end = beg + j - run
                break
        beg = max(0, beg - sr // 200)
        end = min(len(seg) - 1, end + sr // 100)
        out = seg[beg:end].astype(np.float64)
        if len(out) < sr // 50:
            print('%02X: too short' % sid); continue
        fade = min(sr // 100, len(out) // 4)
        ramp = np.linspace(0, 1, fade)[:, None]
        out[:fade] *= ramp
        out[-fade:] *= ramp[::-1]
        m = np.abs(out).max()
        if m > 0:
            out *= 22000.0 / m
        w = wave.open(os.path.join(outdir, '%02X.wav' % sid), 'wb')
        w.setnchannels(out.shape[1]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(out.astype('<i2').tobytes()); w.close()
        print('%02X: %5.2fs  peak %6.0f  floor %5.0f  (%.2f dB over)' %
              (sid, len(out) / sr, peak, floor,
               20 * np.log10(peak / floor) if floor > 0 else 99))

main()
