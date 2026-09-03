/* A driver that plays the game.
 *
 * `--autodrive` used to steer by the course direction field with bang-bang
 * left/right, which is roughly what the AI does - but the AI is not playing
 * the game.  It writes its own heading and speed, ignores surfaces, and
 * teleports itself back onto the road when it gets stuck (src/ai.c).  A
 * driver built that way cannot exercise the player's rules, because it does
 * not obey them.
 *
 * This one only ever presses BUTTONS.  It gets the same pad word a person
 * would hand smk_player_step, so everything the player's decoded control
 * does - the acceleration curve, off-road caps, the slide machine,
 * spin-out, wall bounce and its cost, hop, Lakitu - happens to it exactly
 * as it happens to you.  That makes it worth something as a test: it drives
 * the code under test rather than a copy of it.
 *
 * What it knows is the ROM's own route: the per-sector waypoints
 * (`crs->wx/wy`, NOTES 042) that the game's AI aims at.  Those are a
 * REFERENCE, not a rail:
 *
 *   - it aims at a point a variable distance ahead along that route, and
 *     the distance grows with speed, which is what makes it turn in early
 *     rather than clip the apex (a fixed two-sector aim drove it into
 *     walls);
 *   - it looks at the ground it is about to cross - solid tiles and the
 *     off-road caps the physics will apply - and bends the aim away from
 *     what it cannot drive through;
 *   - it reads the bend in the route beyond the aim point and brakes for
 *     it, because the player's own physics will not slow it down for free;
 *   - it hops into a slide when the corner is sharp enough to need one,
 *     the way the game is actually driven;
 *   - and when it does get stuck, it turns and drives out under power.
 *     No teleporting: it has to earn it back like anyone else.
 *
 * LABELLED, plainly: none of the POLICY here is the ROM's.  The lookahead
 * law, the cornering speeds, the probe and the recovery are ours - a
 * competent driver, not a decoded one.  The route points and the surface
 * behaviour it reacts to are the game's.
 */
#include "smk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Tuning switches, read once.  The driver was tuned by turning each piece
 * off and timing five laps of Mario Circuit 1 against the others; leaving
 * them here means the next person can redo that in a minute instead of
 * rediscovering which part costs what.  Measured, 50cc, at the time:
 *
 *     everything on            2'34"      no slide            2'01"
 *     no brake                 2'23"      no brake, no slide  1'58"
 *     no probe                 2'25"
 *
 * which is how the slide came to be short and rare rather than held: a
 * 90-frame hold on anything that merely bent was costing 33 seconds. */
static int off(const char *name)
{
    return getenv(name) != NULL;
}

/* the ROM's angle: 0 = -Y, clockwise, so heading h points (sin h, -cos h) */
static uint16_t heading_to(float dx, float dy)
{
    float a = atan2f(dx, -dy) * (float)SMK_ANGLE_TURN / (2.0f * (float)M_PI);
    return (uint16_t)(int)lrintf(a);
}

static int ang_abs(int a) { return a < 0 ? -a : a; }

void smk_autopilot_init(smk_autopilot *a)
{
    memset(a, 0, sizeof *a);
    a->last_px = a->last_py = -1;
}

/* How far ahead to aim, in world pixels.  All of the character of the
 * driver is in this one number: too short and it saws at the wheel and
 * clips apexes, too long and it cuts corners across the grass. */
static int lookahead_px(int speed)
{
    int d = 40 + speed / 7;
    return d > 190 ? 190 : d;
}

/* Clear run along a heading, in probe steps, over ground the kart can
 * actually use.  Solid stops it dead; off-road counts for less than road,
 * so the probe prefers a line that stays on the tarmac. */
static int probe(const smk_track *trk, int px, int py, uint16_t h, int *road)
{
    float a = (float)h * (2.0f * (float)M_PI) / (float)SMK_ANGLE_TURN;
    float sx = sinf(a), sy = -cosf(a);
    int clear = 0;
    *road = 0;
    for (int step = 1; step <= 9; step++) {
        int d = step * 10;
        int x = px + (int)(sx * (float)d);
        int y = py + (int)(sy * (float)d);
        uint8_t s = smk_track_surface(trk, x, y);
        if (smk_surface_solid(s)) break;
        clear++;
        if (smk_surface_cap_frac(s) >= 1000) (*road)++;
    }
    return clear;
}

/* THE ITEM BUTTON.  The user, on the second view's driver: "2p-cpu player
 * must use the items.  At least randomly for now."  So it presses the
 * button like a person: a short wait after the roulette settles, then a
 * press.  The wait is DETERMINISTIC - it comes from the driver's own tick
 * counter, not from a random number - because a replay has to stay a
 * replay.  OURS, labelled: when the game's own AI fires is not decoded,
 * and this driver is emulating a person anyway. */
