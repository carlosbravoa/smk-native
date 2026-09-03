"""The native game as a vectorised RL environment.

A thin ctypes binding over ``src/env.c``.  There is no emulator here and
no rendering: the same C the SDL build races is stepped from an action,
about 2.4 million game frames a second on one core, which is roughly
39,000x the speed the SNES ran at.

The stepping is vectorised **inside C** on purpose.  At that rate a
per-environment FFI call would cost several times the simulation itself,
so one call hands over every action and gets back every observation.

    from smkenv import SMKVecEnv, EnvCfg

    env = SMKVecEnv([EnvCfg(track=t) for t in range(8)])
    obs = env.reset()
    obs, rew, done, trunc, info = env.step(actions)

Environments auto-reset: the observation returned with a terminal step is
already the next episode's first, and ``env.final_obs[i]`` holds the state
env *i* was actually left in.  A finish is a true terminal and its value
is zero; a truncation is not, and has to be bootstrapped from
``final_obs`` rather than from the fresh episode's opening state.
"""
from __future__ import annotations

import ctypes
import os
from dataclasses import dataclass

import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))

OBS_DIM = 55
N_ACTIONS = 13
INFO_DIM = 8

MODE_GP = 0
MODE_TT = 4

#: what each action presses.  Mirrors SMK_ENV_ACTIONS in src/env.c.
ACTION_NAMES = [
    "coast", "accel", "accel+left", "accel+right",
    "drift-left", "drift-right", "hop",
    "brake", "brake+left", "brake+right",
    "left", "right", "accel+item",
]

#: the observation's layout, for interpreting a policy rather than
#: guessing at it.  Mirrors observe() in src/env.c.
OBS_LAYOUT = [
    ("speed", 1), ("vel_forward", 1), ("vel_lateral", 1),
    ("slip_sin", 1), ("slip_cos", 1), ("turn_rate", 1), ("height", 1),
    ("airborne", 1), ("spinning", 1), ("drifting", 1),
    ("coins", 1), ("in_hazard", 1), ("on_offroad", 1), ("on_hazard", 1),
    ("item_held", 1), ("sector_fraction", 1),
    ("waypoints_ahead", 12),        # 4 x (sin, cos, distance)
    ("line_offset", 1),
    ("flow_sin", 1), ("flow_cos", 1),
    ("rays", 24),                   # 12 x (wall distance, road-edge distance)
]

_TRACK_NAMES = [
    "Mario Circuit 1", "Donut Plains 1", "Ghost Valley 1", "Bowser Castle 1",
    "Mario Circuit 2", "Choco Island 1", "Ghost Valley 2", "Donut Plains 2",
    "Bowser Castle 2", "Mario Circuit 3", "Koopa Beach 1", "Choco Island 2",
    "Vanilla Lake 1", "Bowser Castle 3", "Mario Circuit 4", "Donut Plains 3",
    "Koopa Beach 2", "Ghost Valley 3", "Vanilla Lake 2", "Rainbow Road",
    "Battle Course 1", "Battle Course 2", "Battle Course 3", "Battle Course 4",
]
GP_TRACKS = list(range(20))


def track_name(t: int) -> str:
    return _TRACK_NAMES[t] if 0 <= t < len(_TRACK_NAMES) else f"track {t}"


class _Cfg(ctypes.Structure):
    """Must match ``smk_env_cfg`` in include/smk.h field for field."""
    _fields_ = [
        ("track", ctypes.c_int), ("character", ctypes.c_int),
        ("engine_class", ctypes.c_int), ("mode", ctypes.c_int),
        ("laps", ctypes.c_int), ("frame_skip", ctypes.c_int),
        ("max_frames", ctypes.c_int), ("stall_frames", ctypes.c_int),
        ("mushroom", ctypes.c_int), ("countdown", ctypes.c_int),
        ("start_hold", ctypes.c_int), ("start_jitter", ctypes.c_int),
        ("disrupt", ctypes.c_int),
        ("seed", ctypes.c_uint32),
        ("w_progress", ctypes.c_float), ("w_time", ctypes.c_float),
        ("w_wall", ctypes.c_float), ("w_offroad", ctypes.c_float),
        ("w_rescue", ctypes.c_float), ("w_finish", ctypes.c_float),
    ]


