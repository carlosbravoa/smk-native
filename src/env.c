/* The reinforcement-learning environment.
 *
 * This is the port driving ITSELF: no emulator, no SDL, no rendering.  It
 * steps exactly the code the game steps - smk_player_step, the pickup
 * collector, the object collision, the movers and the decoded lap rule -
 * from a pad word an agent chooses instead of one a person presses.  A
 * step here is the same 1/60.0988 s the SNES ran.
 *
 * Measured on this machine, one core: ~2.1 M physics steps a second bare,
 * ~1.4 M through this env with its observation built.  That is around
 * 23,000x realtime, so the learner is the bottleneck and never the game.
 *
 * WHAT IS THE ROM'S and what is OURS, since that distinction is the whole
 * discipline of this repository:
 *
 *   the ROM's   the physics, the surfaces, the sector map, the racing
 *               line, the flow field, the lap rule, the starting grid,
 *               the acceleration curves, the rescue - everything the
 *               agent is actually learning to drive.
 *   OURS        the observation vector, the action set, the reward, and
 *               the episode's start/stop rules.  A game has none of
 *               these; they are the training harness and nothing else.
 *               They are labelled here and in docs/RL.md, and they are
 *               not claims about how Super Mario Kart works.
 */
#include "smk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the action set (OURS) ---------------------------------------------
 *
 * Twelve driving actions plus the item.  The pad the game reads is a
 * 16-bit word and a policy over all 65536 of them would be absurd, so
 * this is the subset a person's hands actually produce: a pedal, a
 * steering direction, and whether the hop button is down (which is what
 * a drift IS - the hop is held through the turn).
 *
 * The hop needs an EDGE as well as a hold ($C4's pressed word, NOTES 106):
 * the env keeps the previous frame's pad and derives the press itself,
 * exactly as main.c does for a human. */
typedef struct { uint8_t accel, brake, left, right, hop, item; } smk_env_act;
static const smk_env_act SMK_ENV_ACTIONS[SMK_ENV_ACTIONS_N] = {
    /* 0 */ { 0,0,0,0,0,0 },   /* coast                    */
    /* 1 */ { 1,0,0,0,0,0 },   /* accelerate               */
    /* 2 */ { 1,0,1,0,0,0 },   /* accelerate + left        */
    /* 3 */ { 1,0,0,1,0,0 },   /* accelerate + right       */
    /* 4 */ { 1,0,1,0,1,0 },   /* drift left  (hop held)   */
    /* 5 */ { 1,0,0,1,1,0 },   /* drift right (hop held)   */
    /* 6 */ { 1,0,0,0,1,0 },   /* hop, straight            */
    /* 7 */ { 0,1,0,0,0,0 },   /* brake                    */
    /* 8 */ { 0,1,1,0,0,0 },   /* brake + left             */
    /* 9 */ { 0,1,0,1,0,0 },   /* brake + right            */
    /*10 */ { 0,0,1,0,0,0 },   /* coast + left             */
    /*11 */ { 0,0,0,1,0,0 },   /* coast + right            */
    /*12 */ { 1,0,0,0,0,1 },   /* accelerate + use item    */
};

static uint16_t pad_of(int action, uint16_t prev, uint16_t *pressed_out)
{
    const smk_env_act *a = &SMK_ENV_ACTIONS[action];
    uint16_t held = 0;
    if (a->accel) held |= 0x8000;            /* B: accelerate */
    if (a->brake) held |= 0x4000;            /* Y: brake      */
    if (a->left)  held |= 0x0200;
    if (a->right) held |= 0x0100;
    if (a->hop)   held |= 0x0020;            /* L / R: hop and drift */
    /* the fresh presses are the rising edges, as main.c composes them */
    *pressed_out = (uint16_t)(held & ~prev);
    return held;
}

/* ---- what the ground under a probe IS (OURS: a four-way fold) ----------
 *
 * The classes themselves are the ROM's, and the boundaries are the ones
 * src/effects.c decoded from $80D37A/$80D3B6/$80D3F3/$80D418: cls <= $1E
 * and $40..$52 are road, $54..$58 the ice roads, $5A..$5E the off-road
 * that throws dirt, $20..$24 water, and bit 7 is a wall (NOTES 119).
 * Folding them to four is ours - the agent does not need sixteen drag
 * types, it needs "can I drive here". */
enum { SMK_GND_ROAD = 0, SMK_GND_SLOW, SMK_GND_HAZARD, SMK_GND_WALL };
static int ground_of(uint8_t surf)
{
    if (smk_surface_solid(surf)) return SMK_GND_WALL;
    int cls = surf & 0x7E;
    if (cls >= 0x5A) return SMK_GND_SLOW;             /* $5A/$5C/$5E */
    if (cls >= 0x20 && cls <= 0x3E) return SMK_GND_HAZARD;  /* water, the drop */
    return SMK_GND_ROAD;
}

/* ---- one environment --------------------------------------------------- */
struct smk_env {
    smk_env_cfg cfg;
    const smk_rom *rom;          /* shared with the batch, never written  */
    smk_track   trk;             /* per-env: the pickups are consumed     */
    smk_course  crs;             /* per-env: the movers and spawn move    */
    smk_physics phys;
    smk_player  player;
    smk_kart    kart;
    smk_racer   me;              /* the lap rule's state, as main.c keeps it */
    smk_autopilot ap;            /* the scripted baseline, on request     */
    uint16_t    pad_prev;
    int         sector;          /* the last valid sector, = me.sector    */
    long        frames;          /* game frames since the lights          */
    int         steps;           /* agent steps                           */
    float       prog;            /* last continuous progress, in sectors  */
    float       prog_best;
    int         stall;           /* frames since prog_best last moved     */
    uint32_t    rng;
    /* what the last step did, for the info block */
    float       last_reward;
    int         wall_hits, offroad_frames, rescues;
    int         done, truncated;
    long        finish_frame;
};

static uint32_t xrand(uint32_t *s)
{ uint32_t x = *s ? *s : 0x2545F491u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- the observation (OURS) -------------------------------------------
 *
 * 55 floats, all roughly in [-1,1], and deliberately NOT pixels: the
 * renderer is the one expensive thing in this program and a camera view
 * would cost more than the physics by two orders of magnitude while
 * telling the agent less than the course data already says.
 *
 * The layout is documented in docs/RL.md and mirrored in tools/rl/smkenv.py.
 */
#define OBS_RAY_MAX   192.0f     /* how far a probe looks, in world px    */
#define OBS_RAY_STEP  8          /* and in what increment - one tile      */
#define OBS_WP_AHEAD  4          /* waypoints of racing line in the vector */

static float wrap_px(float d)
{
    if (d >  SMK_WORLD_PX / 2) d -= SMK_WORLD_PX;
    if (d < -SMK_WORLD_PX / 2) d += SMK_WORLD_PX;
    return d;
}
/* a signed angle difference in turns, mapped to [-1,1) */
static float ang_delta(uint16_t a, uint16_t b)
{ return (float)(int16_t)(uint16_t)(a - b) / 32768.0f; }

static void observe(const smk_env *e, float *o)
{
    const smk_kart *k = &e->kart;
    const smk_player *p = &e->player;
    int i = 0;
    int px = smk_kart_px(k->x), py = smk_kart_px(k->y);
    float top = p->target > 0 ? (float)p->target : 1024.0f;

    /* --- kinematics (10) --- */
    float spd = (float)k->speed;
    o[i++] = spd / top;
    /* the velocity in the KART's frame: how much is forward and how much
     * is sideways.  The slide is the whole game and this is where it shows */
    {
        /* vx/vy ($22/$24) are 8.8 px per frame, the SAME units as speed
         * ($EA), so both scale by the kart's own top speed.  They were
         * divided by top/256 in the first draft, which put a hundred-odd
         * into a vector everything else keeps inside [-1,1]. */
        float s, c, vx = (float)k->vx, vy = (float)k->vy;
        float th = (float)k->angle * 2.0f * (float)M_PI / 65536.0f;
        s = sinf(th); c = cosf(th);
        /* the game's angle convention is 0 = -Y, clockwise: forward is
         * (sin, -cos) and the kart's right is (cos, sin) */
        o[i++] = ( vx * s - vy * c) / top;      /* forward  */
        o[i++] = ( vx * c + vy * s) / top;      /* lateral  */
    }
    {   /* the slip: heading against the direction of travel ($A2 vs $28) */
        float d = ang_delta(p->vel_angle, k->angle) * (float)M_PI;
        o[i++] = sinf(d); o[i++] = cosf(d);
    }
    o[i++] = (float)p->turn / 1024.0f;
    o[i++] = (float)(k->z >> 8) / 65536.0f;
    o[i++] = k->airborne ? 1.0f : 0.0f;
    o[i++] = (p->flags & 0x0008) ? 1.0f : 0.0f;        /* spinning out    */
    o[i++] = (p->flags & 0x0024) ? 1.0f : 0.0f;        /* the drift pose  */

    /* --- race context (6) --- */
    o[i++] = (float)p->coins / 10.0f;
    o[i++] = p->hazard ? 1.0f : 0.0f;                  /* water or the drop */
    {
        int g = ground_of(smk_track_surface(&e->trk, px, py));
        o[i++] = g == SMK_GND_SLOW   ? 1.0f : 0.0f;
        o[i++] = g == SMK_GND_HAZARD ? 1.0f : 0.0f;
    }
    o[i++] = p->item_held ? 1.0f : 0.0f;
    o[i++] = e->crs.sectors ? (e->prog - floorf(e->prog)) : 0.0f;

    /* --- the racing line ahead (4 waypoints x 3 = 12) --- */
    for (int w = 0; w < OBS_WP_AHEAD; w++) {
        int s = e->crs.sectors ? (e->sector + 1 + w) % e->crs.sectors : 0;
        float dx = wrap_px((float)e->crs.wx[s] - (float)px);
        float dy = wrap_px((float)e->crs.wy[s] - (float)py);
        float d  = sqrtf(dx * dx + dy * dy);
        /* the bearing, in the game's own convention: 0 = -Y, clockwise */
        uint16_t bear = (uint16_t)(int)(atan2f(dx, -dy)
                                        * (float)SMK_ANGLE_TURN / (2.0f * (float)M_PI));
        float rel = ang_delta(bear, k->angle) * (float)M_PI;
        o[i++] = sinf(rel);
        o[i++] = cosf(rel);
        o[i++] = d / 256.0f;
    }

    /* --- where the line is, sideways, and where the ROM says to go (3) --- */
    {
        int s = e->sector, n = e->crs.sectors ? (s + 1) % e->crs.sectors : 0;
        float ax = e->crs.wx[s], ay = e->crs.wy[s];
        float dx = wrap_px((float)e->crs.wx[n] - ax), dy = wrap_px((float)e->crs.wy[n] - ay);
        float qx = wrap_px((float)px - ax), qy = wrap_px((float)py - ay);
        float len = sqrtf(dx * dx + dy * dy);
        /* the cross product: signed distance from the line, in px */
        o[i++] = len > 0.0f ? (qx * dy - qy * dx) / len / 128.0f : 0.0f;
    }
    {   /* the ROM's OWN direction field for this cell ($7F:4000, NOTES 056)
         * - the heading its AI would take.  Free, exact, and the single
         * most useful number in the vector. */
        int fcell = ((py >> 4) & 63) * 64 + ((px >> 4) & 63);
        uint16_t flow = (uint16_t)((e->crs.flow[fcell] << 8)
                                   | e->crs.flow[(fcell - 1) & 0xFFF]);
        float rel = ang_delta(flow, k->angle) * (float)M_PI;
        o[i++] = sinf(rel);
        o[i++] = cosf(rel);
    }

    /* --- the probes (12 x 2 = 24) ---
     * Rangefinders over +-150 degrees: how far to a wall, and how far to
     * the edge of the road, in each direction.  The kart cannot see, so
     * this is the substitute for looking, and it is what makes a corner
     * legible without a camera. */
    for (int r = 0; r < SMK_ENV_RAYS; r++) {
        float frac = (SMK_ENV_RAYS == 1) ? 0.0f
                   : (float)r / (float)(SMK_ENV_RAYS - 1) - 0.5f;
        uint16_t dir = (uint16_t)(k->angle + (int)(frac * (5.0f / 6.0f) * 65536.0f));
        float th = (float)dir * 2.0f * (float)M_PI / 65536.0f;
        float sx = sinf(th), sy = -cosf(th);
        float wall = OBS_RAY_MAX, edge = OBS_RAY_MAX;
        for (int d = OBS_RAY_STEP; d <= (int)OBS_RAY_MAX; d += OBS_RAY_STEP) {
            int qx = (px + (int)(sx * d)) & (SMK_WORLD_PX - 1);
            int qy = (py + (int)(sy * d)) & (SMK_WORLD_PX - 1);
            int g = ground_of(smk_track_surface(&e->trk, qx, qy));
            if (g != SMK_GND_ROAD && edge == OBS_RAY_MAX) edge = (float)d;
            if (g == SMK_GND_WALL || g == SMK_GND_HAZARD) { wall = (float)d; break; }
        }
        o[i++] = wall / OBS_RAY_MAX;
        o[i++] = edge / OBS_RAY_MAX;
    }
}

static void frame(smk_env *e, uint16_t held, uint16_t pressed);

/* ---- reset -------------------------------------------------------------
 *
 * The grid is the game's own: smk_course_start_solo for a time trial (the
 * $818F7F nudge both recordings show to the pixel) and the player's grid
 * slot for a race.  The 3-2-1 countdown is run in full, so the state at
 * the lights is the state the SDL game has at its own lights.
 *
 * What is OURS: the agent's first action comes AFTER the lights, so it
 * never chooses the turbo start.  cfg.start_hold sets that for it.
 * Ledger: the agent does not practise the start.
 */
static void env_reset_one(smk_env *e)
{
    /* the track is reloaded because the pickups were eaten and the
     * objects moved: it is 0.2 ms, so this is not worth being clever about */
    char err[128];
    smk_track_load(e->rom, e->cfg.track, -1, &e->trk, err, sizeof err);
    smk_track_place_objects(e->rom, &e->trk);
    if (e->cfg.mode == SMK_MODE_TT) {
        /* the time trial has no coins and no boxes: the demo's own tilemap
         * carries the erase tile where a GP has them */
        uint32_t pc = smk_snes_to_pc(e->rom, 0x818BBDu + (uint32_t)e->trk.theme);
        uint8_t erase = pc < e->rom->size ? e->rom->data[pc] : 0;
        for (int c = 0; c < SMK_MAP_BYTES; c++) {
            uint8_t cls = e->trk.surface[e->trk.map[c]];
            if (cls == 0x14 || cls == 0x1A) e->trk.map[c] = erase;
        }
    }
    smk_course_load(e->rom, e->cfg.track, &e->crs);

    smk_player_setup(e->rom, e->cfg.character, e->cfg.engine_class, &e->player);
    smk_physics_load(e->rom, e->cfg.engine_class, &e->phys);

    smk_racer_start(&e->me, &e->crs, SMK_GRID_SLOT(0));
    float gx, gy; uint16_t gh;
    if (e->cfg.mode == SMK_MODE_TT) smk_course_start_solo(&e->crs, &gx, &gy, &gh);
    else smk_course_start(&e->crs, SMK_GRID_SLOT(0), &gx, &gy, &gh);

    /* OURS: a few pixels of jitter so every episode is not the identical
     * trajectory.  Off by default - a deterministic env is worth more
     * while the reward is still being argued with. */
    if (e->cfg.start_jitter > 0) {
        int j = e->cfg.start_jitter;
        gx += (float)((int)(xrand(&e->rng) % (uint32_t)(2 * j + 1)) - j);
        gy += (float)((int)(xrand(&e->rng) % (uint32_t)(2 * j + 1)) - j);
        gh = (uint16_t)(gh + (int)(xrand(&e->rng) % 2048u) - 1024);
    }

    memset(&e->kart, 0, sizeof e->kart);
    e->kart.x = (int32_t)(gx * SMK_POS_ONE);
    e->kart.y = (int32_t)(gy * SMK_POS_ONE);
    e->kart.angle = gh;
    smk_player_reset(&e->player, gh);
    e->player.coins = (e->cfg.mode == SMK_MODE_TT) ? 0 : 2;
    /* the shell hands a time trial its one mushroom (ledger S19) */
    e->player.item_held = (e->cfg.mode == SMK_MODE_TT) && e->cfg.mushroom;

    smk_autopilot_init(&e->ap);
    e->pad_prev = 0;
    e->sector = e->me.sector;
    e->frames = 0;

    /* The start.  336 frames of $0146 ($809FE1), the throttle building
     * the rev, and smk_player_launch paying out at the lights (NOTES 143).
     *
     * It is here rather than skipped because without it the env and the
     * SDL game do not start from the same state, and an exported run
     * (`smk --pads`) drifts apart from the trajectory that produced it -
     * which was exactly what happened the first time.  The kart is held
     * through all 336, so the cost is one reset, not one per agent step.
     *
     * cfg.start_hold is the countdown frame the throttle goes down on,
     * which is the whole turbo-start skill: -1 never (a plain launch),
     * around 300 is the turbo, too early is a wheelspin.  The agent does
     * not choose it yet - the episode begins after the lights - so this
     * is a knob and a ledger entry, not a learned thing. */
    if (e->cfg.countdown) {
        for (int c = 1; c <= SMK_COUNT_FRAMES; c++) {
            bool thr = e->cfg.start_hold >= 0 && c >= e->cfg.start_hold;
            smk_player_rev(&e->player, thr, (unsigned)c);
            if (c >= SMK_COUNT_FRAMES) smk_player_launch(&e->player);
            /* the throttle and the hop are consumed by the lights; the
             * kart is held, so it is stepped with an empty pad */
            frame(e, 0, 0);
        }
        /* the race clock starts on the frame the lights go out, as
         * main.c's hud_race_frames does */
        e->frames = 1;
        e->pad_prev = 0;
    }
    e->steps = 0;
    e->prog = smk_progress_line(&e->me, &e->crs, &e->kart);
    e->prog_best = e->prog;
    e->stall = 0;
    e->wall_hits = e->offroad_frames = e->rescues = 0;
    e->done = e->truncated = 0;
    e->last_reward = 0.0f;
    e->finish_frame = -1;
}

/* ---- one GAME frame, in the order main.c runs it -----------------------
 *
 * Every line here has a counterpart in main.c's race loop.  The order is
 * not decorative: the rescue target is taken BEFORE the step, the object
 * collision AFTER it, and the pickup collector serves the player on odd
 * frames only, which is the game's own alternation ($81B73B, NOTES 110).
 */
static void frame(smk_env *e, uint16_t held, uint16_t pressed)
{
    smk_player *p = &e->player;
    smk_kart   *k = &e->kart;

    /* Lakitu's target ($80B373): the last sector legitimately reached */
    if (p->hazard != 6 && p->hazard != 0x0C && p->hazard != 0x0E) {
        int s = e->sector;
        if (s >= 0 && s < e->crs.sectors) {
            int wx = e->crs.wx[s], wy = e->crs.wy[s];
            p->resc_x = wx; p->resc_y = wy;
            int fcell = ((wy >> 4) & 63) * 64 + ((wx >> 4) & 63);
            p->resc_h = (uint16_t)((e->crs.flow[fcell] << 8)
                                   | e->crs.flow[(fcell - 1) & 0xFFF]);
        }
    }
    /* $84DBD5: which obstacles are on the track for this lap segment */
    smk_course_spawn(&e->crs, e->sector, 0, false);

    bool grounded = k->z == 0;
    int was_hazard = p->hazard;
    smk_player_step(p, k, &e->trk, held, pressed);

    k->star = (p->flags & 2) ? 1 : 0;
    k->hazard_hit = 0;
    if (!p->hazard) smk_collide_objects(k, &e->crs);
    if (k->hazard_hit == 2)      p->squash_t = SMK_SQUASH_T;
    else if (k->hazard_hit == 3) { if (!p->mole_on) { p->mole_on = 1; p->mole_hops = 0; p->mole_dir = 0; } }
    else if (k->hazard_hit)      smk_player_hit_banana(p, k);
    if (p->mole_on && k->speed > 0x100) k->speed = 0x100;
    if (p->squash_t > 0) { p->squash_t--; k->speed = 0; k->speed_frac = 0; }
    /* the rescue's exit: Lakitu takes his two-coin fee */
    if (was_hazard == 0x0E && p->hazard == 0) {
        int fee = p->coins < 2 ? p->coins : 2;
        p->coins -= fee;
        e->rescues++;
    }
    /* the collector serves P1 on odd frames (NOTES 110) */
    if ((e->frames & 1) == 1) smk_pickup_step(e->rom, &e->trk, p, k, grounded);
    /* the Thwomps only move once the first lap is complete */
    smk_course_movers_step(&e->crs, e->me.lap >= 2);

    smk_progress_step(&e->me, &e->crs, k);
    e->sector = e->me.sector;
    e->frames++;
}

/* ---- the reward (OURS, entirely) ---------------------------------------
 *
 * The game has no reward; this is a training signal and nothing more.
 *
 *   + progress along the racing line, in sectors.  Dense, signed, and
 *     the thing actually being optimised.  smk_progress_line, not the
 *     ROM's progress WORD: the word is a monotonic watermark that steps
 *     once a sector - maybe twice a second - and hides mistakes by
 *     construction, which is the opposite of what shaping wants.
 *   - a per-frame time cost, so dawdling on a perfect line still loses.
 *   - wall contact and off-road frames, small: they are already punished
 *     by the physics, and paying twice teaches timidity.
 *   - the rescue, which costs seconds and two coins in the game itself.
 *   + a finish bonus scaled by how much of the budget was left.
 *
 * Deliberately NOT rewarded: speed.  Rewarding speed directly teaches a
 * kart to hug the outside wall at full throttle, because the wall is
 * fast and the corner is not.
 */
static float reward(smk_env *e, float prog_before, int hit_wall, int off, int resc)
{
    const smk_env_cfg *c = &e->cfg;
    float d = e->prog - prog_before;
    /* a wrap with no lap change (the 90-frame cooldown) is not a lap lost */
    float half = (float)e->crs.sectors * 0.5f;
    if (d >  half) d -= (float)e->crs.sectors;
    if (d < -half) d += (float)e->crs.sectors;
    float r = c->w_progress * d
            - c->w_time * (float)c->frame_skip
            - c->w_wall * (float)hit_wall
            - c->w_offroad * (float)off
            - c->w_rescue * (float)resc;
    if (e->done && e->finish_frame >= 0) {
        float left = 1.0f - (float)e->finish_frame / (float)c->max_frames;
        r += c->w_finish * (left > 0.0f ? left : 0.0f);
    }
    return r;
}

/* ---- the batch ---------------------------------------------------------
 *
 * The envs are stepped in a loop inside C, not one at a time from Python:
 * at 1.4 M steps a second a per-env FFI call would cost more than the
 * simulation.  One call in, one batch of observations out.
 *
 * The ROM is loaded ONCE and shared read-only; everything mutable is per
 * env.  There is no threading here on purpose - src/ai.c's
 * `course_for_step` is a global, so two envs in two threads would fight
 * over it.  Parallelism is processes, which is what a vectorised learner
 * wants anyway.
 */
struct smk_env_batch {
    smk_rom  rom;
    int      n;
    smk_env *env;
};

void smk_env_cfg_default(smk_env_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->track = 0;
    c->character = 0;
    c->engine_class = 1;          /* 100cc */
    c->mode = SMK_MODE_TT;
    c->laps = 3;                  /* OURS: shorter than the game's five,
                                     because an episode is a training unit */
    c->frame_skip = 4;
    c->max_frames = 10800;        /* three minutes at 60 Hz */
    c->stall_frames = 300;
    c->mushroom = 1;          /* the shell hands a time trial one (ledger S19) */
    c->countdown = 1;
    c->start_hold = -1;       /* a plain launch, not the turbo start */
    c->start_jitter = 0;
    c->seed = 1;
    c->w_progress = 1.0f;
    c->w_time     = 0.002f;
    c->w_wall     = 0.05f;
    c->w_offroad  = 0.002f;
    c->w_rescue   = 2.0f;
    c->w_finish   = 20.0f;
}

int smk_env_obs_dim(void)     { return SMK_ENV_OBS; }
int smk_env_action_count(void){ return SMK_ENV_ACTIONS_N; }

smk_env_batch *smk_env_batch_create(const char *rom_path, const smk_env_cfg *cfgs,
                                    int n, char *err, size_t errn)
{
    if (n <= 0) { snprintf(err, errn, "n must be positive"); return NULL; }
    smk_env_batch *b = calloc(1, sizeof *b);
    if (!b) { snprintf(err, errn, "out of memory"); return NULL; }
    if (!smk_rom_load(&b->rom, rom_path, err, errn)) { free(b); return NULL; }
    b->n = n;
    b->env = calloc((size_t)n, sizeof *b->env);
    if (!b->env) { smk_rom_free(&b->rom); free(b); snprintf(err, errn, "out of memory"); return NULL; }
    smk_ai_catchup_load(&b->rom);
    for (int i = 0; i < n; i++) {
        b->env[i].cfg = cfgs[i];
        b->env[i].rom = &b->rom;
        b->env[i].rng = cfgs[i].seed ? cfgs[i].seed : (uint32_t)(i + 1);
        if (b->env[i].cfg.track < 0 || b->env[i].cfg.track >= SMK_TRACK_COUNT) {
            snprintf(err, errn, "env %d: track %d out of range", i, b->env[i].cfg.track);
            smk_env_batch_destroy(b);
            return NULL;
        }
        if (b->env[i].cfg.frame_skip < 1) b->env[i].cfg.frame_skip = 1;
        /* Every observation that reads the racing line indexes it by the
         * kart's sector, so a course with no sector data would index
         * wx[-1].  Refuse it here rather than read out of bounds later. */
        {
            static smk_course probe;
            if (!smk_course_load(&b->rom, b->env[i].cfg.track, &probe)
                || probe.sectors <= 0) {
                snprintf(err, errn, "env %d: track %d has no course data",
                         i, b->env[i].cfg.track);
                smk_env_batch_destroy(b);
                return NULL;
            }
        }
    }
    return b;
}

void smk_env_batch_destroy(smk_env_batch *b)
{
    if (!b) return;
    free(b->env);
    smk_rom_free(&b->rom);
    free(b);
}

int smk_env_batch_size(const smk_env_batch *b) { return b ? b->n : 0; }

void smk_env_batch_reset(smk_env_batch *b, float *obs)
{
    for (int i = 0; i < b->n; i++) {
        course_for_step = &b->env[i].crs;
        env_reset_one(&b->env[i]);
        if (obs) observe(&b->env[i], obs + (size_t)i * SMK_ENV_OBS);
    }
}

/* One agent step = cfg.frame_skip game frames with the action held, which
 * is how a person's input actually looks at 60 Hz. */
static void step_one(smk_env *e, int action, float *obs, float *rew,
                     uint8_t *done, uint8_t *trunc, float *info)
{
    course_for_step = &e->crs;
    if (action < 0 || action >= SMK_ENV_ACTIONS_N) action = 0;
    /* the item is a request, not a guarantee: the game refuses it while
     * spinning, and there may be nothing held */
    const smk_env_act *a = &SMK_ENV_ACTIONS[action];

    float prog_before = e->prog;
    int hit_wall = 0, off = 0, resc0 = e->rescues;

    for (int f = 0; f < e->cfg.frame_skip; f++) {
        uint16_t pressed, held = pad_of(action, e->pad_prev, &pressed);
        e->pad_prev = held;
        if (a->item && e->player.item_held) {
            if (smk_player_boost(&e->player)) e->player.item_held = false;
        }
        frame(e, held, pressed);
        if (e->kart.bounce_hit) hit_wall++;
        if (ground_of(smk_track_surface(&e->trk, smk_kart_px(e->kart.x),
                                        smk_kart_px(e->kart.y))) == SMK_GND_SLOW)
            off++;
        if (e->me.lap >= e->cfg.laps + 1) {          /* the grid crossing is lap 1 */
            e->done = 1;
            e->finish_frame = e->frames;
            break;
        }
    }
    e->steps++;
    e->wall_hits += hit_wall;
    e->offroad_frames += off;
    e->prog = smk_progress_line(&e->me, &e->crs, &e->kart);
    if (e->prog > e->prog_best + 0.01f) { e->prog_best = e->prog; e->stall = 0; }
    else e->stall += e->cfg.frame_skip;

    if (!e->done) {
        if (e->frames >= e->cfg.max_frames) e->truncated = 1;
        else if (e->cfg.stall_frames > 0 && e->stall >= e->cfg.stall_frames) e->truncated = 1;
    }
    float r = reward(e, prog_before, hit_wall, off, e->rescues - resc0);
    e->last_reward = r;

    if (rew)   *rew = r;
    if (done)  *done = (uint8_t)e->done;
    if (trunc) *trunc = (uint8_t)e->truncated;
    if (info) {
        info[0] = (float)e->me.lap;
        info[1] = (float)e->frames;
        info[2] = e->prog;
        info[3] = (float)e->kart.speed;
        info[4] = (float)e->wall_hits;
        info[5] = (float)e->rescues;
        info[6] = (float)e->finish_frame;
        info[7] = (float)e->cfg.track;
    }
    /* autoreset, as a vectorised learner expects: the observation handed
     * back with a terminal step is already the NEXT episode's first.  The
     * final observation is not needed by PPO's bootstrap here because a
     * finish is a true terminal (value 0) and a truncation is bootstrapped
     * from the value BEFORE the reset - which is what tools/rl/train.py
     * does, using `truncated` to tell the two apart. */
    if (e->done || e->truncated) env_reset_one(e);
    if (obs) observe(e, obs);
}

void smk_env_batch_step(smk_env_batch *b, const int32_t *actions, float *obs,
                        float *rew, uint8_t *done, uint8_t *trunc, float *info)
{
    for (int i = 0; i < b->n; i++)
        step_one(&b->env[i], actions[i],
                 obs   ? obs   + (size_t)i * SMK_ENV_OBS : NULL,
                 rew   ? rew   + i : NULL,
                 done  ? done  + i : NULL,
                 trunc ? trunc + i : NULL,
                 info  ? info  + (size_t)i * SMK_ENV_INFO : NULL);
}

/* The scripted driver's action for each env, so the harness has a
 * baseline that goes round without any learning at all - and so that a
 * broken observation or reward shows up as "the autopilot scores badly",
 * which is a far sharper test than watching a policy fail to improve. */
void smk_env_batch_autopilot(smk_env_batch *b, int32_t *actions)
{
    for (int i = 0; i < b->n; i++) {
        smk_env *e = &b->env[i];
        course_for_step = &e->crs;
        smk_autopilot_out o;
        smk_autopilot_step(&e->ap, &e->trk, &e->crs, &e->player, &e->kart, &o);
        int act = 1;
        if (o.item && e->player.item_held) act = 12;
        else if (o.brake) act = o.left ? 8 : o.right ? 9 : 7;
        else if (o.accel) {
            if (o.hop_held && o.left)  act = 4;
            else if (o.hop_held && o.right) act = 5;
            else if (o.hop_held || o.hop)   act = 6;
            else if (o.left)  act = 2;
            else if (o.right) act = 3;
            else act = 1;
        } else act = o.left ? 10 : o.right ? 11 : 0;
        actions[i] = act;
    }
}

/* read-only telemetry for one env, for a viewer or a debug dump */
void smk_env_batch_state(const smk_env_batch *b, int i, smk_env_state *out)
{
    const smk_env *e = &b->env[i];
    memset(out, 0, sizeof *out);
    out->x = (float)smk_kart_px(e->kart.x);
    out->y = (float)smk_kart_px(e->kart.y);
    out->heading = e->kart.angle;
    out->speed = e->kart.speed;
    out->lap = e->me.lap;
    out->sector = e->sector;
    out->frames = e->frames;
    out->coins = e->player.coins;
    out->progress = e->prog;
    out->track = e->cfg.track;
}

/* ---- watching a policy drive ------------------------------------------
 *
 * The learner chooses an action index; the SDL game presses buttons.
 * These two turn one into the other so `--pads` can replay a trained
 * policy in the real window, at the real resolution, with the sound on -
 * which is the only way to actually SEE whether the lap time came from
 * driving well or from finding a hole in the reward.
 */
uint16_t smk_env_action_pad(int action)
{
    if (action < 0 || action >= SMK_ENV_ACTIONS_N) action = 0;
    uint16_t dummy;
    return pad_of(action, 0xFFFF, &dummy);   /* held only; the caller does edges */
}
int smk_env_action_uses_item(int action)
{
    if (action < 0 || action >= SMK_ENV_ACTIONS_N) return 0;
    return SMK_ENV_ACTIONS[action].item;
}
