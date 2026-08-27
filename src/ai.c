/* Opponent karts: the AI, in the library so the game and the lap
 * regression run the SAME code.
 *
 * This used to be static in main.c, which meant the "AI completes a lap
 * on 20/20 tracks" test could only have been exercising a SECOND copy of
 * this logic - a gate that could pass while the shipped AI broke.  One
 * implementation, one test.
 */
#include "smk.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

smk_course *course_for_step;

/* |(vx,vy)| the way the ROM takes it - the same length the wall response
 * re-derives the speed with ($80FA5A). */
/* How hard a low-speed object hit shoves back.
 *
 * A WALL forces each component to +-$100 ($80F9C1) - a constant push-back
 * whatever the speed.  An object is milder: at $100 the user reports the
 * low-speed hit "feels too aggressive", and a slow arrival leaving at
 * three times its own speed is exactly that.  Measured on the repro -
 * final distance from a low-speed contact after 240 frames, driving in and
 * holding each direction:
 *
 *     kick    0    7.0 / 7.2 / 6.4   glued, whatever you steer
 *     kick $60   10.0 /16.4 / 8.9   one direction still stuck
 *     kick $80   12.0 /28.0 /14.2   frees in all three   <- taken
 *     kick $B0   14.0 /19.7 /36.7
 *     kick $100  10.0 /30.8 /38.1   frees, but it kicks
 *
 * $80 is the mildest shove that still lets you work free the way the game
 * does, which is half a wall's.  LABELLED: the value is fitted to that
 * behaviour, not read from the ROM - NOTES 072 measured the object
 * response as reflect and 308/581 with no floor at all, and no floor
 * leaves you glued. */
static int obj_kick(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("SMK_OBJ_KICK");
        v = e ? atoi(e) : 0x80;
    }
    return v;
}

static int16_t vec_len_pub(int16_t vx, int16_t vy)
{
    double d = sqrt((double)vx * vx + (double)vy * vy);
    return (int16_t)(d > 32767.0 ? 32767.0 : d);
}

/* Opponents: drive the game's own racing line.
 *
 * The DATA is the ROM's - sector map, waypoints, acceleration tables - and
 * the steering LAW matches the decoded shape: the AI aims at the waypoint
 * of the sector ahead ($80B0B1: waypoint minus position into atan2) and the
 * heading slews toward that target, snapping when close ($80AFBE).
 * PLACEHOLDER values, marked: the slew rate, the target-speed entry per
 * kart, and rubber-banding (none).  Lap counting below is ours too: the
 * ROM's crossing test is not decoded, so we count a lap when a kart on the
 * finish strip has come around through the back half of the course.
 */


/* Race position: order racers by (lap, sector), ties by distance to the
 * next waypoint (closer = ahead).  Our bookkeeping, not the ROM's ranking
 * code - labelled as such. */
int smk_race_rank(const smk_racer *racers, int who, const smk_course *crs)
{
    int rank = 1;
    long mine = ((long)racers[who].lap << 8) | (long)racers[who].sector;
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        if (i == who) continue;
        long theirs = ((long)racers[i].lap << 8) | (long)racers[i].sector;
        if (theirs > mine) { rank++; continue; }
        if (theirs == mine && crs->sectors) {
            int nsec = (racers[i].sector + 1) % crs->sectors;
            float wx = (float)crs->wx[nsec], wy = (float)crs->wy[nsec];
            float dxm = (float)smk_kart_px(racers[who].k.x) - wx;
            float dym = (float)smk_kart_px(racers[who].k.y) - wy;
            float dxt = (float)smk_kart_px(racers[i].k.x) - wx;
            float dyt = (float)smk_kart_px(racers[i].k.y) - wy;
            if (dxt * dxt + dyt * dyt < dxm * dxm + dym * dym) rank++;
        }
    }
    return rank;
}


