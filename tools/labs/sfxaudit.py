#!/usr/bin/env python3
"""Check every captured effect against the span the CHIP actually gives it.

`$2B` was cut to a fifth of its length because `sfxrender.py` decides an
effect is over at the first sample it did not see in the first four
frames, and `$2B`'s second half - the pitch drop - starts at frame six
(NOTES 243).  The user caught that one by ear.  Nothing about it needed
an ear, so this looks for the rest of them by machine.

For every `tmp/vdump/<ID>.log` it finds the voice the poke keyed, walks
it until the baseline has been playing exactly the same thing for eight
frames (which is `sfxrender`'s own end-of-effect test), and reports:

  * the effect's TRUE span in frames and seconds
  * the samples it uses on the way
  * the length of `rom/sfx/<ID>.wav` as it stands
  * SHORT if the file is materially shorter than the span

A SHORT id with more than one sample is the `$2B` bug again, and the fix
is `SFX_KIT=<the samples>` on the re-render.

    tools/labs/sfxaudit.py [rom/sfx]
"""
import os, sys, glob, wave

FPS = 60.0988
VD = 'tmp/vdump'
BASE = 'tmp/vd_base.log'

def read(path):
    rows = {}
    for line in open(path):
        p = line.strip().split(',')
        if len(p) != 7 or not p[0].isdigit():
            continue
        rows[(int(p[0]), int(p[1]))] = (int(p[2]), int(p[3]), int(p[4], 16),
                                        int(p[5], 16), int(p[6]))
    return rows

def span(base, test):
    """-> (voice, first, last, samples) for the voice the poke keyed"""
    # the effect's voice is the one whose SAMPLE the poke changed
    best = None
    for (f, v), t in sorted(test.items()):
        b = base.get((f, v))
        if b is not None and t[3] != b[3] and t[4] > 0:
            if best is None or f < best[1]:
                best = (v, f)
    if best is None:
        return None
    v, first = best
    last, same, samples = first, 0, set()
    for f in range(first, max(fr for fr, vv in test if vv == v) + 1):
        t = test.get((f, v))
        if t is None:
            continue
        b = base.get((f, v))
        # The effect owns this frame only while the voice is playing a
        # SAMPLE the baseline is not.  Testing `t == b` instead counts the
        # music's own pitch drift as part of the effect and inflates every
        # span - which made half of these look truncated when they are not.
        if b is not None and t[3] == b[3]:
            same += 1
            if same >= 4 and f > first + 2:
                break
            continue
        same = 0
        # ...and only a frame that is SOUNDING counts towards the length.
        # A voice keeps its sample long after the driver has keyed it off,
        # so counting released frames made a 4-frame blip look like a
        # 58-frame sound.
        if t[4] > 0:
            samples.add(t[3])
            last = f
    return v, first, last, samples

def wav_len(path):
    try:
        w = wave.open(path)
        return w.getnframes() / float(w.getframerate())
    except Exception:
        return None

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else 'rom/sfx'
    base = read(BASE)
    rows = []
    for p in sorted(glob.glob(os.path.join(VD, '*.log'))):
        name = os.path.basename(p)[:-4]
        if not (len(name) == 2 and all(c in '0123456789ABCDEF' for c in name)):
            continue                     # only plain single ids
        s = span(base, read(p))
        if not s:
            continue
        v, first, last, samples = s
        frames = last - first + 1
        want = frames / FPS
        have = wav_len(os.path.join(outdir, name + '.wav'))
        rows.append((name, v, frames, want, have, sorted(samples)))
    rows.sort(key=lambda r: (r[4] or 0) / r[3] if r[3] else 1)
    print('%-4s %-3s %6s %7s %7s  %-6s %s' %
          ('id', 'v', 'frames', 'chip s', 'file s', 'ratio', 'samples'))
    for name, v, frames, want, have, samples in rows:
        if have is None:
            mark = 'NO FILE'
            ratio = ''
        else:
            r = have / want if want else 1.0
            ratio = '%.2f' % r
            mark = 'SHORT' if r < 0.75 and frames > 6 else ''
        print('$%-3s %-3d %6d %7.3f %7s  %-6s %s  %s'
              % (name, v, frames, want,
                 ('%.3f' % have) if have is not None else '-', ratio,
                 ' '.join('$%02X' % x for x in samples), mark))

main()
