#!/usr/bin/env python3
"""Cut a long .spc render into ONE clean loop (docs/SOUND.md).

The renders are not sample-periodic (echo and noise drift), so the loop
is found on the loudness envelope: a grid over (start, period) maximising
the correlation of [start..start+30s] with [start+T..], the start walked
back while the match holds, and the cut crossfaded 10 ms into its own
head so the whole-file loop is seamless.

    tools/labs/songcut.py long.wav out.wav [max_period_s]
"""
import sys, wave, array, math
src, out = sys.argv[1], sys.argv[2]
maxT = float(sys.argv[3]) if len(sys.argv) > 3 else 220.0
w = wave.open(src); fr = w.getframerate(); ch = w.getnchannels(); n = w.getnframes()
a = array.array('h', w.readframes(n)); w.close(); m = a[0::ch]
dt = 0.1; win = int(fr * dt)
env = [sum(abs(v) for v in m[i:i+win]) / win for i in range(0, len(m) - win, win)]
mu = sum(env) / len(env); e = [v - mu for v in env]
L30 = int(30 / dt)
def corr(o1, o2, l):
    x = e[o1:o1+l]; y = e[o2:o2+l]
    nx = math.sqrt(sum(v*v for v in x)); ny = math.sqrt(sum(v*v for v in y))
    return sum(x[i]*y[i] for i in range(l)) / (nx*ny + 1e-9)
best = (0, 0, 0)
for T in range(int(20/dt), int(maxT/dt), 2):
    for L in range(0, int(80/dt), 5):
        if L + T + L30 >= len(e): continue
        c = corr(L, L + T, L30)
        if c > best[0]: best = (c, L, T)
c, L, T = best
while L > 0 and corr(L - 1, L - 1 + T, L30) > c - 0.02: L -= 1
print(f"{src}: loop start {L*dt:.1f}s period {T*dt:.1f}s corr {c:.3f}")
lo = int(L * dt * fr) * ch; ln = int(T * dt * fr) * ch
seg = array.array('h', a[lo:lo+ln])
xf = int(0.010 * fr) * ch
for i in range(xf):
    t = i / xf
    seg[ln - xf + i] = int(seg[ln - xf + i] * (1 - t) + seg[i] * t)
o = wave.open(out, 'wb'); o.setnchannels(ch); o.setsampwidth(2); o.setframerate(fr)
o.writeframes(seg.tobytes()); o.close()
print(f"  {out}: {ln/ch/fr:.2f}s, crossfaded")
