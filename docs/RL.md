# Training a driver

The port can train a machine-learning agent to drive, because the port is
already a headless deterministic simulator of Super Mario Kart's physics.
There is no emulator anywhere in this: `src/env.c` steps the same C the
SDL build races.

```bash
make envtest                       # the environment's own gate + its speed
make envcheck                      # prove it is the same game as the window
make train TRACK=0                 # PPO on Mario Circuit 1
make watch TRACK=0                 # watch the result drive, in the real window
```

## What it runs on

No RL framework.  `tools/rl/train.py` is PPO in about 300 lines of
PyTorch, for the same reason the physics has no engine behind it: a
hidden `VecNormalize`, a default that quietly clips the reward, or a
`done` flag that conflates a finish with a time-out is exactly the class
of thing this tree refuses to have in the parts that matter.  The only
dependencies are `torch` and `numpy`; the environment is reached through
`ctypes`, so there is nothing to compile on the Python side either.

The network is a two-layer 256-unit MLP with a policy head and a value
head - about 150k parameters. On the machine this was developed on (an
RTX 5070) it sits at **21% GPU utilisation and 887 MB**, because 55
floats through two small matrices is not work. The GPU is a convenience,
not a requirement: `--device cpu` costs perhaps a third of the
throughput, since the bottleneck is neither the network nor the game but
the Python loop between them.

## What it costs

Measured on this machine, one core:

| | |
|---|---|
| bare physics step | ~2.1 M/s |
| through the environment, observation built | ~2.2 M game frames/s |
| agent decisions at frame-skip 4 | ~550 k/s |
| ...in units of the SNES | **~36,000x realtime** |
| PPO end to end, 256 envs, one GPU | ~120 k agent steps/s |

So the simulator is never the bottleneck; the learner is.  Three million
agent steps - a policy that finishes every lap on Mario Circuit 1 and
beats the scripted driver by 26 seconds over three laps - takes about
half a minute.

The state that must be copied to reset an episode is 604 bytes
(`smk_player` + `smk_kart`); a whole mutable world is 44 KB.  That is
what makes 256 environments in one process unremarkable.

## What is the ROM's, and what is ours

This is the distinction the rest of this repository is built on, so it
holds here too.

**The ROM's** - everything the agent is actually learning to drive: the
physics and its 16.16 / 8.8 / 65536-unit arithmetic, the acceleration
curves and target speeds, the surface behaviour table, the sector map and
its racing line, the direction field at `$7F:4000`, the lap rule at
`$808994`, the starting grid, Lakitu's rescue, the 336-frame countdown
and its launch.

**Ours** - the training harness, which the game has no equivalent of and
which makes no claim about how Super Mario Kart works:

- the 55-number observation
- the 13 actions
- the reward
- the episode's start and stop rules

### Ledger

| Shortcut | Why |
|---|---|
| The agent's first action is after the lights | The 336 countdown frames are run in full, so the state at GO is the game's own, but the agent does not choose the throttle during them - so it never learns the turbo start. `cfg.start_hold` sets that by hand. |
| Episodes default to 3 laps, not the game's 5 | An episode is a training unit. The evaluation and `export_pads.py` use 5. |
| No opponents by default | `mode = SMK_MODE_TT` is alone on the course. `SMK_MODE_GP` puts the seven AI karts on the grid, but they are not stepped by the environment yet - that is the next thing. |
| Items are one binary action | Action 12 uses whatever is held. The time trial's single mushroom is the only item in scope; the GP roulette is not. |
| `smk_player_rev_race` is not called | It is the engine NOTE, stepped in main.c's audio section. It feeds no physics, and the frame-exactness check confirms it. |

## The observation (55 floats)

Not pixels.  The renderer is the one expensive thing in this program, and
a camera view would cost more than the physics by two orders of magnitude
while telling the agent less than the course data already does.

| | count | what |
|---|---|---|
| kinematics | 10 | speed over the kart's own top speed; velocity split forward/lateral in the kart's frame; the slip angle as sin/cos of `$A2` against `$28`; turn rate; height; airborne; spinning; the drift pose |
| race context | 6 | coins, in a rescue, on off-road, on a hazard, holding an item, the fraction through the current sector |
| the racing line | 12 | the next four waypoints, each as (sin, cos) of its bearing relative to the heading, and its distance |
| position on the line | 1 | signed lateral offset from the line, in units of 128 px |
| the ROM's own advice | 2 | sin/cos of the direction field's heading for this cell against the kart's - what the game's own AI would steer |
| probes | 24 | twelve rangefinders over +-150 degrees, each giving the distance to the first wall and to the first edge of the road |

The layout is defined once in `observe()` in `src/env.c` and mirrored in
`OBS_LAYOUT` in `tools/rl/smkenv.py`.

## The actions (13)

