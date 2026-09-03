"""Watch a run's current policy drive, in the real game window.

    python3 tools/rl/watch.py runs/gp                # the newest checkpoint
    python3 tools/rl/watch.py runs/gp --track 19     # on Rainbow Road
    python3 tools/rl/watch.py runs/gp --headless     # just the time, no window

It reads the checkpoint a training run is writing, drives one five-lap
time trial with it, and hands the resulting inputs to the SDL binary.
Safe to run while the training is still going: it only reads.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from export_pads import write_pads  # noqa: E402
from policy import load_checkpoint  # noqa: E402
from smkenv import frames_to_time, track_name  # noqa: E402

_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("run", help="a training output directory, e.g. runs/gp")
    p.add_argument("--track", type=int, default=None,
                   help="default: the course the run's own watch file uses")
    p.add_argument("--laps", type=int, default=5)
    p.add_argument("--headless", action="store_true",
                   help="report the time without opening a window")
    p.add_argument("--windowed", action="store_true",
                   help="a window rather than the game's default fullscreen")
    p.add_argument("--sample", action="store_true")
    p.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    args = p.parse_args()

    ck = os.path.join(args.run, "policy.pt")
    if not os.path.exists(ck):
        sys.exit(f"no checkpoint at {ck} yet - the first evaluation writes it")

    device = torch.device(args.device)
    policy, norm, targs = load_checkpoint(ck, device)
    track = args.track
    if track is None:
        track = targs.get("watch_track", -1)
        if track is None or track < 0:
            track = targs.get("track", 0)

    pads = os.path.join(args.run, "watch_now.pads")
    finish = write_pads(policy, norm, device, pads, track,
                        character=targs.get("character", 0),
                        engine_class=targs.get("engine_class", 1),
                        laps=args.laps, frame_skip=targs.get("frame_skip", 4),
                        mushroom=bool(targs.get("mushroom", 1)),
                        sample=args.sample)
    print(f"{track_name(track)}, {args.laps} laps: "
          + (frames_to_time(finish) if finish else "did not finish"))
    if args.headless:
        return

    smk = os.path.join(_ROOT, "build-native", "smk")
    cmd = [smk, "--pads", pads] + (["--windowed"] if args.windowed else [])
    print("opening the game: " + " ".join(cmd))
    subprocess.run(cmd)


if __name__ == "__main__":
    main()