void smk_collide_objects(smk_kart *k, const smk_course *crs)
{
    /* NOTES 075: the per-track object list ($85:D000) holds GROUND
     * features only - item boxes, coins, oil - all stamped into the
     * tilemap and none solid.  The live entities (pipes, moles, Lakitu)
     * come from a separate spawn system (the $1800 object blocks) that is
     * not decoded yet, so right now there is nothing to collide with.
     *
     * The spawner is now decoded (NOTES 078): per-track entity list at
     * $85:C800 + track*64.  Static cylinder collision at those positions
     * with the MEASURED pipe response (crash lab, NOTES 072): velocity
     * REFLECTS about the contact normal, both components halve, speed
     * scales 308/581, and a 10-frame ballistic window follows.  Movers
     * (Thwomps, moles) are static interim - positions right, motion not
     * yet decoded (labelled). */
    /* Only the LIVE slots.  The game spawns two object blocks in a
     * one-player race ($819136: `lda #$0004`, minus two unless $B6 says
     * two-player) and everything downstream - drawing and collision alike
     * - works on those blocks.  This used to walk the whole decoded
     * entity list while draw_scene drew only the live pair, so the track
     * was full of obstacles you could hit but never see: "there should be
     * two thwomps and there is only one, but you can still hit the
     * invisible one" (user, NOTES 151). */
    int kx = smk_kart_px(k->x), ky = smk_kart_px(k->y);
    /* Whatever is DRAWN is what you can hit - the two must never disagree
     * again (NOTES 151).  With smk_obj_show_all that is every entity. */
    int nvis = (smk_obj_show_all || !crs->nlive) ? crs->nent : crs->nlive;
    for (int j = 0; j < nvis; j++) {
        int i = (smk_obj_show_all || !crs->nlive) ? j : crs->live[j];
        int dx = kx - (int)crs->ent[i].x, dy = ky - (int)crs->ent[i].y;
        int d2 = dx * dx + dy * dy;
        if (d2 >= SMK_OBJ_RADIUS * SMK_OBJ_RADIUS || d2 == 0) continue;
        /* A raised Thwomp is overhead, and you drive under it.  LABELLED:
         * the height at which it stops touching is ours - the measurement
         * (NOTES 152) gives the motion, not the hit box.  Half the resting
         * height is the reading that lets a kart through the gap without
         * letting it through a Thwomp on the floor. */
        if (smk_mover_z(crs, i) > SMK_MOVER_PARK / 2) continue;
        float d = sqrtf((float)d2);
        float nx2 = (float)dx / d, ny2 = (float)dy / d;
        float dot = (float)k->vx * nx2 + (float)k->vy * ny2;
        if (dot < 0.0f) {
            k->vx = (int16_t)((float)k->vx - 2.0f * dot * nx2);
            k->vy = (int16_t)((float)k->vy - 2.0f * dot * ny2);
            k->vx /= 2;
            k->vy /= 2;
            k->speed = (int16_t)(k->speed * 308 / 581);
            /* The low-speed FLOOR, which objects were missing.
             *
             * A wall does not scale the bounce when the kart is barely
             * moving: $80F9C1 forces each component to +-$100, which is
             * why the wall push-back "is constant no matter the speed"
             * (user, NOTES 133).  Without the same floor an object hit at
             * a crawl returns a crawl, the kart re-touches within a few
             * frames, and it is glued there - which is the Thwomp the
             * user could not shove away from (NOTES 150).  Here the sign
             * is the contact NORMAL, so the shove is away from the thing
             * that was hit. */
            int ax = k->vx < 0 ? -k->vx : k->vx;
            int ay = k->vy < 0 ? -k->vy : k->vy;
            if (ax < 0xC0 && ay < 0xC0) {
                float kick = (float)obj_kick();
                k->vx = (int16_t)(nx2 * kick);
                k->vy = (int16_t)(ny2 * kick);
                k->speed = vec_len_pub(k->vx, k->vy);
            }
            k->bounce_cool = 10;
            k->bounce_obj = 1;      /* this window expires in the air too */
        }
        /* And say so in the kart's own state, which is what was missing.
         *
         * The bounce used to live only in vx/vy, and player.c rebuilds
         * those from $A2 (= $A4 + $A8) the frame the ballistic window
         * ends - so the reflection was erased one frame after it was
         * applied, the kart resumed driving at whatever it had hit, and
         * bounced off it again for ever.  That is the "got stuck on a
         * Thwomp and could not shove free" report (NOTES 150).
         *
         * The game does not leave it in the velocity either: an impact
         * sets $10 bit $1000, and the next update runs $80A0AF/$80A0C7,
         * which push the kart's ACTUAL velocity through the arctangent
         * ($81F638) and write the result to $A2 - directly, or as the
         * slip $A8 with drive state $16.  So the direction survives the
         * window, and the kart slides away from what it hit while still
         * pointing at it.  Same shape as the wall response in kart.c. */
        if (dot < 0.0f) {
            k->crash_lag = (int16_t)((smk_angle_of(k->vx, k->vy) & 0xFF00)
                                     - k->angle);
            k->crash_frames = 3;
        }
        float push = ((float)SMK_OBJ_RADIUS - d) + 1.0f;
        k->x += (int32_t)(nx2 * push * SMK_POS_ONE);
        k->y += (int32_t)(ny2 * push * SMK_POS_ONE);
    }
}

