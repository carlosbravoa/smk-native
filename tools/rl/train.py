"""PPO on the native Super Mario Kart port.

    python3 tools/rl/train.py --track 0 --steps 20000000
    python3 tools/rl/train.py --tracks gp --envs 256      # all 20 GP courses
    python3 tools/rl/train.py --eval runs/mc1/policy.pt --track 0

Why PPO and not something more sample-efficient: the environment runs at
about 2.4 million game frames a second on one core, so samples are very
nearly free and the usual reason to reach for an off-policy method is
gone.  What is left is robustness to a reward that is still being argued
with, and PPO is good at that.

The whole trainer is here on purpose - no RL framework - because the
interesting part is the environment and a hidden `VecNormalize` or a
default that silently clips the reward would be exactly the kind of thing
this repository refuses to have in the physics.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import time
from dataclasses import asdict

import numpy as np
import torch
import torch.nn as nn
from torch.distributions import Categorical

from smkenv import (EnvCfg, SMKVecEnv, GP_TRACKS, OBS_DIM, MODE_GP, MODE_TT,
                    frames_to_time, track_name)
from policy import Policy, RunningNorm, load_checkpoint
from export_pads import write_pads


# ---- the environments ----------------------------------------------------
def parse_tracks(spec: str, track: int) -> list[int]:
    if spec == "gp":
        return list(GP_TRACKS)
    if spec:
        return [int(t) for t in spec.split(",")]
    return [track]


def holdout_of(args) -> set[int]:
    return {int(t) for t in args.holdout.split(",")} if args.holdout else set()


def build_cfgs(args) -> list[EnvCfg]:
    tracks = [t for t in parse_tracks(args.tracks, args.track)
              if t not in holdout_of(args)]
    if not tracks:
        raise SystemExit("every track was held out - nothing to train on")
    classes = [int(c) for c in args.classes.split(",")] if args.classes \
        else [args.engine_class]

    # THREE situations, not two.
    #
    # A policy that only ever raced has 26 of its 81 inputs pinned at zero
    # in a time trial - the rank, the nearby karts, the item, the incoming
    # shell - and after normalisation that is a large constant offset over
    # a third of the vector, a combination it never saw once.  It brakes:
    # measured at 27% throttle in a time trial against 98% in a race.
    #
    # But two slices would not be enough either.  With only "race" and
    # "alone" the opponent features and the item features appear and vanish
    # TOGETHER, so the network can learn one switch instead of two
    # independent facts.  A third slice - opponents, empty boxes - breaks
    # the correlation and makes each group vary on its own.
    #
    #   nothing at all      a solo time trial
    #   players, no items   seven opponents, no boxes
    #   everything          the full race
    n_tt = max(min(int(round(args.tt_fraction * 100)), 100), 0)
    n_ni = max(min(int(round(args.noitem_fraction * 100)), 100 - n_tt), 0)
    if args.gp:
        modes = [MODE_TT] * n_tt + [MODE_GP] * (100 - n_tt)
        item_on = [True] * n_tt + [False] * n_ni + [True] * (100 - n_tt - n_ni)
    else:
        modes, item_on = [MODE_TT], [True]

    cfgs = []
    for i in range(args.envs):
        cfgs.append(EnvCfg(
            track=tracks[i % len(tracks)],
            character=args.character,
            # spread the classes across the batch so one policy drives all
            # of them, instead of one that is silently wrong on the others
            engine_class=classes[(i // max(len(tracks), 1)) % len(classes)],
            laps=args.laps,
            frame_skip=args.frame_skip,
            max_frames=args.max_frames,
            stall_frames=args.stall_frames,
            mode=modes[i % len(modes)],
            items=int(args.items) if item_on[i % len(item_on)] else 0,
            mushroom=int(args.mushroom),
            start_jitter=args.jitter,
            disrupt=args.disrupt,
            seed=args.seed + i,
        ))
    return cfgs


# ---- evaluation ----------------------------------------------------------
@torch.no_grad()
def evaluate(policy, norm, args, tracks, device, greedy=True, episodes=1):
    """Lap times, per track, against the scripted driver on the same run.

    The number that matters is not the return - it is whether the kart
    finishes and how long it took, which is the game's own scoreboard.
    """
    cfgs = [EnvCfg(track=t, character=args.character, engine_class=args.engine_class,
                   laps=args.laps, frame_skip=args.frame_skip,
                   max_frames=args.max_frames, stall_frames=0,
            mode=MODE_GP if args.gp else MODE_TT,
            items=int(args.items),
            mushroom=int(args.mushroom),
                   # a DIFFERENT seed per episode, or eight races are
                   # ONE race counted eight times: the seed drives the
                   # item roulette, so a fixed one makes every episode
                   # of a course byte-identical
                   seed=args.seed + 9000 + 131 * t + ep)
            for t in tracks for ep in range(episodes)]
    env = SMKVecEnv(cfgs)
    obs = env.reset()
    n = env.n
    fin = np.full(n, -1.0)
    rank = np.zeros(n)
    ret = np.zeros(n)
    live = np.ones(n, dtype=bool)
    budget = args.max_frames // args.frame_skip + 2
    for _ in range(budget):
        logits, _ = policy(torch.as_tensor(norm(obs), device=device))
        act = logits.argmax(-1) if greedy else Categorical(logits=logits).sample()
        obs, rew, done, trunc, info = env.step(act.cpu().numpy())
        ret += rew * live
        for i in range(n):
            if live[i] and done[i]:
                fin[i] = info[i][SMKVecEnv.INFO_FINISH_FRAME]
                rank[i] = info[i][SMKVecEnv.INFO_RANK]
                live[i] = False
            elif live[i] and trunc[i]:
                live[i] = False
        if not live.any():
            break
    env.close()
    out = {}
    for k, t in enumerate(tracks):
        sl = slice(k * episodes, (k + 1) * episodes)
        got = fin[sl][fin[sl] >= 0]
        pl = rank[sl][fin[sl] >= 0]
        out[t] = {"finished": int(len(got)), "of": episodes,
                  "frames": float(got.min()) if len(got) else None,
                  "place": float(pl.mean()) if len(pl) else None,
                  "return": float(ret[sl].mean())}
    return out


def autopilot_baseline(args, tracks):
    """The same evaluation, driven by src/autopilot.c.  It is what the
    policy has to beat before any of this has been worth doing."""
    cfgs = [EnvCfg(track=t, character=args.character, engine_class=args.engine_class,
                   laps=args.laps, frame_skip=args.frame_skip,
                   max_frames=args.max_frames, stall_frames=0,
            mode=MODE_GP if args.gp else MODE_TT,
            items=int(args.items),
            mushroom=int(args.mushroom), seed=args.seed + 9000 + t)
            for t in tracks]
    env = SMKVecEnv(cfgs)
    env.reset()
    n = env.n
    fin = np.full(n, -1.0)
    live = np.ones(n, dtype=bool)
    for _ in range(args.max_frames // args.frame_skip + 2):
        _, _, done, trunc, info = env.step(env.autopilot_actions())
        for i in range(n):
            if live[i] and done[i]:
                fin[i] = info[i][SMKVecEnv.INFO_FINISH_FRAME]; live[i] = False
            elif live[i] and trunc[i]:
                live[i] = False
        if not live.any():
            break
    env.close()
    return {t: (float(fin[k]) if fin[k] >= 0 else None) for k, t in enumerate(tracks)}


# ---- PPO -----------------------------------------------------------------
def train(args):
    device = torch.device(args.device)
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    cfgs = build_cfgs(args)
    tracks = sorted({c.track for c in cfgs})
    held = sorted(holdout_of(args))
    env = SMKVecEnv(cfgs)
    n = env.n

    policy = Policy(hidden=args.hidden).to(device)
    opt = torch.optim.Adam(policy.parameters(), lr=args.lr, eps=1e-5)
    norm = RunningNorm(OBS_DIM)

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "config.json"), "w") as f:
        json.dump({"args": vars(args), "env": asdict(cfgs[0]),
                   "tracks": tracks}, f, indent=2)

    base = autopilot_baseline(args, sorted(set(tracks) | set(held)))
    print("the scripted driver, on the same courses and the same episode rules:")
    for t in tracks:
        print(f"  {track_name(t):<18} "
              f"{frames_to_time(base[t]) if base[t] else 'did not finish'}")

    T = args.rollout
    obs_buf = np.zeros((T, n, OBS_DIM), dtype=np.float32)
    act_buf = np.zeros((T, n), dtype=np.int64)
    logp_buf = np.zeros((T, n), dtype=np.float32)
    rew_buf = np.zeros((T, n), dtype=np.float32)
    val_buf = np.zeros((T + 1, n), dtype=np.float32)
    # `done` ends the value bootstrap; `trunc` does NOT - a time-out is
    # not the end of the world, only the end of our patience, so its value
    # still has to be carried.  Conflating them is the single commonest
    # way to get a quietly wrong advantage.
    end_buf = np.zeros((T, n), dtype=np.float32)
    cut_buf = np.zeros((T, n), dtype=np.float32)
    # the value of the state a TRUNCATED episode was left in.  Without it
    # the bootstrap would use the fresh episode's opening state, which is
    # simply a different number.
    boot_buf = np.zeros((T, n), dtype=np.float32)

    raw = env.reset()
    norm.update(raw)
    obs = norm(raw)

    ep_ret = np.zeros(n)
    ep_len = np.zeros(n, dtype=np.int64)
    recent_ret, recent_fin, recent_len = [], [], []

    total = 0
    updates = args.steps // (T * n)
    t_start = time.time()
    for up in range(1, updates + 1):
        for t in range(T):
            with torch.no_grad():
                logits, v = policy(torch.as_tensor(obs, device=device))
                dist = Categorical(logits=logits)
                a = dist.sample()
                lp = dist.log_prob(a)
            obs_buf[t] = obs
            act_buf[t] = a.cpu().numpy()
            logp_buf[t] = lp.cpu().numpy()
            val_buf[t] = v.cpu().numpy()

            raw, rew, done, trunc, info = env.step(act_buf[t])
            rew_buf[t] = rew
            end_buf[t] = done
            cut_buf[t] = np.maximum(done, trunc)
            cut_only = np.nonzero(trunc & ~done)[0]
            if len(cut_only):
                with torch.no_grad():
                    _, fv = policy(torch.as_tensor(
                        norm(env.final_obs[cut_only]), device=device))
                boot_buf[t, cut_only] = fv.cpu().numpy()
            ep_ret += rew
            ep_len += 1
            for i in np.nonzero(np.maximum(done, trunc))[0]:
                recent_ret.append(ep_ret[i])
                recent_len.append(ep_len[i])
                recent_fin.append(1.0 if done[i] else 0.0)
                ep_ret[i] = 0.0
                ep_len[i] = 0
            norm.update(raw)
            obs = norm(raw)
            total += n

        with torch.no_grad():
            _, v = policy(torch.as_tensor(obs, device=device))
            val_buf[T] = v.cpu().numpy()

        # GAE.  Three cases, and conflating any two of them is the
        # commonest way to get a quietly wrong advantage:
        #   finished   value 0 after it, and the trace stops
        #   truncated  bootstrapped from the state it was LEFT in
        #              (boot_buf, not val_buf[t+1] - by then the env has
        #              already reset), and the trace stops
        #   otherwise  the ordinary one-step bootstrap
        adv = np.zeros((T, n), dtype=np.float32)
        last = np.zeros(n, dtype=np.float32)
        for t in reversed(range(T)):
            cut = cut_buf[t]
            nxt = np.where(cut > 0, boot_buf[t], val_buf[t + 1]) * (1.0 - end_buf[t])
            delta = rew_buf[t] + args.gamma * nxt - val_buf[t]
            last = delta + args.gamma * args.lam * (1.0 - cut) * last
            adv[t] = last
        ret = adv + val_buf[:T]

        b_obs = torch.as_tensor(obs_buf.reshape(-1, OBS_DIM), device=device)
        b_act = torch.as_tensor(act_buf.reshape(-1), device=device)
        b_lp = torch.as_tensor(logp_buf.reshape(-1), device=device)
        b_adv = torch.as_tensor(adv.reshape(-1), device=device)
        b_ret = torch.as_tensor(ret.reshape(-1), device=device)

        idx = np.arange(T * n)
        for _ in range(args.epochs):
            np.random.shuffle(idx)
            for s in range(0, len(idx), args.minibatch):
                mb = torch.as_tensor(idx[s:s + args.minibatch], device=device)
                logits, v = policy(b_obs[mb])
                dist = Categorical(logits=logits)
                lp = dist.log_prob(b_act[mb])
                ratio = (lp - b_lp[mb]).exp()
                a_mb = b_adv[mb]
                a_mb = (a_mb - a_mb.mean()) / (a_mb.std() + 1e-8)
                l1 = ratio * a_mb
                l2 = torch.clamp(ratio, 1 - args.clip, 1 + args.clip) * a_mb
                pi_loss = -torch.min(l1, l2).mean()
                v_loss = 0.5 * (v - b_ret[mb]).pow(2).mean()
                ent = dist.entropy().mean()
                loss = pi_loss + args.vf_coef * v_loss - args.ent_coef * ent
                opt.zero_grad(set_to_none=True)
                loss.backward()
                nn.utils.clip_grad_norm_(policy.parameters(), args.max_grad_norm)
                opt.step()

        if up % args.log_every == 0 or up == updates:
            sps = total / (time.time() - t_start)
            r = np.mean(recent_ret[-100:]) if recent_ret else float("nan")
            fr = np.mean(recent_fin[-100:]) if recent_fin else float("nan")
            ln = np.mean(recent_len[-100:]) if recent_len else float("nan")
            print(f"update {up:5d}/{updates}  steps {total:>10,}  "
                  f"{sps:>8,.0f}/s  return {r:8.2f}  finished {fr*100:5.1f}%  "
                  f"len {ln:6.0f}  entropy {ent.item():.3f}")

        if up % args.eval_every == 0 or up == updates:
            # The courses it never trained on are reported apart from the
            # ones it did.  That gap is the whole question of whether it
            # learned the game or learned twenty routes, and averaging
            # the two together would hide it.
            if held:
                hres = evaluate(policy, norm, args, held, device,
                                episodes=args.eval_episodes)
                hf = [hres[t]["frames"] for t in held]
                print(f"    HELD OUT ({len(held)} courses never trained on): "
                      f"{sum(x is not None for x in hf)}/{len(held)} finished"
                      + (f", mean {frames_to_time(np.mean([x for x in hf if x]))}"
                         if any(hf) else ""))
                for t in held:
                    got, b = hres[t]["frames"], base.get(t)
                    mark = f"   ({(got - b) / 60:+.2f}s vs the script)" if got and b else ""
                    pl = hres[t]["place"]
                    print(f"      {track_name(t):<18} "
                          f"{frames_to_time(got) if got else 'did not finish':<10}"
                          + (f"  P{int(pl)}" if args.gp and pl else "") + mark)
            res = evaluate(policy, norm, args, tracks, device,
                           episodes=args.eval_episodes)
            for t in tracks:
                got, b = res[t]["frames"], base[t]
                mark = ""
                if got and b:
                    mark = f"   ({'-' if got < b else '+'}{abs(got-b)/60.0:.2f}s vs the script)"
                pl = res[t]["place"]
                print(f"    {track_name(t):<18} "
                      f"{frames_to_time(got) if got else 'did not finish':<10}"
                      + (f"  P{int(pl)}" if args.gp and pl else "") + mark)
            torch.save({"policy": policy.state_dict(), "norm": norm.state(),
                        "args": vars(args)}, os.path.join(args.out, "policy.pt"))
            # Leave something to WATCH, not just a number to read.  Every
            # evaluation drops the current policy's inputs for one course,
            # so a five-lap run of whatever it can do right now is always
            # one command away while the training is still going:
            #     ./build-native/smk --pads runs/<name>/latest.pads
            if args.watch:
                wt = args.watch_track if args.watch_track >= 0 else tracks[0]
                f = write_pads(policy, norm, device,
                               os.path.join(args.out, "latest.pads"), wt,
                               character=args.character,
                               engine_class=args.engine_class, laps=5,
                               frame_skip=args.frame_skip,
                               mushroom=bool(args.mushroom))
                # and keep this one, so the whole run can be replayed later
                shutil.copyfile(os.path.join(args.out, "latest.pads"),
                                os.path.join(args.out, f"watch_u{up:05d}.pads"))
                print(f"    watch:  ./build-native/smk --pads "
                      f"{os.path.join(args.out, 'latest.pads')}   "
                      f"({track_name(wt)}, 5 laps, "
                      f"{frames_to_time(f) if f else 'did not finish'})")

    env.close()
    print(f"saved {os.path.join(args.out, 'policy.pt')}")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--track", type=int, default=0)
    p.add_argument("--tracks", type=str, default="",
                   help="'gp' for all 20, or a comma list; overrides --track")
    p.add_argument("--character", type=int, default=0)
    p.add_argument("--engine-class", type=int, default=1, dest="engine_class")
    p.add_argument("--laps", type=int, default=3)
    # ON by default, because the game's own time trial grants one and a
    # policy that trained without it drives a different race from the one
    # `smk --pads` will replay.
    p.add_argument("--no-mushroom", dest="mushroom", action="store_false")
    p.set_defaults(mushroom=True)
    p.add_argument("--frame-skip", type=int, default=4, dest="frame_skip")
    p.add_argument("--max-frames", type=int, default=10800, dest="max_frames")
    p.add_argument("--stall-frames", type=int, default=300, dest="stall_frames")
    p.add_argument("--jitter", type=int, default=0, help="px of start jitter")
    p.add_argument("--gp", action="store_true",
                   help="a full eight-kart race with items, not a time trial")
    p.add_argument("--noitem-fraction", type=float, default=0.2,
                   dest="noitem_fraction",
                   help="with --gp, the share racing with the boxes EMPTY - so "
                        "the opponent features and the item features are not "
                        "perfectly correlated and cannot be learned as one switch")
    p.add_argument("--tt-fraction", type=float, default=0.25, dest="tt_fraction",
                   help="with --gp, the share of environments running a solo "
                        "time trial so that mode stays in distribution")
    p.add_argument("--no-items", dest="items", action="store_false")
    p.set_defaults(items=True)
    p.add_argument("--holdout", default="",
                   help="courses to keep OUT of training and report separately - "
                        "the test of whether it learned the game or the routes")
    p.add_argument("--classes", default="",
                   help="engine classes to spread across the batch, e.g. 0,1,2")
    p.add_argument("--disrupt", type=int, default=0,
                   help="mean frames between a random knock (the game's own "
                        "banana spin, shell tumble or kart bump).  Forces "
                        "reacting rather than replaying a route.")
    p.add_argument("--envs", type=int, default=64)
    p.add_argument("--steps", type=int, default=20_000_000)
    p.add_argument("--rollout", type=int, default=128)
    p.add_argument("--minibatch", type=int, default=2048)
    p.add_argument("--epochs", type=int, default=4)
    p.add_argument("--lr", type=float, default=3e-4)
    p.add_argument("--gamma", type=float, default=0.995)
    p.add_argument("--lam", type=float, default=0.95)
    p.add_argument("--clip", type=float, default=0.2)
    p.add_argument("--ent-coef", type=float, default=0.01, dest="ent_coef")
    p.add_argument("--vf-coef", type=float, default=0.5, dest="vf_coef")
    p.add_argument("--max-grad-norm", type=float, default=0.5, dest="max_grad_norm")
    p.add_argument("--hidden", type=int, default=256)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    p.add_argument("--out", default="runs/smk")
    p.add_argument("--log-every", type=int, default=10, dest="log_every")
    p.add_argument("--eval-every", type=int, default=100, dest="eval_every")
    p.add_argument("--eval", type=str, default="", help="load a policy and evaluate it")
    # A RACE is far noisier than a time trial - one shell decides a place -
    # so a single episode per course says almost nothing about a GP policy.
    p.add_argument("--eval-episodes", type=int, default=1, dest="eval_episodes",
                   help="episodes per course at evaluation (raise it for --gp)")
    p.add_argument("--no-watch", dest="watch", action="store_false",
                   help="do not write a watchable .pads at each evaluation")
    p.set_defaults(watch=True)
    p.add_argument("--watch-track", type=int, default=-1, dest="watch_track",
                   help="which course the watchable run drives (default: the first)")
    args = p.parse_args()

    if args.eval:
        device = torch.device(args.device)
        ck = torch.load(args.eval, map_location=device, weights_only=False)
        policy = Policy(hidden=ck["args"].get("hidden", 256)).to(device)
        policy.load_state_dict(ck["policy"])
        norm = RunningNorm(OBS_DIM); norm.load(ck["norm"])
        tracks = GP_TRACKS if args.tracks == "gp" else \
            ([int(t) for t in args.tracks.split(",")] if args.tracks else [args.track])
        base = autopilot_baseline(args, tracks)
        res = evaluate(policy, norm, args, tracks, device,
                       episodes=args.eval_episodes)
        head = f"{'course':<18} {'best':<10} {'the script':<10}  delta"
        # A RACE is far noisier than a time trial - one red shell decides a
        # place - so the best lap alone says very little about a GP policy.
        # What it FINISHED and where is the number that means something.
        if args.gp:
            head += "      finished  mean place"
        print(head)
        pl_sum = pl_n = 0.0
        for t in tracks:
            got, b = res[t]["frames"], base[t]
            d = f"{(got - b) / 60.0:+.2f}s" if got and b else ""
            line = (f"{track_name(t):<18} "
                    f"{frames_to_time(got) if got else 'DNF':<10} "
                    f"{frames_to_time(b) if b else 'DNF':<10}  {d:<9}")
            if args.gp:
                pl = res[t]["place"]
                line += (f"  {res[t]['finished']}/{res[t]['of']:<7}"
                         + (f"  P{pl:.2f}" if pl else "  -"))
                if pl:
                    pl_sum += pl * res[t]["finished"]
                    pl_n += res[t]["finished"]
            print(line)
        if args.gp and pl_n:
            print(f"\nmean finishing place over {int(pl_n)} races: "
                  f"P{pl_sum / pl_n:.2f} of 8")
        return
    train(args)


if __name__ == "__main__":
    main()
