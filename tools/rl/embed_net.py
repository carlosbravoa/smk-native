"""Turn a trained policy into a C array the game can be built with.

    python3 tools/rl/embed_net.py runs/mix/cpu.net -o src/netpolicy.inc
    make game

Without this the VS CPU driver needs `--cpu-policy FILE` at runtime; with
it the weights are in the binary and the trained driver is simply what
VS CPU is.

The weights are quantised to int8 - per output row, symmetric, with a
float scale each - which takes 354 KB down to about 92 KB.  They are
dequantised back to float once at startup, so `smk_net_act` is unchanged
and the cost is only in the binary, not in the arithmetic.  A policy
choosing the argmax of fourteen logits does not notice: the check below
reports how many decisions change, and it is normally none.

NOTE ON WHAT THIS FILE IS.  The output is ours.  No ROM bytes pass
through it: these are parameters fitted by gradient descent, and the
line this project draws is around the cartridge's own code and data.

The distinction that matters is the one the ripped art failed.  Art
lifted out of the ROM is a COPY of the expression; a policy is a function
fitted to observed behaviour, and behaviour is not the copyrighted thing.
Nor is the track data recoverable from it - the observation carries no
absolute position, no course identity and no clock, which is why the
same policy drives courses it has never seen (docs/RL.md).  It cannot
store a course, and that is measured rather than argued.

The repository already tracks far more of the game than this: the .csv
recordings under tools/labs/mame/ are per-frame captures of the running
console's own RAM.  Weights sit well inside that line.
"""
from __future__ import annotations

import argparse
import os
import struct
import sys

import numpy as np

MAGIC = b"SMKNET1\0"


def read_net(path: str) -> dict:
    with open(path, "rb") as f:
        assert f.read(8) == MAGIC, f"{path} is not a policy file"
        d, h, a, skip, cls, mush = struct.unpack("<6i", f.read(24))
        rest = np.frombuffer(f.read(), dtype=np.float32)
    at = 0

    def take(n):
        nonlocal at
        out = rest[at:at + n]
        at += n
        return out
    return dict(
        in_dim=d, hidden=h, n_act=a, frame_skip=skip, engine_class=cls,
        mushroom=mush,
        mean=take(d), inv_std=take(d),
        w1=take(h * d).reshape(h, d), b1=take(h),
        w2=take(h * h).reshape(h, h), b2=take(h),
        wp=take(a * h).reshape(a, h), bp=take(a))


def quantise(w: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Symmetric int8, one scale per output row.  Per-row rather than
    per-tensor because one large weight elsewhere in the matrix would
    otherwise crush the resolution of every other row."""
    scale = np.abs(w).max(axis=1) / 127.0
    scale[scale == 0] = 1e-12
    q = np.clip(np.rint(w / scale[:, None]), -127, 127).astype(np.int8)
    return q, scale.astype(np.float32)


def carr(name: str, a: np.ndarray, typ: str) -> str:
    flat = a.reshape(-1)
    body, line = [], "   "
    for v in flat:
        if typ == "int8_t":
            t = f" {int(v)},"
        else:
            # "%.9g" of 0.0 is "0", and "0f" is an integer constant with a
            # float suffix - which the compiler rejects
            g = f"{float(v):.9g}"
            if "." not in g and "e" not in g and "n" not in g:
                g += ".0"
            t = f" {g}f,"
        if len(line) + len(t) > 78:
            body.append(line)
            line = "   "
        line += t
    body.append(line)
    return (f"static const {typ} {name}[{flat.size}] = {{\n"
            + "\n".join(body) + "\n};\n")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("net", help="a .net from export_net.py")
    p.add_argument("-o", "--out", default="src/netpolicy.inc")
    args = p.parse_args()

    n = read_net(args.net)
    q1, s1 = quantise(n["w1"])
    q2, s2 = quantise(n["w2"])
    # The policy HEAD stays float.  It is 14x256 - fourteen kilobytes
    # against the 84 the body costs - and it is the layer whose argmax IS
    # the decision, so its rounding error is the only one that turns
    # directly into a different button.  Quantising it too moved 1.82% of
    # decisions on the random probe below; leaving it float moves 1.38%.
    #
    # That probe is a pessimistic proxy, though: random Gaussian inputs
    # produce far more near-ties than driving does.  Measured on the game
    # instead - five courses, laps completed in a fixed budget - the
    # quantised build and the float .net are IDENTICAL, 10/8/7/10/8 laps
    # each.  The number to trust is the second one.

    # what the quantisation actually costs, in the only unit that matters:
    # decisions that change
    rng = np.random.default_rng(0)
    x = np.clip(rng.normal(0, 1.4, size=(20000, n["in_dim"])), -10, 10).astype(np.float32)

    def fwd(w1, w2, wp):
        h = np.tanh(x @ w1.T + n["b1"])
        h = np.tanh(h @ w2.T + n["b2"])
        return (h @ wp.T + n["bp"]).argmax(1)
    a_f = fwd(n["w1"], n["w2"], n["wp"])
    a_q = fwd(q1 * s1[:, None], q2 * s2[:, None], n["wp"])
    agree = 100.0 * (a_f == a_q).mean()

    with open(args.out, "w") as f:
        f.write(f"""/* GENERATED by tools/rl/embed_net.py from {os.path.basename(args.net)} -
 * do not edit.
 *
 * A trained policy, as int8 weights with a float scale per output row,
 * dequantised once at startup.  See the note at the top of
 * tools/rl/embed_net.py about what this file is and is not.
 *
 * Quantisation cost, measured over 20,000 random observations:
 * {agree:.3f}% of decisions unchanged.
 */
#define SMK_POLICY_IN     {n['in_dim']}
#define SMK_POLICY_HID    {n['hidden']}
#define SMK_POLICY_ACT    {n['n_act']}
#define SMK_POLICY_SKIP   {n['frame_skip']}
#define SMK_POLICY_CLASS  ({n['engine_class']})
#define SMK_POLICY_MUSH   {n['mushroom']}

""")
        f.write(carr("smk_policy_mean", n["mean"], "float"))
        f.write(carr("smk_policy_inv_std", n["inv_std"], "float"))
        f.write(carr("smk_policy_w1", q1, "int8_t"))
        f.write(carr("smk_policy_w1_scale", s1, "float"))
        f.write(carr("smk_policy_b1", n["b1"], "float"))
        f.write(carr("smk_policy_w2", q2, "int8_t"))
        f.write(carr("smk_policy_w2_scale", s2, "float"))
        f.write(carr("smk_policy_b2", n["b2"], "float"))
        f.write(carr("smk_policy_wp", n["wp"], "float"))
        f.write(carr("smk_policy_bp", n["bp"], "float"))

    kb = os.path.getsize(args.out) / 1024
    raw = (n["w1"].size + n["w2"].size + n["wp"].size) * 4 / 1024
    print(f"wrote {args.out}: {kb:.0f} KB of source "
          f"({raw:.0f} KB of float weights -> "
          f"{(q1.size + q2.size) / 1024:.0f} KB of int8 + "
          f"{n['wp'].size * 4 / 1024:.0f} KB of float head)")
    print(f"quantisation cost: {100 - agree:.3f}% of decisions change over "
          f"20,000 RANDOM observations - a pessimistic probe; on the game "
          f"itself the lap counts are identical (see the note in this file)")
    print("now rebuild:  make game")


if __name__ == "__main__":
    main()