class _State(ctypes.Structure):
    """Must match ``smk_env_state``."""
    _fields_ = [
        ("x", ctypes.c_float), ("y", ctypes.c_float), ("progress", ctypes.c_float),
        ("heading", ctypes.c_int), ("speed", ctypes.c_int), ("lap", ctypes.c_int),
        ("sector", ctypes.c_int), ("coins", ctypes.c_int), ("track", ctypes.c_int),
        ("frames", ctypes.c_long),
    ]


@dataclass
class EnvCfg:
    """One environment's configuration.

    The defaults are ``smk_env_cfg_default``'s, restated here so a reader
    can see them without opening the C.  Everything below the line is the
    reward, which is ours and not the game's - see docs/RL.md.
    """
    track: int = 0
    character: int = 0
    engine_class: int = 1          # 0 = 50cc, 1 = 100cc, 2 = 150cc
    mode: int = MODE_TT            # alone on the course
    laps: int = 3
    frame_skip: int = 4
    max_frames: int = 10800        # three minutes at 60 Hz
    stall_frames: int = 300
    mushroom: int = 1              # the shell hands a time trial one
    countdown: int = 1             # run the game's own 336-frame start
    start_hold: int = -1           # countdown frame to hold the throttle from
    start_jitter: int = 0
    disrupt: int = 0               # mean frames between a random knock
    seed: int = 1
    # --- the reward's weights ---
    w_progress: float = 1.0
    w_time: float = 0.002
    w_wall: float = 0.05
    w_offroad: float = 0.002
    w_rescue: float = 2.0
    w_finish: float = 20.0

    def to_c(self) -> _Cfg:
        # by name, so a field added to one side and not the other is a
        # loud TypeError rather than a silently shifted struct
        return _Cfg(**{name: getattr(self, name) for name, _ in _Cfg._fields_})


def _load_lib(path: str | None = None) -> ctypes.CDLL:
    candidates = [path] if path else [
        os.path.join(_ROOT, "build-native", "libsmkenv.so"),
        os.path.join(_ROOT, "build-native", "libsmkenv.dylib"),
    ]
    for c in candidates:
        if c and os.path.exists(c):
            return ctypes.CDLL(c)
    raise FileNotFoundError(
        "libsmkenv.so not found - run `make envlib` (or `make game`) first.\n"
        f"looked in: {', '.join(str(c) for c in candidates)}")