The pad is a 16-bit word and a policy over all 65536 of them would be
absurd, so this is the subset a person's hands actually make: a pedal, a
steering direction, and whether the hop button is held - which is what a
drift *is*.  The environment derives the button EDGES itself, as main.c
does for a human, because `$C4`'s pressed word is what a hop reads.

```
0 coast          4 drift left     8 brake+left    12 accel+item
1 accel          5 drift right    9 brake+right
2 accel+left     6 hop           10 coast+left
3 accel+right    7 brake         11 coast+right
```

## The reward

```
+ w_progress * (distance moved along the racing line, in sectors)
- w_time     * frames elapsed
- w_wall     * wall contacts
- w_offroad  * frames off the road
- w_rescue   * times Lakitu had to fish the kart out
+ w_finish   * (how much of the budget was left) , on finishing
```

Two decisions worth stating, because both were arrived at by choosing
against the obvious thing:

**Progress is measured continuously, not by the ROM's progress word.**
The word (`$F8`, `(lap << 8) | sector`) is a monotonic watermark that
steps once a sector - roughly twice a second - and by construction it
*hides* going backwards.  Both properties are wrong for shaping.
`smk_progress_line` projects the kart onto the segment between its
sector's waypoint and the next, so the signal is smooth and signed.

**Speed is not rewarded.**  Rewarding speed directly teaches a kart to
hold the outside wall at full throttle, because the wall is fast and the
corner is not.  Speed is how you finish sooner, and finishing sooner is
already what the time penalty and the finish bonus pay for.

## How it is checked

Three gates, none of which involve looking at a graph and deciding it
looks like it is learning:

1. **`make envtest`** - the observation is finite and bounded, the same
   seed and actions replay exactly, and driving forward pays. Then the
   real one: `src/autopilot.c`, which has never seen the observation
   vector, must get round all 20 GP courses *through the environment*
   and collect a large positive return. A reward with the wrong sign, a
   progress measure that jumps at the finish line, or an episode that
   truncates on its own all fail here before any learning is attempted.

2. **`make envcheck`** - drive a race in the environment, export the
   inputs, replay them through the actual SDL binary, and compare the
   kart's position and speed on **every frame**. The bar is 100%, and it
   currently holds for 8,966 consecutive frames of a five-lap race. Two
   callers of the same physics can drift apart silently; this is what
   stops that.

3. **the scripted driver as the baseline** - `train.py` prints
   `src/autopilot.c`'s time on the same courses under the same episode
   rules before it starts, and the evaluation is reported as a delta
   against it. A policy that cannot beat the script has not done
   anything.

## Watching it

A lap time printed by a trainer is not evidence.  A policy that has found
a hole in the reward - cutting a corner the sector map does not notice,
riding a wall that happens to be fast - prints a good number too.  So the
training is built to be watched, not only read.

**While it is still running.**  `make watch` reads the checkpoint the
trainer is writing, drives a five-lap time trial with whatever the policy
can do at that moment, and opens the game with it.  It only reads, so it
is safe to run against a live training directory as often as you like:

```bash
make watch RUN=runs/gp                  # the course the run is watching
make watch RUN=runs/gp TRACK=19         # any course, trained on or not
make watch-time RUN=runs/gp TRACK=19    # just the lap time, no window
```

**A record of the whole run.**  Every evaluation also drops a
`latest.pads` in the run directory and keeps a numbered copy
(`watch_u00200.pads`, `watch_u00400.pads`, ...), so the run can be played
back later, update by update, and the driving compared with itself:

```bash
./build-native/smk --pads runs/gp/watch_u00200.pads   # early
./build-native/smk --pads runs/gp/latest.pads         # now
```

`--no-watch` turns it off; it costs one five-lap roll-out per evaluation,
which is a few hundred milliseconds.

**A finished policy.**

```bash
python3 tools/rl/export_pads.py runs/gp/policy.pt --track 0 -o run.pads
./build-native/smk --pads run.pads
```

`--pads` reads one action index a game frame and decodes it through the
same table `src/env.c` presses, so what appears on screen is the policy
and not a second interpretation of the action set.  Add `--fast` for a
headless run; without it the fixed timestep follows the wall clock and a
headless binary at 13,000 fps barely ticks the simulation at all.

## Where to take it next

- **The other seven karts.** `smk_racer_step` is already in the library
  and already drives the ROM's racing lines; a GP-mode environment needs
  them stepped in the loop and the rank added to the observation.
- **Generalisation across courses.** `--tracks gp` already trains one
  policy on all twenty at once, with the batch spread across them. Hold
  out four and see whether it drives a course it has never seen.
- **The turbo start.** `cfg.start_hold` is a knob today. Making the
  countdown part of the episode makes it a learned skill.
- **Self-play.** Two policies on one grid, which the two-player path
  already has most of the machinery for.
