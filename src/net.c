/* A trained policy, run inside the game.
 *
 * The network tools/rl/train.py produces is a two-layer MLP - 55 inputs,
 * two 256-wide tanh layers, and a head of 13 logits - which is 83,469
 * numbers and about 84,000 multiply-adds per decision.  At the trained
 * rate of one decision every four frames that is roughly 1.3 MFLOP a
 * second per driver, next to a software Mode 7 renderer doing hundreds
 * of millions.  So there is no reason to link a runtime for it: the
 * forward pass is thirty lines and lives here.
 *
 * The point of running it in the game at all is `--cpu-policy`, which
 * makes the VS CPU driver a trained policy instead of src/autopilot.c.
 * That driver is a full smk_player in its own grid slot and it reaches
 * the game the only way anything does - by pressing buttons, through
 * smk_player_step (src/main.c's load_race: "the difference is only who
 * presses the buttons").  It gets no privileged control over its kart,
 * which is the whole point: an opponent that cheats is not an opponent.
 *
 * The file is written by tools/rl/export_net.py and carries the
 * observation normaliser with the weights.  It has to: the vector mixes
 * pixel distances with sines, the network learned against normalised
 * inputs, and handing it raw ones produces a driver that looks broken
 * rather than one that looks untrained.
 */
#include "smk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NET_MAGIC "SMKNET1"

static float *rd(FILE *f, size_t n)
{
    float *p = malloc(n * sizeof *p);
    if (!p) return NULL;
    if (fread(p, sizeof *p, n, f) != n) { free(p); return NULL; }
    return p;
}

bool smk_net_load(smk_net *n, const char *path, char *err, size_t errn)
{
    memset(n, 0, sizeof *n);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errn, "cannot open %s", path); return false; }
    char magic[8] = { 0 };
    int32_t hdr[6];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, NET_MAGIC, 7) != 0
        || fread(hdr, sizeof(int32_t), 6, f) != 6) {
        snprintf(err, errn, "%s is not a policy file", path);
        fclose(f); return false;
    }
    n->in_dim = hdr[0]; n->hidden = hdr[1]; n->n_act = hdr[2];
    n->frame_skip = hdr[3] > 0 ? hdr[3] : 1;
    /* What it was TRAINED on.  A policy learned at 100cc driving a 50cc
     * kart is not a worse driver, it is a driver whose throttle and
     * steering timing belong to a different car - and it looks broken
     * rather than mismatched.  The file says, so the game can. */
    n->engine_class = hdr[4];
    n->mushroom = hdr[5];
    if (n->in_dim != SMK_ENV_OBS || n->n_act != SMK_ENV_ACTIONS_N) {
        /* the observation or the action set changed under the weights:
         * the numbers would still multiply, and the driving would be
         * nonsense.  Say so instead. */
        snprintf(err, errn,
                 "%s expects a %d-wide observation and %d actions; this build has %d and %d",
                 path, n->in_dim, n->n_act, SMK_ENV_OBS, SMK_ENV_ACTIONS_N);
        fclose(f); return false;
    }
    if (n->hidden <= 0 || n->hidden > 4096) {
        snprintf(err, errn, "%s: implausible hidden width %d", path, n->hidden);
        fclose(f); return false;
    }
    size_t d = (size_t)n->in_dim, h = (size_t)n->hidden, a = (size_t)n->n_act;
    n->mean = rd(f, d);  n->inv_std = rd(f, d);
    n->w1 = rd(f, h * d); n->b1 = rd(f, h);
    n->w2 = rd(f, h * h); n->b2 = rd(f, h);
    n->wp = rd(f, a * h); n->bp = rd(f, a);
    fclose(f);
    if (!n->mean || !n->inv_std || !n->w1 || !n->b1 || !n->w2 || !n->b2
        || !n->wp || !n->bp) {
        snprintf(err, errn, "%s is truncated", path);
        smk_net_free(n);
        return false;
    }
    n->h1 = malloc(h * sizeof *n->h1);
    n->h2 = malloc(h * sizeof *n->h2);
    if (!n->h1 || !n->h2) { snprintf(err, errn, "out of memory"); smk_net_free(n); return false; }
    n->ok = true;
    return true;
}

void smk_net_free(smk_net *n)
{
    free(n->mean); free(n->inv_std);
    free(n->w1); free(n->b1); free(n->w2); free(n->b2);
    free(n->wp); free(n->bp); free(n->h1); free(n->h2);
    memset(n, 0, sizeof *n);
}

/* out = tanh(W x + b), W stored row-major as [rows][cols] */
static void dense_tanh(const float *w, const float *b, const float *x,
                       int rows, int cols, float *out)
{
    for (int r = 0; r < rows; r++) {
        const float *wr = w + (size_t)r * cols;
        float s = b[r];
        for (int c = 0; c < cols; c++) s += wr[c] * x[c];
        out[r] = tanhf(s);
    }
}

