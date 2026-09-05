#!/usr/bin/env python3
"""The spin's own sample (NOTES 293): SRCN $00 out of a sound-CPU snapshot,
written RAW at 32000 Hz to rom/sfx/spin00.wav, loop point in the log.

    tools/labs/spinsample.py snapshot.spc [outdir]

The snapshot comes from tools/labs/mame/spcdump.lua at any race frame
(SPC_FRAME=2245 SPC_OUT=tmp/snap replay.sh spin1 spcdump.lua 40)."""
import sys, os, wave, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brr
path = sys.argv[1]; outdir = sys.argv[2] if len(sys.argv) > 2 else 'rom/sfx'
pcm, li = brr.load_samples(path, want=[0])[0]
os.makedirs(outdir, exist_ok=True)
out = os.path.join(outdir, 'spin00.wav')
w = wave.open(out, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(32000)
w.writeframes(struct.pack('<%dh' % len(pcm), *[max(-32768, min(32767, x)) for x in pcm])); w.close()
print('%s: %d samples, loop from %s' % (out, len(pcm), li))
