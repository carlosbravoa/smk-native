"""Is it driving, or replaying a route?

    python3 tools/rl/robustness.py runs/gp2/policy.pt

A policy that has memorised a route has learned an open-loop sequence:
it works from a known start and comes apart the moment anything moves it
off that sequence.  A policy that has learned the track reacts, because
what it sees is what it steers by.

This knocks it about with the GAME'S own disruptions - the banana spin,
the shell tumble, the kart-to-kart bump, the three things that will
actually happen in a race it is not driving alone - at several rates, and
reports how often it still finishes and how much time it loses.

The reading is in the SHAPE of the curve, not any single row.  A route
replayer falls off a cliff at the first knock and never recovers.  A
driver degrades: it loses the seconds the spin cost it and carries on.
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from policy import load_checkpoint  # noqa: E402
from smkenv import (EnvCfg, SMKVecEnv, GP_TRACKS, MODE_TT,  # noqa: E402
                    frames_to_time, track_name)


@torch.no_grad()
def trial(policy, norm, device, tracks, disrupt, laps, engine_class,
          episodes=4, seed=7000):
    cfgs = []
    for t in tracks:
        for e in range(episodes):
            cfgs.append(EnvCfg(track=t, engine_class=engine_class, mode=MODE_TT,
                               laps=laps, max_frames=20000, stall_frames=0,
                               disrupt=disrupt, seed=seed + 97 * t + e))
    env = SMKVecEnv(cfgs)
    obs = env.reset()
    n = env.n
    fin = np.full(n, -1.0)
    knocks = np.zeros(n)
    live = np.ones(n, dtype=bool)
    for _ in range(20000 // 4 + 2):
        logits, _ = policy(torch.as_tensor(norm(obs), device=device))
        obs, rew, done, trunc, info = env.step(logits.argmax(-1).cpu().numpy())
        for i in np.nonzero(live & (done.astype(bool) | trunc.astype(bool)))[0]:
            if done[i]:
                fin[i] = info[i][SMKVecEnv.INFO_FINISH_FRAME]
            knocks[i] = info[i][SMKVecEnv.INFO_DISRUPTED]
            live[i] = False
        if not live.any():
            break
    env.close()
    got = fin[fin >= 0]
    return (len(got) / n, float(got.mean()) if len(got) else None, float(knocks.mean()))


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("policy")
    p.add_argument("--tracks", default="gp")
    p.add_argument("--laps", type=int, default=3)
    p.add_argument("--engine-class", type=int, default=1, dest="engine_class")
    p.add_argument("--episodes", type=int, default=4)
    p.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    args = p.parse_args()

    tracks = GP_TRACKS if args.tracks == "gp" else [int(t) for t in args.tracks.split(",")]
    device = torch.device(args.device)
    policy, norm, targs = load_checkpoint(args.policy, device)

    print(f"{args.policy} on {len(tracks)} courses, {args.episodes} episodes each, "
          f"{args.laps} laps\n")
    print(f"{'knock every':>12}  {'finishes':>9}  {'mean time':>10}  {'knocks/run':>10}   vs clean")
    base = None
    for disrupt in (0, 900, 450, 200, 100):
        rate, mean, knocks = trial(policy, norm, device, tracks, disrupt,
                                   args.laps, args.engine_class, args.episodes)
        if base is None and mean:
            base = mean
        label = "never" if disrupt == 0 else f"{disrupt / 60:.1f}s"
        delta = f"{(mean - base) / 60:+.1f}s" if (mean and base) else ""
        print(f"{label:>12}  {rate * 100:8.0f}%  "
              f"{frames_to_time(mean) if mean else 'n/a':>10}  {knocks:10.1f}   {delta}")


if __name__ == "__main__":
    main()
