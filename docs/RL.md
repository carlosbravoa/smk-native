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
   holds for 8,966 consecutive frames of a five-lap race. Two callers of
   the same physics can drift apart silently; this is what stops that.

   It checks **both** mushroom settings, and that is not fussiness. The
   first version built its own config with the mushroom on - which is
   what the shell grants - so a policy trained *without* one was never
   tested. Its "use item" presses became real boosts in the window and
   nothing in the environment, and the replay reached lap 1 where the
   environment had finished five. A gate that only exercises the working
   combination is not a gate. The `.pads` file now states the race it was
   driven in (`# mushroom N`) and `--pads` honours it, so the two cannot
   disagree out of band.

   The real-time path is checked too, not just the headless one: the same
   file played at the wall clock and under `--fast` gives **7,114 of
   7,114 identical ticks**, so what you watch in a window is the run that
   was measured.

3. **`make envcheck` also runs `check_obs.py`** - the same race in both,
   driven from one fixed input sequence so the trajectories are
   identical, stopping at matched frames and comparing all 55
   observation numbers. Proving the two step the same race is *not* the
   same as proving they show a driver the same world, and both of the
   bugs that reached the user lived in exactly that gap: the time-trial
   mushroom the game granted and the environment did not, and then the
   one the game would not let a synthetic driver spend, so `item_held`
   stayed true for a whole race on one side only. It immediately found a
   third - `smk_obj_ticks`, the moles' pop-up clock, which main.c sets
   from `fx_ticks` and the environment never set at all.

4. **the scripted driver as the baseline** - `train.py` prints
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

## The policy as the VS CPU driver

```bash
python3 tools/rl/export_net.py runs/gp2/policy.pt -o runs/gp2/cpu.net
./build-native/smk --players cpu --cpu-policy runs/gp2/cpu.net --class 1
```

The second player in `VS CPU` is already a full `smk_player` in its own
grid slot, and `src/main.c`'s own note says what matters: *"BOTH split
modes drive a real kart with the player physics.  The difference is only
who presses the buttons: a person, or our own autopilot."*  So the policy
substitutes at exactly that point.  It sees `smk_obs_build` - the same
observation, from the same implementation, that it was trained on - and
answers with an action index, which becomes the pad word a person's hands
would make.

**It gets no privileged control over its kart.**  That is not a courtesy,
it is the only way the comparison means anything: it accelerates,
brakes, steers and hops through `smk_player_step` like everything else
here, and it is subject to every rule a person is.

The decision is held for the `frame_skip` it trained at (saved in the
file).  Re-deciding every frame would run the policy four times faster
than the rate it learned at, which is a different driver.

There is no runtime to link.  The network is 83,469 parameters - 327 KB -
and a forward pass is two matrix-vector products and a tanh, about 84,000
multiply-adds per decision.  At one decision every four frames that is
roughly 1.3 MFLOP a second, next to a software Mode 7 renderer doing
hundreds of millions.  `src/net.c` is thirty lines of arithmetic.

### What it is and is not, today

Against `src/autopilot.c` in a real eight-kart race at the class it
trained on, over 6,000 ticks:

| | autopilot | policy |
|---|---|---|
| Mario Circuit 1 | lap 4 | lap 4 |
| Donut Plains 2 | lap 5 | **lap 6** |
| Rainbow Road | lap 4 | **lap 5** |

Which is a real result and a limited one.  The policy has **never seen
another kart**.  It was trained alone in a time trial, so it drives a
fast line and is blind to the seven karts around it, to the items it
picks up, and to the ones thrown at it.  It is a better *driver* than the
autopilot and not yet a better *racer*.

**The class must match.**  The acceleration curve and the target speeds
are the ROM's own per engine class, so a policy trained at 100cc driving
a 50cc kart has the throttle and steering timing of a different car - it
looks broken rather than mismatched.  The file records what it learned
on and the game warns when they differ.  The fix is to vary
`engine_class` across the training batch rather than to remember a flag;
the environment already takes a config per environment, so that is a
one-line change to `build_cfgs`.