#define AP_ITEM_MIN  30        /* half a second at the least */
#define AP_ITEM_SPAN 120       /* and up to two and a half   */

void smk_autopilot_step(smk_autopilot *a, const smk_track *trk,
                        const smk_course *crs, const smk_player *p,
                        const smk_kart *k, smk_autopilot_out *out)
{
    static int inited, no_brake, no_slide, no_probe, ap_lead = 16, ap_dead = 0x300;
    if (!inited) {
        inited = 1;
        no_brake = off("SMK_AP_NOBRAKE");
        no_slide = off("SMK_AP_NOSLIDE");
        no_probe = off("SMK_AP_NOPROBE");
        { const char *e = getenv("SMK_AP_LEAD"); if (e) ap_lead = atoi(e); }
        { const char *e = getenv("SMK_AP_DEAD"); if (e) ap_dead = atoi(e); }
    }
    memset(out, 0, sizeof *out);
    /* what it is holding, before any of the steering's early returns */
    if (!p->item_held) {
        a->item_wait = 0;
    } else if (a->item_wait == 0) {
        a->item_wait = AP_ITEM_MIN + (int)((unsigned)a->tick * 37u % AP_ITEM_SPAN);
    } else if (a->item_wait > 1) {
        a->item_wait--;
    } else if (!k->airborne && !p->hazard) {
        out->item = true;                 /* one press, then it is spent */
        a->item_wait = 0;
    }
    if (crs->sectors <= 0) { out->accel = true; return; }

    int px = smk_kart_px(k->x), py = smk_kart_px(k->y);

    /* Where it is on the lap.  The ROM's own rule first ($808962): keep
     * the last sector while off-course, and while airborne reject a jump
     * sector's waypoint.
     *
     * Then a rule of the driver's own.  Where a course runs back alongside
     * itself the cell under the kart can belong to the OTHER stretch, and
     * believing it sends the driver away down the wrong one: on Mario
     * Circuit 2 that put it in a loop between sectors 22 and 31 that it
     * never escaped.  A driver knows roughly how far round it is, so a
     * sector is only believed if it is near the one it already had. */
    uint8_t cell = smk_course_cell(crs, px, py);
    int sec = cell & SMK_SECT_OFF;
    if (sec != SMK_SECT_OFF && sec < crs->sectors
        && !(k->airborne && (crs->wattr[sec] & 0x80))) {
        int n0 = crs->sectors;
        int fwd = (sec - a->sector + n0) % n0;      /* 0..n-1 ahead */
        if (fwd <= 4 || fwd >= n0 - 2 || a->lost > 90) {
            a->sector = sec;
            a->lost = 0;
        } else {
            /* it disagrees; if it keeps disagreeing we are the ones who
             * are wrong, so give in rather than drive off for ever */
            a->lost++;
        }
    }
    sec = a->sector;

    /* While Lakitu has the kart, or it is in the water, the controls are
     * not the driver's to use - hold the throttle and wait. */
    if (p->hazard) { out->accel = true; a->still = 0; return; }

    /* ---- the aim point, a variable distance along the ROM's route ---- */
    int aim = (sec + 1) % crs->sectors;
    float ax = (float)crs->wx[aim], ay = (float)crs->wy[aim];
    float run = hypotf(ax - (float)px, ay - (float)py);
    int want_d = lookahead_px(k->speed);
    for (int n = 0; n < crs->sectors && run < (float)want_d; n++) {
        int nxt = (aim + 1) % crs->sectors;
        run += hypotf((float)crs->wx[nxt] - ax, (float)crs->wy[nxt] - ay);
        aim = nxt;
        ax = (float)crs->wx[aim]; ay = (float)crs->wy[aim];
    }
    uint16_t route = heading_to(ax - (float)px, ay - (float)py);

    /* WHERE to point, and WHY it is not simply the route.
     *
     * The route points are sector CENTROIDS, not a drivable polyline: on
     * Mario Circuit 2 the straight line from sector 29's point to sector
     * 30's crosses a solid wall, and a driver that follows it parks
     * against that wall.  ai.c already records the same trap - the ROM
     * steers ON COURSE by the per-cell direction field ($7F:4000, NOTES
     * 056) and only falls back to atan2-toward-a-waypoint when it is OFF
     * course.  That field is built FROM these waypoints, so it is the same
     * route in the form that knows where the road is.
     *
     * So the field steers, and the route earns its keep further down,
     * where the driver decides how fast to arrive.  Steering by the route
     * and braking by the field is the wrong way round and cost a wall. */
    uint16_t want = route;
    if ((cell & SMK_SECT_OFF) != SMK_SECT_OFF)
        want = (uint16_t)(crs->flow[((py >> 4) & 63) * 64
                                    + ((px >> 4) & 63)] << 8);

    /* ---- how hard the ROAD bends ahead, so it can brake for it -------
     *
     * Measured off the direction field along the way the kart is actually
     * going, not off the route points.  The route-point version read
     * 60-110 degrees almost everywhere on a near-oval, because a chord
     * between sector centroids is not the road's curvature, and the driver
     * crawled the whole lap at the cornering floor. */
    int bend = 0;
    {
        float ha = (float)want * (2.0f * (float)M_PI) / (float)SMK_ANGLE_TURN;
        float hx = sinf(ha), hy = -cosf(ha);
        int reach = 40 + k->speed / 8;
        uint16_t f0 = want, f1 = want;
        int gx = (px + (int)(hx * (float)(reach / 2)));
        int gy = (py + (int)(hy * (float)(reach / 2)));
        uint8_t c1 = smk_course_cell(crs, gx, gy);
        if ((c1 & SMK_SECT_OFF) != SMK_SECT_OFF)
            f0 = (uint16_t)(crs->flow[((gy >> 4) & 63) * 64 + ((gx >> 4) & 63)] << 8);
        gx = (px + (int)(hx * (float)reach));
        gy = (py + (int)(hy * (float)reach));
        uint8_t c2 = smk_course_cell(crs, gx, gy);
        if ((c2 & SMK_SECT_OFF) != SMK_SECT_OFF)
            f1 = (uint16_t)(crs->flow[((gy >> 4) & 63) * 64 + ((gx >> 4) & 63)] << 8);
        bend = ang_abs((int)(int16_t)(uint16_t)(f1 - want))
             + ang_abs((int)(int16_t)(uint16_t)(f0 - want)) / 2;
    }
    (void)aim; (void)route;
    int err = (int)(int16_t)(uint16_t)(want - p->heading);

    /* ---- bend the aim away from what it cannot drive through ---------- */
    if (!k->airborne) {
        static const int OFF[] = { 0, -0x0500, 0x0500, -0x0A00, 0x0A00,
                                   -0x1000, 0x1000, -0x1A00, 0x1A00 };
        int best = -1000000;
        uint16_t pick = want;
        for (size_t i = 0; i < sizeof OFF / sizeof OFF[0]; i++) {
            uint16_t h = (uint16_t)(want + OFF[i]);
            int road = 0;
            int clear = probe(trk, px, py, h, &road);
            if (i == 0) a->ahead = clear;    /* how far it can see */
            /* open ground first, tarmac next, and among equals the line
             * closest to the route */
            /* the deviation penalty has to be steep, or the probe
             * wanders off the route whenever the grass beside it happens
             * to read one step more open than the road */
            int score = clear * 24 + road * 6 - ang_abs(OFF[i]) / 0x80;
            if (score > best) { best = score; pick = h; }
        }
        a->dbg_dev = (int)(int16_t)(uint16_t)(pick - want);
        if (!no_probe) want = pick;
        err = (int)(int16_t)(uint16_t)(want - p->heading);
    }

    /* ---- stuck: turn out and drive, no teleport -----------------------
     *
     * Stagnation has to be measured over a WINDOW.  Testing whether the
     * kart moved since last frame looks right and catches nothing: pinned
     * against the rail on Rainbow Road it jittered between x=297 and 298
     * for sixty thousand frames with the throttle wide open, and an
     * exact-equality test reset on every one of them.  (ai.c has the same
     * hole.)  So: sample every 30 frames and ask how far it actually got. */
    if (++a->tick >= 30) {
        int dx = px - a->last_px, dy = py - a->last_py;
        a->tick = 0;
        a->last_px = px; a->last_py = py;
        if (dx * dx + dy * dy < 10 * 10) a->still++;
        else a->still = 0;
    }
    if (a->recover > 0) {
        a->recover--;
        out->accel = true;
        out->left = a->recover_dir < 0;
        out->right = a->recover_dir > 0;
        return;
    }
    if (a->still >= 2) {
        /* pick the most open compass point and commit to it for a while;
         * without the hold the aim drags it straight back into the wall */
        int best = -1, bestd = 0;
        for (int d = 0; d < 8; d++) {
            int road = 0;
            int clear = probe(trk, px, py, (uint16_t)(d * 0x2000), &road);
            int score = clear * 8 + road;
            if (score > best) { best = score; bestd = d; }
        }
        int d = (int)(int16_t)(uint16_t)((uint16_t)(bestd * 0x2000) - p->heading);
        a->recover_dir = d < 0 ? -1 : 1;
        a->recover = 45;
        a->still = 0;
        out->accel = true;
        out->left = a->recover_dir < 0;
        out->right = a->recover_dir > 0;
        return;
    }

    /* ---- steering -----------------------------------------------------
     *
     * Bang-bang on the error alone weaves: the input is held until the
     * heading actually reaches the target, by which time the kart is
     * already turning hard and sails straight past it, so the next frame
     * corrects the other way.  Watching it, the word for it is drunk.
     *
     * The cure is not a shorter press, it is steering on where the heading
     * is GOING to be.  $B2 is the turn rate the ROM adds to the heading
     * (>> 3 each frame, $80AFBE), which is exactly the derivative term a
     * bang-bang controller needs: lead it by a few frames and the input
     * releases while the kart is still swinging, so it arrives on the
     * target instead of crossing it.
     *
     * Measured, five laps of Mario Circuit 1, 50cc - lead in frames and
     * the deadband either side of the target heading:
     *
     *     lead  0, dead $200   2'01"48   203 reversals   21.7 deg mean err
     *     lead 12, dead $200   1'39"00   250             16.9
     *     lead 16, dead $200   1'34"86   232             15.9
     *     lead 14, dead $100   1'38"16   430             16.6   (chatters)
     *     lead 16, dead $300   1'34"36   126             16.4   <- taken
     *     lead 16, dead $400   1'36"65   102             18.0
     *
     * so leading the heading is worth 27 seconds over five laps, and a
     * deadband of $300 halves the number of times the wheel changes hands
     * for nothing.  ($300 is what the first bang-bang driver used; I had
     * narrowed it to $140 on the theory that tighter is more accurate,
     * which is exactly backwards without a derivative term.) */
    const int DEAD = ap_dead;
    int lead = ((int)p->turn * ap_lead) >> 3;
    int e = err - lead;
    out->left = e < -DEAD;
    out->right = e > DEAD;
    /* Two numbers for the weave: how often the wheel changes hands, and -
     * the one that actually matters - how far off the intended heading the
     * kart sits on average.  A precise driver still makes many small
     * corrections, so the reversal COUNT alone says little; the mean error
     * is what "drunk" looks like as a measurement. */
    a->dbg_err_sum += ang_abs(err);
    a->dbg_err_n++;
    if (out->left && a->last_steer > 0) a->dbg_flips++;
    if (out->right && a->last_steer < 0) a->dbg_flips++;
    a->last_steer = out->left ? -1 : (out->right ? 1 : a->last_steer);

    /* ---- the slide, which is how the game takes a real corner --------- */
    int need = ang_abs(err);
    bool grounded = !k->airborne && k->z == 0;
    /* A power slide is a short, deliberate thing.  Held on anything that
     * merely bends - which is what a 90-frame hold on `bend > 0x1600`
     * amounts to - it only scrubs speed: measured at THIRTY-THREE seconds
     * over five laps of Mario Circuit 1 against not sliding at all.  So it
     * is armed for a genuinely sharp corner and dropped the moment the
     * kart is pointing where it wanted to go. */
    if (a->slide > 0) {
        a->slide--;
        if (need < 0x0900) a->slide = 0;
    } else if (grounded && k->speed > 520 && need > 0x2200) {
        a->slide = 30;
        out->hop = true;                  /* the press that starts it */
    }
    if (no_slide) { a->slide = 0; out->hop = false; }
    out->hop_held = a->slide > 0;

    /* ---- throttle: the physics will not slow down for the corner ------ */
    int limit = p->target;
    int sharp = bend + need / 2;
    /* Lift for a real corner and no more.  Braking on anything that merely
     * bends cost eleven seconds over five laps of Mario Circuit 1 against
     * simply holding the throttle: the slide above is what gets it round a
     * corner, and the off-road caps punish the rest. */
    if (sharp > 0x3400)      limit = limit * 66 / 100;
    else if (sharp > 0x2600) limit = limit * 82 / 100;
    /* Slow down for ground you cannot see across.  The probe already knows
     * how far the kart can go before it meets something it cannot drive on
     * - a wall, or on Rainbow Road the edge of the world, which is the
     * same $20-bit class - so use it.  Without this the driver went off
     * the side of Rainbow Road again and again: it was rescued, made
     * progress, and simply ran out of frames. */
    if (a->ahead <= 3)      { if (limit > 380) limit = 380; }
    else if (a->ahead <= 5) { if (limit > 520) limit = 520; }
    else if (a->ahead <= 7) { if (limit > 660) limit = 660; }
    if (limit < 340) limit = 340;         /* never crawl to a stop */

    a->dbg_bend = bend; a->dbg_need = need; a->dbg_limit = limit;
    a->dbg_aim = aim;
    if (no_brake) limit = 1 << 20;
    if (k->speed > limit + 0x50) out->brake = true;
    else out->accel = true;
}
