#!/usr/bin/env python3
"""Render a SUSTAINED voice - one the driver keys once and leaves
looping - as a seamless loop file.

The item roulette is exactly that (NOTES 220): voice 5 keys sample $0D
at pitch $042F when the box is hit and holds it until the roulette
stops, so the sound is the sample's own loop region played over and
over.  Rendered here as an integer number of loop turns, which SDL can
repeat with no seam.

    tools/labs/loopsound.py snap.spc SRCN PITCH TURNS out.wav
"""
import sys, os, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brr

spc, srcn, pitch, turns, out = sys.argv[1], int(sys.argv[2],0), int(sys.argv[3],0), int(sys.argv[4]), sys.argv[5]
OUT_SR = 32000
samples = brr.load_samples(spc, [srcn])
pcm, loop = samples[srcn]
if loop is None:
    loop = 0
rate = (pitch & 0x3FFF) / 4096.0 * 32000.0
step = rate / OUT_SR
period = len(pcm) - loop                      # samples in one turn of the loop
n = int(round(period * turns / step))
buf = np.zeros(n)
phase = float(loop)
for i in range(n):
    j = int(phase)
    if j >= len(pcm):
        phase = loop + (phase - len(pcm)); j = int(phase)
    buf[i] = pcm[j] / 32768.0
    phase += step
# LOOP_RAW=1 keeps the sample's OWN level.  Normalising every file to
# one peak is what destroyed the game's balance the first time round: a
# surface hiss is quiet in the ROM because the ROM means it to be.
if os.environ.get('LOOP_RAW'):
    buf *= 32768.0
else:
    peak = np.abs(buf).max() or 1.0
    buf *= 20000.0 / peak
st = np.stack([buf, buf], axis=1)
w = wave.open(out, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(OUT_SR)
w.writeframes(st.astype('<i2').tobytes()); w.close()
print('%s: %d turns of SRCN $%02X at pitch $%04X (%.0f Hz), %.3f s, loops %.1f times a second'
      % (os.path.basename(out), turns, srcn, pitch, rate, n / OUT_SR, rate / period))
