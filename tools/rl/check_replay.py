"""Prove the environment and the SDL game are the same game.

    python3 tools/rl/check_replay.py [--track N] [--laps N]

The environment (src/env.c) and the race loop (src/main.c) are two
callers of the same physics, and nothing but discipline keeps them
stepping it in the same order.  If they drift, every lap time the
trainer prints is about a different game than the one on screen, and
nothing would say so - the numbers would just be wrong.

So: drive a race in the environment, export the inputs, replay them
through the real SDL binary with its own trace on, and compare the
kart's position and speed on every single frame.  The bar is 100%.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smkenv import EnvCfg, SMKVecEnv, MODE_TT, frames_to_time, track_name  # noqa: E402


def write_header(path, track, character, engine_class, mushroom, acts):
    """The same file export_pads.write_pads writes - the header has to
    describe the race, or the game replays a different one."""
    with open(path, "w") as f:
        f.write(f"# track {track} character {character} "
                f"class {engine_class} mode {MODE_TT}\n")
        f.write(f"# mushroom {int(bool(mushroom))}\n")
        f.write("\n".join(str(a) for a in acts) + "\n")

_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
_TRACE = re.compile(r"pads f(\d+) a(\d+) x(-?\d+) y(-?\d+) spd(-?\d+) lap(-?\d+)")


def run(track: int, laps: int, character: int, engine_class: int,
        driver=None, mushroom: bool = True) -> tuple[int, int, str]:
    """Returns (frames compared, frames identical, a one-line verdict).

    `driver` is None for src/autopilot.c, or (policy, norm, device) to
    check a trained policy instead - the actions go down the same path
    either way, so the scripted driver is the cheap default."""
    cfg = dict(track=track, character=character, engine_class=engine_class,
               mode=MODE_TT, laps=laps, max_frames=30000, stall_frames=0,
               mushroom=int(bool(mushroom)))

    env = SMKVecEnv([EnvCfg(frame_skip=1, **cfg)])
    obs = env.reset()
    acts, rows, finish = [], [], None
    for _ in range(30000):
        if driver is None:
            a = int(env.autopilot_actions()[0])
        else:
            import torch
            policy, norm, device = driver
            with torch.no_grad():
                logits, _ = policy(torch.as_tensor(norm(obs), device=device))
                a = int(logits.argmax(-1)[0])
        obs, rew, done, trunc, info = env.step(np.array([a], dtype=np.int32))
        st = env.state(0)
        acts.append(a)
        rows.append((st.frames, int(st.x), int(st.y), st.speed))
        if done[0]:
            finish = float(info[0][SMKVecEnv.INFO_FINISH_FRAME])
            break
        if trunc[0]:
            break
    env.close()

    with tempfile.TemporaryDirectory() as tmp:
        pads = os.path.join(tmp, "check.pads")
        write_header(pads, track, character, engine_class, mushroom, acts)

        # 2. the same inputs through the real game
        smk = os.path.join(_ROOT, "build-native", "smk")
        env_vars = dict(os.environ, SMK_PADS_TRACE="1",
                        SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        # --fast is required: without it the fixed timestep follows the
        # WALL clock, and a headless run at 13,000 fps barely ticks the
        # simulation at all
        out = subprocess.run(
            [smk, "--pads", pads, "--fast", "--frames", str(len(acts) + 2000),
             "--width", "64", "--height", "56", "--windowed"],
            capture_output=True, text=True, env=env_vars, timeout=600).stdout

    game = {}
    for line in out.splitlines():
        m = _TRACE.match(line)
        if m:
            g = [int(x) for x in m.groups()]
            game[g[0]] = (g[2], g[3], g[4])

    n = same = 0
    first = None
    for f, x, y, s in rows:
        # the game's trace is printed BEFORE that frame's step, so frame
        # f+1's reading is the state frame f left behind
        g = game.get(f + 1)
        if g is None:
            continue
        n += 1
        if g == (x, y, s):
            same += 1
        elif first is None:
            first = f
    verdict = (f"{track_name(track)}{' +mushroom' if mushroom else ' no mushroom'}: "
               f"{same}/{n} frames identical"
               + (f", first divergence at frame {first}" if first else "")
               + (f", {laps} laps in {frames_to_time(finish)}" if finish else ", did not finish"))
    return n, same, verdict


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--track", type=int, action="append", default=None)
    p.add_argument("--laps", type=int, default=2)
    p.add_argument("--character", type=int, default=0)
    p.add_argument("--engine-class", type=int, default=1, dest="engine_class")
    p.add_argument("--policy", default="",
                   help="check a trained policy instead of the scripted driver")
    args = p.parse_args()
    tracks = args.track if args.track else [0, 7, 19]

    driver = None
    if args.policy:
        import torch
        from policy import load_checkpoint
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        pol, norm, _ = load_checkpoint(args.policy, device)
        driver = (pol, norm, device)

    # BOTH mushroom settings.  The gate used to build its own config with
    # the mushroom on, which happens to be what the shell grants, so it
    # never exercised a run driven WITHOUT one - and a policy trained that
    # way pressed "use item" into a boost the environment never gave it.
    # Every action-12 press came apart from there.  The header carries the
    # answer now, and this is what proves the header is honoured.
    ok = True
    for t in tracks:
        for mush in (True, False):
            n, same, verdict = run(t, args.laps, args.character,
                                   args.engine_class, driver, mush)
            print("  " + verdict)
            if n == 0 or same != n:
                ok = False
    print("the environment and the SDL game step the same race"
          if ok else "DIVERGED - src/env.c and src/main.c are not running the same game")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