class SMKVecEnv:
    """N environments stepped together in one C call."""

    def __init__(self, cfgs: list[EnvCfg], rom: str | None = None,
                 lib: str | None = None):
        if not cfgs:
            raise ValueError("at least one environment is needed")
        self._c = _load_lib(lib)
        self._bind()
        self.cfgs = list(cfgs)
        self.n = len(cfgs)
        rom = rom or os.environ.get("SMK_ROM") or os.path.join(_ROOT, "rom", "smk_usa.sfc")
        if not os.path.exists(rom):
            raise FileNotFoundError(
                f"no ROM at {rom}.  The port reads the track data out of a Super "
                "Mario Kart (USA) ROM you supply; see rom/README.md.")
        arr = (_Cfg * self.n)(*[c.to_c() for c in cfgs])
        err = ctypes.create_string_buffer(256)
        self._b = self._c.smk_env_batch_create(rom.encode(), arr, self.n, err, 256)
        if not self._b:
            raise RuntimeError(err.value.decode())

        if self._c.smk_env_obs_dim() != OBS_DIM or self._c.smk_env_action_count() != N_ACTIONS:
            raise RuntimeError("smkenv.py and src/env.c disagree about the "
                               "observation width or the action count")

        self.obs = np.zeros((self.n, OBS_DIM), dtype=np.float32)
        self.rew = np.zeros(self.n, dtype=np.float32)
        self.done = np.zeros(self.n, dtype=np.uint8)
        self.trunc = np.zeros(self.n, dtype=np.uint8)
        self.info = np.zeros((self.n, INFO_DIM), dtype=np.float32)
        #: the state an episode was actually left in, written only for the
        #: envs that ended this step.  A truncated episode has to be
        #: bootstrapped from THIS, not from `obs`, which by then already
        #: holds the next episode's first state.
        self.final_obs = np.zeros((self.n, OBS_DIM), dtype=np.float32)
        self._act = np.zeros(self.n, dtype=np.int32)

    def _bind(self) -> None:
        c = self._c
        f32 = np.ctypeslib.ndpointer(np.float32, flags="C_CONTIGUOUS")
        u8 = np.ctypeslib.ndpointer(np.uint8, flags="C_CONTIGUOUS")
        i32 = np.ctypeslib.ndpointer(np.int32, flags="C_CONTIGUOUS")
        c.smk_env_batch_create.restype = ctypes.c_void_p
        c.smk_env_batch_create.argtypes = [ctypes.c_char_p, ctypes.POINTER(_Cfg),
                                           ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t]
        c.smk_env_batch_destroy.argtypes = [ctypes.c_void_p]
        c.smk_env_batch_size.argtypes = [ctypes.c_void_p]
        c.smk_env_batch_size.restype = ctypes.c_int
        c.smk_env_batch_reset.argtypes = [ctypes.c_void_p, f32]
        c.smk_env_batch_step.argtypes = [ctypes.c_void_p, i32, f32, f32, u8, u8,
                                         f32, f32]
        c.smk_env_batch_autopilot.argtypes = [ctypes.c_void_p, i32]
        c.smk_env_batch_state.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                          ctypes.POINTER(_State)]
        c.smk_env_obs_dim.restype = ctypes.c_int
        c.smk_env_action_count.restype = ctypes.c_int

    # -- the environment interface --------------------------------------
    def reset(self) -> np.ndarray:
        self._c.smk_env_batch_reset(self._b, self.obs)
        return self.obs

    def step(self, actions) -> tuple:
        np.copyto(self._act, np.asarray(actions, dtype=np.int32).reshape(self.n))
        self._c.smk_env_batch_step(self._b, self._act, self.obs, self.rew,
                                   self.done, self.trunc, self.info,
                                   self.final_obs)
        return self.obs, self.rew, self.done, self.trunc, self.info

    def autopilot_actions(self) -> np.ndarray:
        """What ``src/autopilot.c`` would press - the scripted baseline."""
        self._c.smk_env_batch_autopilot(self._b, self._act)
        return self._act

    def state(self, i: int) -> _State:
        st = _State()
        self._c.smk_env_batch_state(self._b, i, ctypes.byref(st))
        return st

    # ``info`` columns, by name
    INFO_LAP, INFO_FRAMES, INFO_PROGRESS, INFO_SPEED = 0, 1, 2, 3
    INFO_WALLS, INFO_RESCUES, INFO_FINISH_FRAME, INFO_DISRUPTED = 4, 5, 6, 7

    def close(self) -> None:
        if getattr(self, "_b", None):
            self._c.smk_env_batch_destroy(self._b)
            self._b = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


def frames_to_time(frames: float) -> str:
    """The game's own clock format: 1'01"50."""
    cs = int(frames) * 100 // 60
    return f"{cs // 6000}'{cs // 100 % 60:02d}\"{cs % 100:02d}"


if __name__ == "__main__":
    # a smoke test: the scripted driver, one lap of Mario Circuit 1
    env = SMKVecEnv([EnvCfg(track=0, laps=1)])
    env.reset()
    total = 0.0
    for _ in range(3000):
        obs, rew, done, trunc, info = env.step(env.autopilot_actions())
        total += float(rew[0])
        if done[0] or trunc[0]:
            print(f"{'finished' if done[0] else 'timed out'} "
                  f"in {frames_to_time(info[0][SMKVecEnv.INFO_FINISH_FRAME])}, "
                  f"return {total:.1f}")
            break
    env.close()