## Did it learn the game, or twenty routes?

The question matters more than it sounds: a policy that has memorised a
route has learned an open-loop sequence, and the first bump from another
kart ends it.  There are two answers, one structural and one measured.

**Structurally it cannot memorise a route.**  Look at what is in the 55
numbers: velocity in the kart's own frame, the slip angle, the next four
waypoints as bearings *relative to heading*, the offset from the line,
the flow field *relative to heading*, and twelve rangefinders.  There is
no absolute position, no track identity, no sector index, no lap counter
and no clock.  `sector_fraction` is the fraction between two waypoints
and does not say which two.  The policy cannot encode "at twelve seconds,
turn left" because it has no clock, and cannot encode "on Rainbow Road,
do this" because it does not know which course it is on.  It is a
reactive function of local geometry or it is nothing.

**Measured, on four courses it never trained on.**  `--holdout 3,11,16,19`
keeps them out of the batch entirely and reports them apart from the rest
at every evaluation, because averaging the two hides the only number that
answers this:

| never trained on | policy | vs the scripted driver |
|---|---|---|
| Bowser Castle 1 | 1'41"96 | -25.20s |
| Choco Island 2 | 1'23"45 | -18.62s |
| Koopa Beach 2 | 0'46"66 | -17.57s |
| Rainbow Road | 1'03"66 | -6.97s |

4 of 4 finished, all four faster than the scripted driver, on courses the
network had never seen.  It is a competent driver on an unseen track, not
an expert one - the held-out times are slower than the same courses when
trained on, and that gap is the honest size of the generalisation.

**And measured under disruption.**  `--disrupt N` knocks the kart about
with the game's own hits - the banana spin, the shell tumble, the
kart-to-kart bump - at a mean of every N frames.
`tools/rl/robustness.py` sweeps the rate.  The reading is the SHAPE: a
route replayer falls off a cliff at the first knock, a driver degrades.

```
                     trained clean        trained with --disrupt 450
 knock every   finishes   vs clean      finishes   vs clean
       never        95%                     100%
       15.0s        92%     +12.7s           97%     +7.7s
        7.5s        88%     +25.9s           98%    +17.3s
        3.3s        72%     +50.1s           98%    +40.5s
        1.7s        67%    +144.3s           95%   +117.9s
```

Both degrade rather than collapse, which is the structural argument
showing up in behaviour.  Training *with* disruption is dramatically
better under it - 95% still finishing while being knocked 114 times in a
run - at the cost of a little outright pace.  It is also the cheapest
stand-in for opponents until the GP environment exists, because being
spun by a shell is being spun by a shell whether or not there is a kart
behind it.

## Where to take it next

- **Racing, rather than driving.** This is the big one, and the reason
  the VS CPU driver is only half a result. Everything needed is already
  in `libsmkcore` - `smk_racer_step`, `smk_item_box`/`smk_item_step`,
  `smk_proj_step`/`smk_proj_hit`, `smk_racer_hit`, kart-to-kart bumping
  with the ROM's weight table - and the environment calls none of it.
  The work is that ~420 lines of race orchestration in `src/main.c` have
  to become shared code with two callers, which is the same problem the
  lap rule had, and `make envcheck` is already the gate that polices it.
  Then the observation grows by the rank, the nearest few karts and the
  held item; the reward stops being lap time and becomes finishing
  position.
- **The engine classes.** Vary `engine_class` across the batch so one
  policy drives all three, instead of one that is silently wrong on two
  of them.
- **Generalisation across courses.** `--tracks gp` already trains one
  policy on all twenty at once, with the batch spread across them. Hold
  out four and see whether it drives a course it has never seen.
- **The turbo start.** `cfg.start_hold` is a knob today. Making the
  countdown part of the episode makes it a learned skill.
- **Self-play.** Two policies on one grid, which the two-player path
  already has most of the machinery for.
