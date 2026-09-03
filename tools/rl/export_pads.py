"""Run a trained policy and write its inputs out for the real game to play.

    python3 tools/rl/export_pads.py runs/mc1/policy.pt --track 0 -o mc1.pads
    ./build-native/smk --pads mc1.pads

The file is one action index a game frame, with a header line naming the
race, and `--pads` decodes it through the same table src/env.c presses -
so what appears on screen is the policy, not a second interpretation of
the action set.

This exists because a lap time printed by a trainer is not evidence.  A
policy that has found a hole in the reward - cutting a corner the sector
map does not notice, or riding a wall that happens to be fast - will
still print a good number, and the only way to catch that is to watch it.
"""
from __future__ import annotations

import argparse

import numpy as np
import torch
from torch.distributions import Categorical

from smkenv import EnvCfg, SMKVecEnv, MODE_TT, OBS_DIM, frames_to_time, track_name
from train import Policy, RunningNorm


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("policy", nargs="?", default="",
                   help="a checkpoint from train.py; omit for the scripted driver")
    p.add_argument("--track", type=int, default=0)
    p.add_argument("--character", type=int, default=0)
    p.add_argument("--engine-class", type=int, default=1, dest="engine_class")
    p.add_argument("--laps", type=int, default=5, help="the game's own race is 5")
    p.add_argument("--no-mushroom", dest="mushroom", action="store_false",
                   help="the shell hands a time trial one; this takes it away")
    p.set_defaults(mushroom=True)
    p.add_argument("--frame-skip", type=int, default=4, dest="frame_skip")
    p.add_argument("--max-frames", type=int, default=18000, dest="max_frames")
    p.add_argument("--sample", action="store_true", help="sample instead of argmax")
    p.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    p.add_argument("-o", "--out", default="policy.pads")
    args = p.parse_args()

    cfg = EnvCfg(track=args.track, character=args.character,
                 engine_class=args.engine_class, mode=MODE_TT, laps=args.laps,
                 frame_skip=args.frame_skip, max_frames=args.max_frames,
                 stall_frames=0, mushroom=int(args.mushroom))
    env = SMKVecEnv([cfg])
    obs = env.reset()

    policy = norm = None
    if args.policy:
        device = torch.device(args.device)
        ck = torch.load(args.policy, map_location=device, weights_only=False)
        policy = Policy(hidden=ck["args"].get("hidden", 256)).to(device)
        policy.load_state_dict(ck["policy"])
        policy.eval()
        norm = RunningNorm(OBS_DIM)
        norm.load(ck["norm"])

    acts, finish = [], None
    for _ in range(args.max_frames // args.frame_skip + 2):
        if policy is None:
            a = int(env.autopilot_actions()[0])
        else:
            with torch.no_grad():
                logits, _ = policy(torch.as_tensor(norm(obs), device=device))
                a = int(Categorical(logits=logits).sample()[0] if args.sample
                        else logits.argmax(-1)[0])
        acts.extend([a] * args.frame_skip)          # one entry a GAME frame
        obs, rew, done, trunc, info = env.step(np.array([a], dtype=np.int32))
        if done[0]:
            finish = float(info[0][SMKVecEnv.INFO_FINISH_FRAME])
            break
        if trunc[0]:
            break
    env.close()

    with open(args.out, "w") as f:
        f.write(f"# track {args.track} character {args.character} "
                f"class {args.engine_class} mode {MODE_TT}\n")
        f.write("\n".join(str(a) for a in acts) + "\n")

    who = args.policy or "the scripted driver"
    if finish is not None:
        print(f"{who} on {track_name(args.track)}: "
              f"{args.laps} laps in {frames_to_time(finish)}")
    else:
        print(f"{who} on {track_name(args.track)}: did not finish {args.laps} laps")
    print(f"wrote {args.out} ({len(acts)} frames)\n"
          f"watch it:  ./build-native/smk --pads {args.out}")


if __name__ == "__main__":
    main()