/* $81EE07..$81EE58: rows of 8 characters at $81:EE97, 16 bytes per
 * character; 1P mode uses P1's row, 2P mode P2's ($81EE72 by $2E).  In
 * 1P mode kart $1100 gets the row's 7th entry (the rival, $81EE78); then
 * karts $1700 down to $1200 take the row's entries in order, skipping the
 * two humans' characters. */
#define T_GRID_ROWS 0x81EE97u
void smk_grid_order(const smk_rom *rom, int p1, int p2, bool two_players, int out[8])
{
    int row_char = two_players ? p2 : p1;
    uint32_t row = smk_snes_to_pc(rom, T_GRID_ROWS + (uint32_t)row_char * 16u);
    int list[8];
    for (int i = 0; i < 8; i++) list[i] = rom->data[row + (uint32_t)i * 2u] / 2;
    if (!two_players) p2 = list[6];
    out[0] = p1;
    out[1] = p2;
    int slot = 7;
    for (int i = 0; i < 8 && slot >= 2; i++) {
        if (list[i] == p1 || list[i] == p2) continue;
        out[slot--] = list[i];
    }
}

void smk_racer_start(smk_racer *r, const smk_course *crs, int slot)
{
    float x, y;
    uint16_t heading;
    memset(r, 0, sizeof *r);
    r->character = slot;
    smk_course_start(crs, slot, &x, &y, &heading);
    r->k.x = (int32_t)(x * SMK_POS_ONE);
    r->k.y = (int32_t)(y * SMK_POS_ONE);
    r->k.angle = heading;
    r->sector = crs->sectors - 1;         /* the grid sits in the last sector */
}

static uint16_t heading_to(const smk_kart *k, int tx, int ty)
{
    float dx = (float)(tx - smk_kart_px(k->x));
    float dy = (float)(ty - smk_kart_px(k->y));
    /* game convention: 0 = -Y, clockwise */
    return (uint16_t)(atan2f(dx, -dy) * (float)SMK_ANGLE_TURN
                      / (2.0f * (float)M_PI));
}

#define AI_SNAP      0x0200      /* $80AFBE snaps inside this            */

