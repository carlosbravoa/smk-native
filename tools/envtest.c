/* The RL environment's own gate, and its benchmark.
 *
 *   smk_envtest [rom]            the checks, then the throughput
 *   smk_envtest [rom] --bench    the throughput alone
 *
 * The sharpest check available is not "does it run" but "does the
 * SCRIPTED driver score well through it".  src/autopilot.c already gets
 * round most courses, and it never sees the observation vector - so if
 * the reward and the episode rules are wired correctly the autopilot
 * must finish laps and collect a large positive return.  A reward that
 * is signed wrongly, a progress measure that jumps at the finish line,
 * or an episode that truncates on its own will all show up here, before
 * any learning has been attempted.
 */
#include "smk.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

static int fails;
static void check(int cond, const char *what)
{
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static double now(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

#define N 8

int main(int argc, char **argv)
{
    const char *rom = "rom/smk_usa.sfc";
    bool bench_only = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bench")) bench_only = true;
        else rom = argv[i];
    }

    smk_env_cfg cfg[N];
    for (int i = 0; i < N; i++) {
        smk_env_cfg_default(&cfg[i]);
        cfg[i].track = i;            /* eight different courses at once */
        cfg[i].seed  = (uint32_t)(i + 1);
    }
    char err[256];
    smk_env_batch *b = smk_env_batch_create(rom, cfg, N, err, sizeof err);
    if (!b) { printf("skipped: %s\n", err); return 77; }

    static float obs[N * SMK_ENV_OBS], rew[N], info[N * SMK_ENV_INFO];
    static uint8_t done[N], trunc[N];
    static int32_t act[N];

    if (!bench_only) {
        printf("the environment\n");
        check(smk_env_obs_dim() == SMK_ENV_OBS, "the observation is the width the header declares");
        check(smk_env_action_count() == SMK_ENV_ACTIONS_N, "the action count matches the header");
        check(smk_env_batch_size(b) == N, "the batch is the size it was asked for");

        smk_env_batch_reset(b, obs);
        /* every observation must be a finite number in a sane range - an
         * unnormalised or NaN feature is the classic silent killer of a
         * training run, and it will not announce itself */
        int bad = 0; float amax = 0;
        for (int i = 0; i < N * SMK_ENV_OBS; i++) {
            if (!isfinite(obs[i])) bad++;
            if (fabsf(obs[i]) > amax) amax = fabsf(obs[i]);
        }
        check(bad == 0, "every observation is finite on reset");
        check(amax <= 8.0f, "and inside a sane range (max |x| <= 8)");

        /* The reset alone proves nothing: a kart standing still has zero
         * for every velocity, so a feature scaled wrongly by two orders
         * of magnitude passes.  That is exactly what happened - the
         * velocity split was divided by top/256 instead of top and ran
         * to 441 - so the range is now checked while DRIVING. */
        smk_env_batch_reset(b, obs);
        float dmax = 0; int dbad = 0, dwhere = -1;
        for (int s = 0; s < 1500; s++) {
            smk_env_batch_autopilot(b, act);
            smk_env_batch_step(b, act, obs, rew, done, trunc, info, NULL);
            for (int j = 0; j < N * SMK_ENV_OBS; j++) {
                if (!isfinite(obs[j])) dbad++;
                if (fabsf(obs[j]) > dmax) { dmax = fabsf(obs[j]); dwhere = j % SMK_ENV_OBS; }
            }
        }
        check(dbad == 0, "every observation stays finite through 1500 driven steps");
        if (dmax > 8.0f)
            printf("    the largest is %.1f, at index %d of the vector\n", dmax, dwhere);
        check(dmax <= 8.0f, "and every feature stays inside [-8, 8] while driving");

        /* the grid is where the game puts it, and it is on-course.
         * From a fresh reset - the driving above left a race in progress. */
        smk_env_batch_reset(b, obs);
        smk_env_state st;
        smk_env_batch_state(b, 0, &st);
        /* frame 1, not 0: the 336-frame countdown has already run, and
         * the race clock starts on the frame the lights go out - which is
         * what main.c's hud_race_frames does */
        check(st.lap == 0 && st.frames == 1, "an episode starts at the lights, on lap 0");
        check(st.sector >= 0, "and in a valid sector - the grid is on the course");

        /* the same actions from the same seed must give the same numbers:
         * a stochastic env would make every regression here meaningless */
        smk_env_batch_reset(b, obs);
        static float o1[N * SMK_ENV_OBS], r1[N];
        for (int s = 0; s < 200; s++) {
            for (int i = 0; i < N; i++) act[i] = (s / 7 + i) % SMK_ENV_ACTIONS_N;
            smk_env_batch_step(b, act, o1, r1, done, trunc, info, NULL);
        }
        float sum1 = 0; for (int i = 0; i < N * SMK_ENV_OBS; i++) sum1 += o1[i];
        smk_env_batch_reset(b, obs);
        static float o2[N * SMK_ENV_OBS], r2[N];
        for (int s = 0; s < 200; s++) {
            for (int i = 0; i < N; i++) act[i] = (s / 7 + i) % SMK_ENV_ACTIONS_N;
            smk_env_batch_step(b, act, o2, r2, done, trunc, info, NULL);
        }
        float sum2 = 0; for (int i = 0; i < N * SMK_ENV_OBS; i++) sum2 += o2[i];
        check(sum1 == sum2, "the same actions from the same seed replay exactly");

        /* driving forward pays and driving backward costs - the reward's
         * sign, checked rather than assumed */
        smk_env_batch_reset(b, obs);
        double fwd = 0;
        for (int s = 0; s < 60; s++) {
            for (int i = 0; i < N; i++) act[i] = 1;         /* accelerate */
            smk_env_batch_step(b, act, obs, rew, done, trunc, info, NULL);
            for (int i = 0; i < N; i++) fwd += rew[i];
        }
        check(fwd > 0.0, "holding accelerate off the grid earns a positive return");

        /* THE test: the scripted driver, through the env, on every GP
         * course, must complete the episode's laps */
        smk_env_cfg gp[20];
        for (int i = 0; i < 20; i++) {
            smk_env_cfg_default(&gp[i]);
            gp[i].track = i;
            gp[i].laps = 1;                 /* one lap is enough to prove it */
            gp[i].max_frames = 9000;
            gp[i].stall_frames = 900;
        }
        smk_env_batch *g = smk_env_batch_create(rom, gp, 20, err, sizeof err);
        static float gobs[20 * SMK_ENV_OBS], grew[20], ginfo[20 * SMK_ENV_INFO];
        static uint8_t gdone[20], gtrunc[20];
        static int32_t gact[20];
        static double ret[20]; static int fin[20], tru[20];
        smk_env_batch_reset(g, gobs);
        for (int s = 0; s < 9000 / 4; s++) {
            smk_env_batch_autopilot(g, gact);
            smk_env_batch_step(g, gact, gobs, grew, gdone, gtrunc, ginfo, NULL);
            for (int i = 0; i < 20; i++) {
                if (!fin[i] && !tru[i]) ret[i] += grew[i];
                if (gdone[i])  fin[i] = 1;
                if (gtrunc[i]) tru[i]++;
            }
        }
        int laps_done = 0; double worst = 1e9;
        for (int i = 0; i < 20; i++) {
            if (fin[i]) laps_done++;
            if (ret[i] < worst) worst = ret[i];
            printf("    track %2d: %-8s return %8.1f%s\n", i,
                   fin[i] ? "LAP" : "no lap", ret[i],
                   fin[i] ? "" : "   <-- the scripted driver did not get round");
        }
        printf("  the scripted driver finished on %d/20 GP courses\n", laps_done);
        /* The bar is TODAY'S number, so a regression shows.  It is 20/20
         * for ONE lap: src/autopilot.c is a test aid and the README's
         * "most courses, not all" is about full five-lap runs, where it
         * still loses some.  If this ever drops, the env broke - not the
         * driver, which no learning touches. */
        check(laps_done >= 20, "the scripted driver gets round all 20 GP courses");
        check(worst > -50.0, "and no course produces a runaway negative return");
        smk_env_batch_destroy(g);
    }

    /* ---- throughput ---- */
    smk_env_batch_reset(b, obs);
    const long STEPS = 200000;
    double t0 = now();
    for (long s = 0; s < STEPS; s++) {
        for (int i = 0; i < N; i++) act[i] = 1;
        smk_env_batch_step(b, act, obs, rew, done, trunc, info, NULL);
    }
    double dt = now() - t0;
    double agent_steps = (double)STEPS * N;
    double game_frames = agent_steps * cfg[0].frame_skip;
    printf("\n%.0f agent steps/s, %.0f game frames/s on one core "
           "(%.0f x realtime, frame_skip %d)\n",
           agent_steps / dt, game_frames / dt,
           game_frames / dt / 60.0988, cfg[0].frame_skip);

    smk_env_batch_destroy(b);
    if (!bench_only) printf("\n%s\n", fails ? "FAILED" : "the environment gate passes");
    return fails ? 1 : 0;
}
