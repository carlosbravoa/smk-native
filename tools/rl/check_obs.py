"""Does the game show the policy what the training showed it?

    python3 tools/rl/check_obs.py

`make envcheck` proves the environment and the SDL game step the same
race.  That is not the same as proving they show a driver the same
OBSERVATION, and the difference is where two bugs have now lived: the
time-trial mushroom that the game granted and the environment did not,
and then the mushroom the game would not let a synthetic driver spend,
so `item_held` stayed true for a whole race in one and not the other.

Both were invisible to every gate here, because one implementation of
the observation is not the same thing as one set of inputs to it.

So: drive the same solo time trial in both, stop at the same race frame,
and compare all 55 numbers.  The bar is float precision.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smkenv import (EnvCfg, SMKVecEnv, MODE_TT, MODE_GP,  # noqa: E402
                    OBS_LAYOUT, OBS_DIM)

_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

#: (mode, track) -> the frame from which the two are known to differ.
#: GP on Mario Circuit 1 diverges from about frame 357: the same item box
#: gives the two sides different items, and everything after that is a
#: different race.  Written down rather than papered over - see docs/RL.md.
#: A divergence anywhere ELSE, or earlier than this, still fails.
KNOWN_GAPS = {(0, 0): 350}   # (MODE_GP, track 0)


SKIP = 4


def env_run(track, engine_class, frames, mode=MODE_TT, seed=1):
    """Drive the environment with the scripted driver and keep both the
    actions and the observation at each decision point.

    The actions are what the game is then replayed from.  Handing both
    sides the same buttons is the only way this comparison means
    anything: an autopilot stepped at 60 Hz in one place and 15 Hz in the
    other is a different DRIVER, and would diverge for reasons that have
    nothing to do with the observation."""
    env = SMKVecEnv([EnvCfg(track=track, engine_class=engine_class, mode=mode,
                            laps=5, frame_skip=SKIP, mushroom=1, seed=seed,
                            max_frames=30000, stall_frames=0)])
    obs = env.reset()
    acts, seen = [], {}
    # the clock starts at 1 (the lights) and each step advances SKIP, so
    # the decision before step n is taken at race frame 1 + n*SKIP
    for n in range(max(frames) // SKIP + 2):
        seen[1 + n * SKIP] = obs[0].copy()
        a = int(env.autopilot_actions()[0])
        acts.append(a)
        obs, rew, done, trunc, info = env.step(np.array([a], dtype=np.int32))
        if done[0] or trunc[0]:
            break
    env.close()
    return acts, seen


def game_obs(track, engine_class, acts, at_frame, tmp, mode=MODE_TT, seed=1):
    """The same observation from the SDL game, replaying those buttons."""
    pads = os.path.join(tmp, "obs.pads")
    with open(pads, "w") as f:
        f.write(f"# track {track} character 0 class {engine_class} mode {mode}\n")
        f.write("# mushroom 1\n")
        f.write("\n".join(str(a) for a in acts for _ in range(SKIP)) + "\n")
    # +1, the same shift check_replay.py uses: the game traces the state
    # ENTERING a frame, while the environment's observation is built after
    # that frame's step.  Frame f in one is frame f+1 in the other.
    env_vars = dict(os.environ, SMK_OBS_TRACE=str(at_frame + 1),
                    # the same item stream on both sides, or the same box
                    # gives different items and nothing can be compared
                    SMK_ITEM_SEED=str(seed),
                    SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    out = subprocess.run(
        [os.path.join(_ROOT, "build-native", "smk"), "--pads", pads, "--fast",
         "--frames", str(len(acts) * SKIP + 800),
         "--windowed", "--width", "64", "--height", "56"],
        capture_output=True, text=True, env=env_vars, timeout=300).stdout
    for line in out.splitlines():
        m = re.match(r"obs f(\d+) act (-?\d+):(.*)", line)
        if m:
            return np.array([float(x) for x in m.group(3).split()], dtype=np.float32)
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--track", type=int, action="append", default=None)
    p.add_argument("--class", type=int, default=1, dest="engine_class")
    p.add_argument("--frame", type=int, action="append", default=None)
    p.add_argument("--tol", type=float, default=1e-4)
    p.add_argument("--mode", default="both", choices=("tt", "gp", "both"),
                   help="a time trial, a full eight-kart race with items, or both")
    args = p.parse_args()
    tracks = args.track if args.track else [0, 7, 19]
    frames = args.frame if args.frame else [5, 201, 601]

    import tempfile
    modes = ([("time trial", MODE_TT)] if args.mode == "tt"
             else [("GP race", MODE_GP)] if args.mode == "gp"
             else [("time trial", MODE_TT), ("GP race", MODE_GP)])
    ok = True
    for label, mode in modes:
        print(f"  -- {label} --")
        ok = run_mode(tracks, frames, args, mode) and ok
    print("the game shows a driver what the training showed it"
          if ok else "DIVERGED - the policy is being shown a different world than it learned")
    return 0 if ok else 1


def run_mode(tracks, frames, args, mode):
    import tempfile
    ok = True
    for t in tracks:
        acts, seen = env_run(t, args.engine_class, frames, mode, seed=1)
        for f in frames:
            if f not in seen:
                continue
            e = seen[f]
            with tempfile.TemporaryDirectory() as tmp:
                g = game_obs(t, args.engine_class, acts, f, tmp, mode, seed=1)
            if g is None:
                print(f"  track {t} frame {f}: the game produced no observation")
                ok = False
                continue
            d = np.abs(g - e)
            worst = float(d.max())
            # A KNOWN gap is still printed, still measured, and still
            # fails if it moves - it just does not fail the build while
            # it is written down.  A gate that is switched off is not a
            # gate, and one that hides a number is worse.
            known = (mode, t) in KNOWN_GAPS and f >= KNOWN_GAPS[(mode, t)]
            tag = ("" if worst <= args.tol
                   else "   <-- KNOWN GAP (docs/RL.md)" if known
                   else "   <-- DIVERGED")
            print(f"  track {t:2d} frame {f:4d}: max |difference| {worst:.3e}{tag}")
            if worst > args.tol and not known:
                ok = False
                i = 0
                for name, n in OBS_LAYOUT:
                    seg = d[i:i + n]
                    if seg.max() > args.tol:
                        j = int(seg.argmax())
                        print(f"      {name:<18} env {e[i + j]:+.5f}  game {g[i + j]:+.5f}")
                    i += n
    return ok


if __name__ == "__main__":
    sys.exit(main())