void smk_racer_step(smk_racer *r, const smk_track *trk,
                       const smk_course *crs, const smk_physics *phys)
{
    uint8_t cell = smk_course_cell(crs, smk_kart_px(r->k.x), smk_kart_px(r->k.y));
    int sec = cell & SMK_SECT_OFF;
    /* DECODED ($808962): keep the old sector when off-course ($7F), and
     * while airborne reject sectors whose waypoint attribute has bit 7 set
     * - the anti-shortcut rule for jump zones. */
    if (sec != SMK_SECT_OFF && sec < crs->sectors
        && !(r->k.airborne && (crs->wattr[sec] & 0x80))) {
        /* DECODED ($8089B6/$8089ED): the lap lives in the high byte of the
         * kart's progress word - crossing the line forward does
         * `+$0100, and #$FF00`; crossing backward subtracts it; and $F8,x
         * keeps the maximum progress so a lap only counts when it exceeds
         * everything seen before.  We keep lap and sector as fields and
         * apply the same wrap and guard. */
        /* the crossing only counts ON the strip ($808994 is called from
         * the strip-accept path), forward guarded by max progress.  The
         * strip holds paint of BOTH ends of the loop, so the sector can
         * oscillate across one transit; without a cooldown that fired
         * +1 then an unguarded -1 and left the counter locked (NOTES 055).
         * One lap event per transit. */
        if (r->lap_cool > 0) r->lap_cool--;
        if ((cell & SMK_SECT_FINISH) && r->lap_cool == 0) {
            if (r->sector != sec) r->esc_len = 0;
        /* progress for the rescue timer = monotonic max only, or the two
         * stuck loops that oscillate between adjacent sectors reset it */
        {
            int prog2 = (r->lap << 8) | sec;
            if (prog2 > r->rescue_max) { r->rescue_max = prog2; r->no_prog = 0; }
        }
            if (r->sector >= crs->sectors - 2 && sec <= 1) {
                int prog = ((r->lap + 1) << 8) | sec;
                if (prog > r->progress_max) {
                    r->lap++;
                    r->progress_max = prog;
                    r->lap_cool = 90;
                }
            } else if (sec >= crs->sectors - 2 && r->sector <= 1) {
                r->lap--;
                r->lap_cool = 90;
            }
        }
        r->sector = sec;
    }

    /* DECODED steering ($80B0B1 / NOTES 056): on course the AI's target
     * angle is the flow field byte for its cell - atan2 to a waypoint is
     * only the OFF-COURSE recovery path in the ROM, and treating it as the
     * main rule was why our karts clipped corners into walls. */
    int fcell = ((smk_kart_px(r->k.y) >> 4) & 63) * 64
              + ((smk_kart_px(r->k.x) >> 4) & 63);
    int fsec = crs->map[fcell] & SMK_SECT_OFF;
    uint16_t want;
    if (fsec != SMK_SECT_OFF && crs->map[fcell] != 0) {
        want = (uint16_t)(crs->flow[fcell] << 8);
    } else {
        int next = r->sector + 1;
        if (next >= crs->sectors) next = 0;
        want = heading_to(&r->k, crs->wx[next], crs->wy[next]);
    }
    /* Stuck against a sticky wall (labelled AI behaviour, not a decode):
     * the ROM's karts bounce free, ours stop - so scan eight compass
     * directions for the most open ground, take it, and HOLD it briefly;
     * without the hold the slew dragged the kart straight back into the
     * wall before it could move (NOTES 057). */
    if (r->k.speed > 300) r->was_fast = 1;
    /* Lakitu: the game fishes a stuck or fallen kart back onto the track.
     * Ten seconds without sector progress -> set down at the sector's own
     * waypoint, facing the next one.  (The real trigger and animation are
     * not decoded; the rescue itself is the game's own behaviour.) */
    if (++r->no_prog > 600) {
        int nx2 = r->sector + 1;
        if (nx2 >= crs->sectors) nx2 = 0;
        r->k.x = (int32_t)crs->wx[r->sector] << 16;
        r->k.y = (int32_t)crs->wy[r->sector] << 16;
        r->k.angle = heading_to(&r->k, crs->wx[nx2], crs->wy[nx2]);
        r->k.speed = 0;
        r->k.vx = r->k.vy = 0;
        r->k.airborne = false;
        r->no_prog = 0;
        r->esc_len = 0;
        r->escape = 0;
    }
    /* a kart pinned nearly square against a wall keeps its speed (the
     * proportional graze loss is ~0) while its position only crawls
     * sub-pixel - so stagnation, not low speed, is the reliable trigger */
    {
        int px = smk_kart_px(r->k.x), py = smk_kart_px(r->k.y);
        if (px == r->last_px && py == r->last_py) r->still++;
        else { r->still = 0; r->last_px = px; r->last_py = py; }
    }
    if (r->escape > 0) {
        r->escape--;
    } else if (((r->k.speed < 100 && r->was_fast) || r->still > 40)) {
        r->slow_frames += (r->still > 40) ? 31 : 1;
        if (r->slow_frames > 30) {
            int best_d = -1, best_score = -1000;
            for (int d = 0; d < 8; d++) {
                float a = (float)d * (float)M_PI / 4.0f;
                int open = 0;
                static const int STEPS[7] = { 2, 4, 8, 16, 24, 32, 40 };
                for (int si = 0; si < 7; si++) {
                    int step = STEPS[si];
                    int sx = smk_kart_px(r->k.x) + (int)(sinf(a) * step);
                    int sy = smk_kart_px(r->k.y) - (int)(cosf(a) * step);
                    if (smk_surface_solid(smk_track_surface(trk, sx, sy)))
                        break;
                    open++;
                }
                /* prefer open ground, break ties toward the flow direction
                 * so the escape makes forward progress */
                int16_t da = (int16_t)((uint16_t)(d * 0x2000) - want);
                int align = 4 - (abs((int)da) >> 12);       /* 4..-4 */
                int score = open * 8 + align;
                if (score > best_score) { best_score = score; best_d = d; }
            }
            r->k.angle = (uint16_t)(best_d * 0x2000);
            r->k.speed = 300;
            /* escalate on consecutive triggers: deep pockets need longer
             * runs before the flow field is allowed to pull again */
            r->esc_len = r->esc_len ? (r->esc_len * 2 > 120 ? 120
                                       : r->esc_len * 2) : 25;
            r->escape = r->esc_len;
            r->slow_frames = 0;
            if (r->still > 120) {
                /* wedged in a concave notch: no heading can move it, so
                 * step the position out directly (labelled last resort) */
                float ea = (float)best_d * (float)M_PI / 4.0f;
                r->k.x += (int32_t)(sinf(ea) * 3.0f * SMK_POS_ONE);
                r->k.y -= (int32_t)(cosf(ea) * 3.0f * SMK_POS_ONE);
                r->still = 0;
            }
        }
    } else
        r->slow_frames = 0;
    int16_t diff = (int16_t)(want - r->k.angle);
    if (r->escape > 0) diff = 0;             /* hold the escape heading */
    if (diff > AI_SNAP || diff < -AI_SNAP) {
        uint16_t err = (uint16_t)(diff > 0 ? diff : -diff);
        /* DECODED ($80AFF9): turn amount from the physics blob's words 32+,
         * indexed by heading error; the demo AI uses row 8 ($C8 = 8).
         * MEASURED (NOTES 043): above ~90 degrees of error the AI turns at
         * $800 per frame - a fast turnaround, not a table step. */
        uint16_t step = err > 0x4000 ? 0x800 : smk_physics_turn(phys, err, 8);
        r->k.angle += (uint16_t)(diff > 0 ? step : -(int)step);
    } else {
        r->k.angle = want;
    }

    /* DECODED ($80B074): the target speed row is selected by the sector
     * waypoint attribute's low two bits, offset by the kart's $C8 row.
     * The attract demo runs its AI at row +4 (700-1050), which outruns the
     * player at every engine class (user report) - presumably the demo's
     * difficulty, with rubber-banding undecoded.  Race AI uses row +0, the
     * same rows the player's class selects, so 50/100/150cc scale both. */
    int target = (int16_t)phys->w[SMK_PHYS_TARGET + (crs->wattr[r->sector] & 3)];
    /* DECODED ($80A701 structure): off-road surfaces cap the speed and the
     * over-cap decel row applies.  Cap values are measured (NOTES 053). */
    {
        uint8_t sv = smk_track_surface(trk, smk_kart_px(r->k.x),
                                       smk_kart_px(r->k.y));
        /* the real AI ignores surfaces (rubber-band cheat, NOTES 057);
         * we apply a softened measured cap so the field stays honest but
         * competitive - labelled behaviour */
        int frac = smk_surface_cap_frac(sv);
        if (frac < 800) {
            int cap = (int)phys->w[SMK_PHYS_TARGET + 3] * (frac + 200) / 1000;
            if (target > cap) target = cap;
        }
    }
    int32_t accel;
    if (r->k.speed < target)
        accel = (int32_t)smk_physics_accel(phys, r->k.speed) << 8;
    else
        accel = -((int32_t)0x0400 << 8);
    r->k.accel = (int16_t)(accel >> 16);
    r->k.accel_frac = (uint16_t)(accel & 0xFFFF);
    smk_kart_accelerate(&r->k);
    if (r->k.speed > target) r->k.speed = (int16_t)target;
    smk_kart_face(&r->k);
    smk_kart_gravity(&r->k);
    smk_kart_move(&r->k, trk);
    smk_collide_objects(&r->k, crs);
}
