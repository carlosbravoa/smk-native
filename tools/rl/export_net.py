"""Write a trained policy out for the game to run.

    python3 tools/rl/export_net.py runs/gp2/policy.pt -o runs/gp2/cpu.net
    ./build-native/smk --players cpu --cpu-policy runs/gp2/cpu.net

The result is a flat float32 file that src/net.c reads.  It carries the
observation normaliser as well as the weights, and it has to: the vector
mixes pixel distances with sines, the network learned against normalised
inputs, and feeding it raw ones gives a driver that looks broken rather
than one that looks untrained.

Only the policy head is written.  The value head is a training artefact
and the game has no use for it.
"""
from __future__ import annotations

import argparse
import os
import struct

import numpy as np
import torch

from policy import load_checkpoint
from smkenv import N_ACTIONS, OBS_DIM

MAGIC = b"SMKNET1\0"


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("policy", help="a checkpoint from train.py")
    p.add_argument("-o", "--out", default="")
    args = p.parse_args()

    device = torch.device("cpu")
    net, norm, targs = load_checkpoint(args.policy, device)
    out = args.out or os.path.join(os.path.dirname(args.policy), "cpu.net")

    sd = net.state_dict()
    w1, b1 = sd["body.0.weight"].numpy(), sd["body.0.bias"].numpy()
    w2, b2 = sd["body.2.weight"].numpy(), sd["body.2.bias"].numpy()
    wp, bp = sd["pi.weight"].numpy(), sd["pi.bias"].numpy()
    hidden = w1.shape[0]
    assert w1.shape[1] == OBS_DIM and wp.shape[0] == N_ACTIONS, "shapes moved"

    inv_std = 1.0 / np.sqrt(norm.var + 1e-8)

    with open(out, "wb") as f:
        f.write(MAGIC)
        # the race it was trained for, so the game can say when it is
        # being asked to drive a different one
        f.write(struct.pack("<6i", OBS_DIM, hidden, N_ACTIONS,
                            int(targs.get("frame_skip", 4)),
                            # -1 means "any": a run that spread --classes
                            # across the batch drives all of them, and
                            # claiming one would make the game warn wrongly
                            (-1 if len(str(targs.get("classes", "")).split(",")) > 1
                             else int(targs.get("engine_class", 1))),
                            int(bool(targs.get("mushroom", True)))))
        for a in (norm.mean, inv_std, w1, b1, w2, b2, wp, bp):
            f.write(np.ascontiguousarray(a, dtype=np.float32).tobytes())

    n = sum(a.size for a in (w1, b1, w2, b2, wp, bp))
    print(f"wrote {out}: {n:,} parameters, {os.path.getsize(out) / 1024:.0f} KB, "
          f"hidden {hidden}, one decision every {targs.get('frame_skip', 4)} frames")
    multi = len(str(targs.get("classes", "")).split(",")) > 1
    cc = "every class" if multi else \
        f"{ {0: 50, 1: 100, 2: 150}.get(int(targs.get('engine_class', 1)), '?') }cc"
    held = targs.get("holdout", "")
    print(f"trained at {cc}"
          + (" with the mushroom" if targs.get("mushroom", True) else "")
          + (f", holding out courses {held}" if held else ""))
    print(f"use it:  ./build-native/smk --players cpu --cpu-policy {out}")


if __name__ == "__main__":
    main()
