"""The network and the observation normaliser, shared by every tool here.

They live in their own module because train.py, export_pads.py and
watch.py all need them and none of those should have to import the
trainer to get at a class definition.
"""
from __future__ import annotations

import numpy as np
import torch
import torch.nn as nn

from smkenv import N_ACTIONS, OBS_DIM


class Policy(nn.Module):
    """A small MLP.  The observation is 55 engineered numbers, not pixels,
    so there is nothing for a convolution to do and the whole network fits
    in a fraction of the time one batch of environment steps takes."""

    def __init__(self, obs_dim: int = OBS_DIM, n_act: int = N_ACTIONS, hidden: int = 256):
        super().__init__()
        self.body = nn.Sequential(
            nn.Linear(obs_dim, hidden), nn.Tanh(),
            nn.Linear(hidden, hidden), nn.Tanh(),
        )
        self.pi = nn.Linear(hidden, n_act)
        self.v = nn.Linear(hidden, 1)
        # orthogonal init with a small policy head: the standard recipe,
        # and it matters - a large initial logit spread makes the first
        # updates thrash
        for m in self.modules():
            if isinstance(m, nn.Linear):
                nn.init.orthogonal_(m.weight, np.sqrt(2))
                nn.init.zeros_(m.bias)
        nn.init.orthogonal_(self.pi.weight, 0.01)
        nn.init.orthogonal_(self.v.weight, 1.0)

    def forward(self, x):
        h = self.body(x)
        return self.pi(h), self.v(h).squeeze(-1)


class RunningNorm:
    """Welford mean/variance for the observation.

    Not optional.  The vector mixes distances in hundreds of pixels with
    sines in [-1,1]; without this the first layer spends its capacity on
    scale and the run looks like the algorithm is at fault.
    """

    def __init__(self, dim: int = OBS_DIM):
        self.mean = np.zeros(dim, dtype=np.float64)
        self.var = np.ones(dim, dtype=np.float64)
        self.count = 1e-4

    def update(self, x: np.ndarray) -> None:
        bm, bv, bc = x.mean(0), x.var(0), x.shape[0]
        d = bm - self.mean
        tot = self.count + bc
        self.mean += d * bc / tot
        m_a, m_b = self.var * self.count, bv * bc
        self.var = (m_a + m_b + d * d * self.count * bc / tot) / tot
        self.count = tot

    def __call__(self, x: np.ndarray) -> np.ndarray:
        return np.clip((x - self.mean) / np.sqrt(self.var + 1e-8), -10, 10).astype(np.float32)

    def state(self) -> dict:
        return {"mean": self.mean.tolist(), "var": self.var.tolist(), "count": self.count}

    def load(self, s: dict) -> None:
        self.mean = np.array(s["mean"])
        self.var = np.array(s["var"])
        self.count = s["count"]


def load_checkpoint(path: str, device) -> tuple[Policy, RunningNorm, dict]:
    """A checkpoint written by train.py: the weights, the normaliser and
    the arguments the run was started with."""
    ck = torch.load(path, map_location=device, weights_only=False)
    policy = Policy(hidden=ck["args"].get("hidden", 256)).to(device)
    policy.load_state_dict(ck["policy"])
    policy.eval()
    norm = RunningNorm(OBS_DIM)
    norm.load(ck["norm"])
    return policy, norm, ck["args"]