int smk_net_act(smk_net *n, const float *obs)
{
    if (!n->ok) return 1;                       /* accelerate, and nothing else */
    int d = n->in_dim, h = n->hidden, a = n->n_act;
    /* the normaliser, exactly as tools/rl/policy.py applies it - including
     * the clip, which is not decoration: an observation the run never saw
     * (a kart somewhere the training never put one) would otherwise
     * arrive as a very large number and swamp the first layer */
    float x[SMK_ENV_OBS];
    for (int i = 0; i < d; i++) {
        float v = (obs[i] - n->mean[i]) * n->inv_std[i];
        x[i] = v < -10.0f ? -10.0f : v > 10.0f ? 10.0f : v;
    }
    dense_tanh(n->w1, n->b1, x, h, d, n->h1);
    dense_tanh(n->w2, n->b2, n->h1, h, h, n->h2);
    /* the head is linear, and only its argmax is wanted - the policy is
     * played greedily here, because a driver that samples its actions
     * twitches in a way that reads as a bug rather than as a driver */
    int best = 0;
    float bestv = -1e30f;
    for (int r = 0; r < a; r++) {
        const float *wr = n->wp + (size_t)r * h;
        float s = n->bp[r];
        for (int c = 0; c < h; c++) s += wr[c] * n->h2[c];
        if (s > bestv) { bestv = s; best = r; }
    }
    return best;
}

/* ---- the policy built INTO the game ------------------------------------
 *
 * src/netpolicy.inc is generated by tools/rl/embed_net.py and is not in
 * the tree by default - see the note at the top of that script about what
 * those numbers are.  When it is present the trained driver simply IS
 * what VS CPU means, with no flag; when it is not, the game builds and
 * plays exactly as before and src/autopilot.c drives.
 *
 * The weights arrive as int8 with a float scale per output row and are
 * expanded to float once, here, so the forward pass above is unchanged.
 * The saving is in the binary - 88 KB instead of 351 - not in the
 * arithmetic, which was never the problem.
 */
#if __has_include("netpolicy.inc")
#include "netpolicy.inc"
#define SMK_HAVE_POLICY 1

static float *expand(const int8_t *q, const float *scale, int rows, int cols)
{
    float *w = malloc((size_t)rows * cols * sizeof *w);
    if (!w) return NULL;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            w[(size_t)r * cols + c] = (float)q[(size_t)r * cols + c] * scale[r];
    return w;
}

static float *dup_f(const float *src, size_t n)
{
    float *w = malloc(n * sizeof *w);
    if (w) memcpy(w, src, n * sizeof *w);
    return w;
}

bool smk_net_builtin(smk_net *n, char *err, size_t errn)
{
    memset(n, 0, sizeof *n);
    if (SMK_POLICY_IN != SMK_ENV_OBS || SMK_POLICY_ACT != SMK_ENV_ACTIONS_N) {
        snprintf(err, errn,
                 "the built-in policy is %d wide with %d actions; this build "
                 "has %d and %d - regenerate src/netpolicy.inc",
                 SMK_POLICY_IN, SMK_POLICY_ACT, SMK_ENV_OBS, SMK_ENV_ACTIONS_N);
        return false;
    }
    n->in_dim = SMK_POLICY_IN; n->hidden = SMK_POLICY_HID;
    n->n_act = SMK_POLICY_ACT; n->frame_skip = SMK_POLICY_SKIP;
    n->engine_class = SMK_POLICY_CLASS; n->mushroom = SMK_POLICY_MUSH;
    size_t d = SMK_POLICY_IN, h = SMK_POLICY_HID, a = SMK_POLICY_ACT;
    n->mean    = dup_f(smk_policy_mean, d);
    n->inv_std = dup_f(smk_policy_inv_std, d);
    n->w1 = expand(smk_policy_w1, smk_policy_w1_scale, (int)h, (int)d);
    n->b1 = dup_f(smk_policy_b1, h);
    n->w2 = expand(smk_policy_w2, smk_policy_w2_scale, (int)h, (int)h);
    n->b2 = dup_f(smk_policy_b2, h);
    /* the head is stored in float: its argmax is the decision itself */
    n->wp = dup_f(smk_policy_wp, a * h);
    n->bp = dup_f(smk_policy_bp, a);
    n->h1 = malloc(h * sizeof *n->h1);
    n->h2 = malloc(h * sizeof *n->h2);
    if (!n->mean || !n->inv_std || !n->w1 || !n->b1 || !n->w2 || !n->b2
        || !n->wp || !n->bp || !n->h1 || !n->h2) {
        snprintf(err, errn, "out of memory expanding the built-in policy");
        smk_net_free(n);
        return false;
    }
    n->ok = true;
    return true;
}
#else
bool smk_net_builtin(smk_net *n, char *err, size_t errn)
{
    memset(n, 0, sizeof *n);
    snprintf(err, errn, "no policy is built in "
             "(run tools/rl/embed_net.py, then rebuild)");
    return false;
}
#endif

/* Which courses the trained driver is trusted on.
 *
 * The 20 GP courses, and no others.  The battle arenas (20-23) have no
 * racing line and no sector map to follow, so every feature the policy
 * steers by is meaningless there: pointed at one it sits still, measured.
 * Battle mode is not built either, but a shipped default must not depend
 * on that staying true, so the rule is here rather than assumed.
 */
bool smk_net_drives_track(int track)
{
    return track >= 0 && track < SMK_GP_TRACKS;
}
