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
     * (Thwomps, moles) run the measured fall/hold/rise cycle in
     * smk_course_movers_step (NOTES 152); only the RISE duration is
     * labelled. */
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
        if (i < 0) continue;                 /* an empty block */
        int dx = kx - (int)crs->ent[i].x, dy = ky - (int)crs->ent[i].y;
        int d2 = dx * dx + dy * dy;
        if (d2 >= SMK_OBJ_RADIUS * SMK_OBJ_RADIUS || d2 == 0) continue;
        /* A raised mover is overhead, and you drive under it.  The height
         * is SMK_MOVER_CLEAR - OURS, ledgered, and set from the user's own
         * rule after seven rigs failed to measure the game's (NOTES 176).
         * It is validated against their recorded run rather than guessed:
         * every crash in it was below the line and every close pass above. */
        /* Bug 13, retimed (round 2, bug 11: "it gets triggered too soon,
         * the thwomp is still in top altitude.  It should be on contact"):
         * the squash fires only in the CONTACT band of the fall; higher
         * up, a descending block overhead is neither a wall nor a hit -
         * you cannot bounce off its underside - so it is skipped until it
         * reaches you.  Checked BEFORE the overhead skip. */
        if (!crs->dead[i] && !k->star && smk_theme_has_movers(crs->theme)
            && i < 32 && crs->mv[i].phase == SMK_MV_FALL) {
            if (smk_mover_z(crs, i) < SMK_SQUASH_Z) k->hazard_hit = 2;
            continue;
        }
        if (smk_mover_z(crs, i) > SMK_MOVER_CLEAR) continue;
        if (crs->dead[i]) continue;
        if (k->star) {                /* OURS: a starred kart knocks it out */
            ((smk_course *)crs)->dead[i] = 1;
            continue;
        }
        /* Choco Island's piranha plants and Koopa Beach's cheep-cheeps are
         * not walls: "if you touch it, it triggers the spinning animation"
         * (the user).  The kart reports the touch; the driver spins. */
        /* Donut Plains: the MOLE (bug 12).  Underground it is nothing at
         * all; popped, it LATCHES onto the kart - hazard kind 3; the
         * driver code attaches it.  The AI ignores it (OURS). */
        if (crs->theme == 2) {
            if (smk_mole_step(smk_obj_ticks, i) > 0) k->hazard_hit = 3;
            continue;
        }
        if (crs->theme == 3 || crs->theme == 5) { k->hazard_hit = 1; continue; }
        /* Rainbow Road's Thwomps spin you AND stand like a rock (round 2,
         * bug 9: "you can pass-through thwomps, they are not a rock as
         * THEY SHOULD BE") - flag the spin and fall through to the
         * measured bounce.  Plants and fish stay pass-through. */
        if (crs->theme == 7) k->hazard_hit = 1;
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
            /* ...and $10 bit 12, as a wall raises it: in the user's crash
             * recording every pipe hit goes to drive state $16, halves the
             * rev with the speed and fires $3C then $3F - the wall's own
             * pair - 13 of 13 (NOTES 287).  The port's pipes and Thwomps
             * cost speed but were silent and left the engine screaming. */
            k->bounce_hit = 1;
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
    r->finish_frame = -1;                 /* has not finished */
    r->place = 0;
    r->coins = 2;                         /* LABELLED: $81E3DA is read for the two
                                           * human-slot karts only ($81E3B8); where
                                           * an AI keeps coins is not decoded */
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
    /* The lap rule is ONE implementation, in src/course.c: the AI, the
     * player and the RL environment all step the same decoded code.  It
     * used to be written out here and again in main.c, so the AI
     * regression could pass while the player's lap counting was broken.
     * All that stays here is what is the AI's own: its finish frame. */
    if (smk_progress_step(r, crs, &r->k) > 0) {
        /* the last crossing is this kart's finish */
        if (r->lap >= SMK_RACE_CROSSINGS && r->finish_frame < 0)
            r->finish_frame = smk_race_frame;
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
        /* $80B0E8 reads a WORD at $7F:3FFF + cell: the cell's own byte
         * is the high half and its LEFT neighbour's the low half, so the
         * target angle carries a sub-step from the cell beside it rather
         * than being quantised to 256 directions.  The port had only the
         * high byte, which is why our karts sat on a coarser staircase
         * than the game's.  (The rescue path in main.c already read it
         * this way.) */
        want = (uint16_t)((crs->flow[fcell] << 8)
                          | crs->flow[(fcell - 1) & (SMK_SECT_CELLS - 1)]);
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
    /* The rescue trigger, and it has to answer three cases at once
     * (NOTES 169).  The old rule - ten seconds without beating your own
     * best sector - fished up karts that were driving perfectly: one
     * backward excursion, a spin or a corner cut into an earlier
     * sector's paint, parks the watermark and a healthy kart is
     * teleported.  Measured on track 4: rescued at frame 1308 doing 362
     * in sector 14.  That is the user's "they disappear and re-appear a
     * few meters further".
     *
     * But dropping the watermark for "did the sector change" reopens the
     * hole NOTES 057 needed it for: two karts circling between adjacent
     * sectors reset any such timer for ever.
     *
     * NET DISPLACEMENT over the window answers all three.  A kart going
     * somewhere has moved; a wedged one has not; and a circling one
     * comes back to where it started.  Ours, and labelled - the ROM's
     * own trigger is still not decoded. */
    if (++r->no_prog >= SMK_AI_RESCUE_FRAMES) {
        int adx = smk_kart_px(r->k.x) - r->anchor_x;
        int ady = smk_kart_px(r->k.y) - r->anchor_y;
        if (adx >  512) adx -= 1024;
        if (adx < -512) adx += 1024;
        if (ady >  512) ady -= 1024;
        if (ady < -512) ady += 1024;
        if (adx * adx + ady * ady <= SMK_AI_STUCK_PX * SMK_AI_STUCK_PX) {
            /* Lakitu: set it down at its sector's own waypoint, facing
             * the next one.  The animation is still missing (S12). */
            int nx2 = r->sector + 1;
            if (nx2 >= crs->sectors) nx2 = 0;
            r->k.x = (int32_t)crs->wx[r->sector] << 16;
            r->k.y = (int32_t)crs->wy[r->sector] << 16;
            r->k.angle = heading_to(&r->k, crs->wx[nx2], crs->wy[nx2]);
            r->k.speed = 0;
            r->k.vx = r->k.vy = 0;
            r->k.airborne = false;
            r->esc_len = 0;
            r->escape = 0;
        }
        r->no_prog = 0;
        r->anchor_x = smk_kart_px(r->k.x);
        r->anchor_y = smk_kart_px(r->k.y);
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
    r->dbg_want = want;
    int16_t diff = (int16_t)(want - r->k.angle);
    if (r->escape > 0) diff = 0;             /* hold the escape heading */
    /* MEASURED (NOTES 280): a flying AI kart holds its heading - DK Jr's
     * Mario Circuit 2 flight is a straight line from the ramp to the
     * landing - so nothing steers it in the air */
    if (r->k.airborne) {
        /* keep the angle */
    } else if (diff > AI_SNAP || diff < -AI_SNAP) {
        uint16_t err = (uint16_t)(diff > 0 ? diff : -diff);
        /* DECODED ($80AFF9): the turn amount is the physics blob's word at
         * $C8 + 7 - and $80AFBE only ever reaches it with a value the
         * routine clamps to $01FF, so the rate is a CONSTANT per row, not
         * a function of the error.  MEASURED (NOTES 043): above ~90
         * degrees of error the AI turns at $800 per frame instead.
         *
         * The row is the ROM's $C8, and passing OUR $C8 (r->row * 2) was
         * tried: it costs track 15, where a kart can no longer get round
         * (NOTES 256).  Our rubber-band row is a model that scores 94.2%
         * against the game's own logged rows (NOTES 174), not the row
         * itself, so feeding it into the steering propagates its 6% into
         * a place that was a fixed constant and worked.  LABELLED: row 8
         * stays until $C8 is exact. */
        uint16_t step = err > 0x4000 ? 0x800
                      : smk_physics_turn(phys, err, 8);
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
    /* $80B074: the waypoint attribute picks the entry, the rubber band's
     * row picks the group, and $80B086 adds the flat per-rank correction
     * from $B0A1 (NOTES 167).  The port used to pass row 0 always, which
     * is the leader's ease-off row - so every AI kart drove as if it were
     * comfortably in front, and none of them ever chased. */
    int row = r->row;
    if (row < 0 || row > SMK_AI_ROW_SLOW) row = SMK_AI_ROW_HOLD;
    int target = (int16_t)phys->w[SMK_PHYS_TARGET + (crs->wattr[r->sector] & 3)
                                  + row];
    /* $80B086: a handicap kart ($DA non-zero) takes $B099's BONUS by its
     * $DA and never the rank penalty; the others take $B0A1 by rank.  The
     * port used to penalise all eight (NOTES 277). */
    if (r->da > 0 && SMK_AI_DECEL[0] != 0)
        target += smk_ai_da_bonus(r->da);
    else
        target += SMK_AI_RANK_BONUS[r->rank & 7];
    /* DECODED ($80A701 structure): off-road surfaces cap the speed and the
     * over-cap decel row applies.  Cap values are measured (NOTES 053). */
    bool boosting = false;
    {
        uint8_t sv = smk_track_surface(trk, smk_kart_px(r->k.x),
                                       smk_kart_px(r->k.y));
        /* THE BOOST PAD, class $16 ($80B47B): $FC = $20, and $80A5E4 then
         * adds $32 a frame up to $7E0 while it counts.  The player had it;
         * the field did not, which is why the original's karts reach Mario
         * Circuit 2's ramp at 1300 and ours at 900 (NOTES 280: DK Jr 901 ->
         * 1007 -> 1330 -> 1380 across the strip, in the user's recording). */
        if ((sv & 0xFE) == 0x16 && !r->k.airborne && r->hit_t == 0) r->boost_t = SMK_BOOST_PAD_T;
        /* $80B015 -> $80A5E4: the AI's boost branch runs only in a sector
         * whose waypoint attribute is 3 (the fast ones), and never in the
         * air.  MEASURED on DK Jr (NOTES 280): $FC re-armed at 32 on the
         * pad and stepping down, frozen at 30 through the flight and
         * after the landing in an attribute-2 sector, where the normal
         * decel took over. */
        /* ...and the flight ends the boost STATE ($AC): after the landing
         * DK Jr's $FC still read 30 in an attribute-3 sector and the normal
         * decel ran, so the pad's state does not survive a launch */
        if (r->k.airborne) r->boost_t = 0;
        if (r->boost_t > 0 && (crs->wattr[r->sector] & 3) == 3) {
            r->boost_t--;
            boosting = true;
        }
        /* the real AI ignores surfaces (rubber-band cheat, NOTES 057);
         * we apply a softened measured cap so the field stays honest but
         * competitive - labelled behaviour */
        int frac = smk_surface_cap_frac(sv);
        if (frac < 800) {
            /* FAIR: the player's own cap, unsoftened (NOTES 281) */
            int soft = smk_cpu_rules == SMK_CPU_FAIR ? 0 : 200;
            int cap = (int)phys->w[SMK_PHYS_TARGET + 3] * (frac + soft) / 1000;
            if (target > cap) target = cap;
        }
    }
    /* hit by an item (docs/ITEMS.md §6): the tumble, on the racer.  The
     * pose spins at the $E4 rate decaying $40 a frame, the speed falls 56
     * a frame to nothing, and the AI does not drive until it is over. */
    if (r->hit_t > 0) {
        r->hit_t--;
        int rate = r->tumble > 0x1000 ? 0x1000 : r->tumble;
        r->spin_pose = (int16_t)(r->spin_pose + (r->hit_dir ? rate : -rate));
        int off = 56;
        if (r->hit_kind == 4) off = (r->hit_t & 1) ? 16 : 15;    /* 15.5 a frame, flat rate */
        else r->tumble = (int16_t)(r->tumble > 0x40 ? r->tumble - 0x40 : 0);
        if (r->hit_t == 0) r->spin_pose = 0;
        r->k.speed = (int16_t)(r->k.speed > off ? r->k.speed - off : 0);
        r->k.accel = 0; r->k.accel_frac = 0;
        smk_kart_face(&r->k);
        smk_kart_gravity(&r->k);
        smk_kart_move(&r->k, trk);
        r->k.hazard_hit = 0; smk_collide_objects(&r->k, crs);
        if (r->k.hazard_hit == 2) r->squash_t = SMK_SQUASH_T;
        else if (r->k.hazard_hit == 1) smk_racer_hit(r, 1, 0);
        return;
    }
    if (r->squash_t > 0) { r->squash_t--; target = 0; }   /* bug 13: flattened, going nowhere */
    if (r->shrink_t > 0) { r->shrink_t--; if (target > 0x200) target = 0x200; }   /* OURS: small is slow */
    /* $80B035: below the target, the accel curve by speed ($0690, the
     * player's own); above it, $80B04B's rate by the gap - clamped to
     * $1FF, banded by 64 into $80B064: -4, -8, -16, -24 a frame - and
     * NO clamp to the target.  The port used to snap the speed down to
     * the target in one frame, which is where the field lost its pace:
     * the original never loses 60 in a frame, ours lost 100 ten times
     * per thousand kart-frames (NOTES 277). */
    int32_t accel;
    if (r->k.airborne) {
        accel = 0;                                  /* the air holds the speed */
    } else if (boosting) {
        /* $80A5E4: the boost's own throttle, +$32 a frame to $7E0 */
        int s = r->k.speed + SMK_BOOST_STEP;
        if (s > SMK_BOOST_CAP) s = SMK_BOOST_CAP;
        accel = (int32_t)(s - r->k.speed) << 16;
    } else if (r->k.speed < target)
        accel = (int32_t)smk_physics_accel(phys, r->k.speed) << 8;
    else {
        int gap = r->k.speed - target;
        if (gap > 0x1FF) gap = 0x1FF;
        int16_t rate = SMK_AI_DECEL[0] ? SMK_AI_DECEL[(gap >> 7) & 3] : -4;
        accel = (int32_t)rate << 16;
    }
    r->k.accel = (int16_t)(accel >> 16);
    r->k.accel_frac = (uint16_t)(accel & 0xFFFF);
    smk_kart_accelerate(&r->k);
    if (target <= 0 && r->k.speed < 0) r->k.speed = 0;
    /* SMK_AI_SNAP=1: the old one-frame snap to target, for A/B rigs only */
    { static int snap = -1; if (snap < 0) snap = getenv("SMK_AI_SNAP") != NULL;
      if (snap && r->k.speed > target) r->k.speed = (int16_t)target; }
    smk_kart_face(&r->k);
    smk_kart_gravity(&r->k);
    smk_kart_move(&r->k, trk);
    r->k.hazard_hit = 0; smk_collide_objects(&r->k, crs);
    if (r->k.hazard_hit == 2) r->squash_t = SMK_SQUASH_T;
    else if (r->k.hazard_hit == 1) smk_racer_hit(r, 1, 0);
}

/* ---- The rubber band (NOTES 167) -------------------------------------- */

/* $80AF0F, read from the ROM rather than transcribed, because five of
 * its eight rows are data and the rest are the bytes of $80AF5F. */
uint16_t SMK_AI_CATCHUP[SMK_AI_SKILLS][8];

/* $80B064: how fast an AI sheds speed it is above its target - by the
 * gap, clamped to $1FF and banded by 64: -4, -8, -16, -24 a frame.  The
 * table has eight words; the clamp reaches only the first four.  READ
 * IN PLAY (NOTES 278): the original AI's negative speed steps are exactly
 * 4, 8, 16 and 24 in all three recordings, and never more. */
int16_t SMK_AI_DECEL[4];
/* $80B099: the correction for a kart carrying a $DA handicap, by $DA -
 * 2, 4, 8, 16, 0 - which REPLACES the rank penalty for karts 4-7
 * ($80B086: `ldy $DA,x / beq rank-path / adc $B099,y`).  Confirmed to
 * the unit by the four karts' maxima in the recordings (NOTES 277). */
int16_t SMK_AI_DA_BONUS[5];

bool smk_ai_catchup_load(const smk_rom *rom)
{
    uint32_t a = smk_snes_to_pc(rom, 0x80AF0Fu);
    if (a + SMK_AI_SKILLS * 16u > rom->size) return false;
    for (int c = 0; c < SMK_AI_SKILLS; c++)
        for (int i = 0; i < 8; i++) {
            uint32_t o = a + (uint32_t)(c * 16 + i * 2);
            SMK_AI_CATCHUP[c][i] =
                (uint16_t)(rom->data[o] | rom->data[o + 1] << 8);
        }
    uint32_t d = smk_snes_to_pc(rom, 0x80B064u), b = smk_snes_to_pc(rom, 0x80B099u);
    if (d + 8 > rom->size || b + 10 > rom->size) return false;
    for (int i = 0; i < 4; i++)
        SMK_AI_DECEL[i] = (int16_t)(rom->data[d + i * 2] | rom->data[d + i * 2 + 1] << 8);
    for (int i = 0; i < 5; i++)
        SMK_AI_DA_BONUS[i] = (int16_t)(rom->data[b + i * 2] | rom->data[b + i * 2 + 1] << 8);
    return true;
}

const int16_t SMK_AI_RANK_BONUS[8] =             /* $80B0A1 */
    { 0, -2, -4, -8, -12, -16, -20, -24 };

/* $80AEFC: the catch-up distance for a kart, by ITS skill and a rank.
 * The index is ($C1 & 7) * 16 + rank * 2 bytes into $80AF0F, and the
 * original lets that run past the table - see SMK_AI_CATCHUP. */
static uint16_t catchup_of(const smk_racer *of, int rank)
{
    int i = ((of->skill & 7) << 3) + rank;      /* in WORDS */
    return SMK_AI_CATCHUP[i >> 3][i & 7];
}

/* $80AF5F: the DSP-1's vector length between two karts.  Straight-line
 * distance in world units - $80AF5F feeds command $28 the coordinate
 * differences and reads the length back, and a recorded race confirms it
 * against the game's own cached $92 to a median of 3.4 units. */
static float kart_dist(const smk_racer *a, const smk_racer *b)
{
    float dx = (float)(smk_kart_px(a->k.x) - smk_kart_px(b->k.x));
    float dy = (float)(smk_kart_px(a->k.y) - smk_kart_px(b->k.y));
    return sqrtf(dx * dx + dy * dy);
}

/* Which branch of $80ADA0 last answered, so a headless race can be
 * attributed the same way the recorded one is (SMK_ROW_TRACE). */
int smk_ai_branch = 0;
#define BR(n, v) do { smk_ai_branch = (n); return (v); } while (0)

/* $80AD96 -> $80ADA0: which speed row a kart drives on this frame.
 *
 * This is the rubber band, and the thing that makes it one is $10 bit 15:
 * THE HUMAN PLAYER.  Every branch below asks whether the kart ahead or
 * behind is the player, and picks the row from the answer - which is why
 * "no matter how fast I go, I always have someone pretty close chasing"
 * (the user) and why an AI field with no such test gets left behind.
 *
 * The rows are not named after speeds here, because their speeds are not
 * a property of the row but of the physics block: $D6 comes from
 * w[16 + (surface & 3) + row], and measured there the order is
 * $08 > $00 > $10 > $18 in every class (NOTES 174).  $00 is a FAST
 * cruising row, not the ease-off this port used to treat it as.
 *
 * MEASURED against a recorded race: this function reproduces the game's
 * own $C8 on 94.7% of 39,074 kart-frames.  The residue is the distance
 * CACHE - $80AEBC and $80AECF reuse $92/$90 when $96/$94 still names the
 * same partner, so the original sometimes tests a distance a few frames
 * old, and this always tests a fresh one.  LABELLED, not modelled.
 *
 * LABELLED and not ported: the $E2 bit 1 policy at $80ADC0, which never
 * ran in a one-player race (0 frames of 5582), and $0E50, whose non-zero
 * value forces row $00 on every kart and was likewise never seen.
 */
int smk_ai_row_for(const smk_racer *r, const smk_racer *ahead,
                   const smk_racer *behind, const smk_racer *third,
                   int *s04, int *s06)
{
    if (r->trouble) BR(1, SMK_AI_ROW_SLOW);                 /* $80ADB0 */
    int p00 = r->da;                        /* $80AD9F: lda $DA,x */

    if (r->rank == 0) {                                     /* $80ADE0 */
        if (!behind) BR(2, SMK_AI_ROW_HOLD);
        if (behind->is_player)
            BR(3, kart_dist(r, behind) >= 0x140 ? SMK_AI_ROW_HOLD
                                                : SMK_AI_ROW_EASE);
        if (p00 < behind->da) BR(4, SMK_AI_ROW_HOLD);
        if ((r->skill & 7) == 0) BR(5, SMK_AI_ROW_CHASE);
        if (kart_dist(r, behind) >= catchup_of(r, 0)) BR(6, SMK_AI_ROW_EASE);
        /* $80AE0F/$80AE16: the second test reads X but branches on the
         * FIRST test's flags - the original's own dead code, kept. */
        BR(7, (third && third->is_player) ? SMK_AI_ROW_CHASE
                                         : SMK_AI_ROW_EASE);
    }
    if (r->rank == 7) {                                     /* $80AE23 */
        if (!ahead) BR(8, SMK_AI_ROW_HOLD);
        if (ahead->is_player)
            BR(9, kart_dist(r, ahead) < 0x80 ? SMK_AI_ROW_EASE
                                            : SMK_AI_ROW_CHASE);
        if (ahead->da < p00) BR(10, SMK_AI_ROW_CHASE);
        BR(11, kart_dist(r, ahead) < catchup_of(ahead, r->rank)
                   ? SMK_AI_ROW_HOLD : SMK_AI_ROW_EASE);
    }
    if (behind && behind->is_player) {                      /* $80AE4C */
        if (ahead) {
            if (ahead->row == SMK_AI_ROW_HOLD) BR(12, SMK_AI_ROW_HOLD);
            if (ahead->da >= p00) BR(13, SMK_AI_ROW_HOLD);
        }
        if (kart_dist(r, behind) < 0x140) BR(14, SMK_AI_ROW_EASE);
        /* $80AE6A reads $04 without this call having written it - it is
         * whatever the LAST kart's pass left there.  Kept deliberately. */
        BR(15, *s04 >= p00 ? SMK_AI_ROW_EASE : SMK_AI_ROW_HOLD);
    }
    if (!behind || !ahead) BR(16, SMK_AI_ROW_HOLD);
    *s06 = behind->da;                                      /* $80AE79 */
    if (ahead->is_player) {
        if (p00 < *s06) BR(17, SMK_AI_ROW_HOLD);
        BR(18, kart_dist(r, ahead) >= 0x80 ? SMK_AI_ROW_CHASE
                                          : SMK_AI_ROW_EASE);
    }
    *s04 = ahead->da;                                       /* $80AE97 */
    if (p00 < *s06) BR(19, SMK_AI_ROW_HOLD);
    if (*s04 < p00) BR(20, SMK_AI_ROW_CHASE);
    if (kart_dist(r, ahead) < catchup_of(ahead, r->rank)) BR(21, SMK_AI_ROW_HOLD);
    BR(22, ahead->row);      /* $80AEB9: adopt the row of the kart ahead */
}

/* $DA, static per kart block for the whole race.  MEASURED from a
 * recorded race, where every kart held one value from the first frame to
 * the last.  LABELLED: where the game writes them is not decoded. */
static const int SMK_AI_DA[8] = { 0, 0, 0, 0, 2, 4, 6, 8 };

/* Which racers[] slot is the human.  $10 bit 15 in the game; block 0
 * here, which is what main.c puts the player in. */
int smk_ai_player_block = 0;
long smk_race_frame = 0;

/* Forces every AI onto one $80AF0F row.  -1, the default, uses the
 * decoded answer instead - the LAP.  Only useful for sweeping the
 * alternatives with tools/rowcheck. */
int smk_ai_skill = -1;
int smk_cpu_rules = SMK_CPU_ORIGINAL;

int smk_ai_da_bonus(int da)
{
    int di = da >> 1;
    int b = SMK_AI_DA_BONUS[di > 4 ? 4 : di];
    return smk_cpu_rules == SMK_CPU_FAIR ? b / 2 : b;
}


void smk_ai_rubber(smk_racer *racers, int n, const smk_course *crs, int cls)
{
    /* $80AF0F is indexed by the kart's own LAP, not by the engine class.
     * Indexing it by class is what this port did before, and it is
     * wrong: it made the thresholds constant for a whole race. */
    (void)cls;
    for (int i = 0; i < n; i++) {
        racers[i].is_player = (i == smk_ai_player_block);
        racers[i].da        = SMK_AI_DA[i & 7];
        /* $C1 is the LAP.  $80:89B6 adds $0100 to the word at $C0 and
         * stores it immediately before `cmp $F8,x`, the progress
         * watermark; in a recorded race every kart's high byte walks
         * $7F -> $80 -> $81 ... one step at a time and never back.  $7F
         * is the line not yet crossed, so the byte is $7F + laps, and
         * `and #$0007` indexes $80AF0F BY THE LAP: the catch-up
         * distances are re-tuned every lap, and lap one lands on row 7 -
         * past the table, in the code of $80AF5F - so early chasing
         * comes only from $DA and from adopting the row ahead. */
        racers[i].skill     = smk_ai_skill >= 0 ? smk_ai_skill
                                                : ((0x7F + racers[i].lap) & 7);
        /* $84 != 0 or $10 & $0020.  APPROXIMATED by the states this port
         * does have for "not driving normally"; the two ROM fields are
         * not decoded, and in the recorded race this branch took 6% of
         * kart-frames. */
        racers[i].trouble   = (racers[i].k.crash_frames > 0
                               || racers[i].k.bounce_cool > 0);
    }
    /* $80A047's sort: rank by progress, and each kart learns its place */
    for (int i = 0; i < n; i++) {
        racers[i].rank = smk_race_rank(racers, i, crs) - 1;
        if (racers[i].rank < 0) racers[i].rank = 0;
        if (racers[i].rank > 7) racers[i].rank = 7;
    }
    /* $010E: the rank table the routine indexes through $010C,y (ahead)
     * and $0110,y (behind) - both are that one table, two entries apart. */
    smk_racer *by_rank[8] = {0};
    for (int i = 0; i < n; i++)
        if (racers[i].rank >= 0 && racers[i].rank < 8)
            by_rank[racers[i].rank] = &racers[i];

    int s04 = 0, s06 = 0;       /* $04/$06 persist between calls */
    for (int i = 0; i < n; i++) {
        smk_racer *r = &racers[i];
        if (r->is_player) continue;             /* the player picks no row */
        int rk = r->rank;
        r->row = smk_ai_row_for(r, rk > 0 ? by_rank[rk - 1] : 0,
                            rk < 7 ? by_rank[rk + 1] : 0,
                            by_rank[2], &s04, &s06);
        r->branch = smk_ai_branch;
    }
}

/* ---- The attack (NOTES 279) --------------------------------------------
 *
 * $80:EEF9-$80:F141, transcribed.  The port used to fire each AI's weapon
 * on its own 640-frame cooldown whenever the player was within 160 px,
 * always as a drop behind - the user: "exaggerated rate of attack, not
 * accurate either".  The ROM runs ONE machine for the field, aimed at
 * the leading human, armed by the victim's rank-neighbour after a
 * minute's adjacency and a random draw against a per-character table,
 * and answers with a drop, a forward throw or a star by the geometry.
 * Everything below is read from the ROM at load or transcribed from it;
 * what is OURS is named where it stands. */
uint16_t SMK_AI_ATTACK_MASK[8][8];
uint16_t SMK_AI_ATTACK_TYPE[8][8];
uint16_t SMK_AI_ATTACK_WIN[4][2];

static uint16_t rd16(const smk_rom *rom, uint32_t snes)
{
    uint32_t pc = smk_snes_to_pc(rom, snes);
    return pc + 1 < rom->size ? (uint16_t)(rom->data[pc] | rom->data[pc + 1] << 8) : 0;
}

bool smk_ai_attack_load(const smk_rom *rom)
{
    /* $80EF95: eight pointers (by character*2) to rows of eight masks by
     * the victim's rank; $80F007: the same shape for the attack type */
    for (int ch = 0; ch < 8; ch++) {
        uint16_t mrow = rd16(rom, 0x80EF95u + (uint32_t)ch * 2u);
        uint16_t trow = rd16(rom, 0x80F007u + (uint32_t)ch * 2u);
        for (int rk = 0; rk < 8; rk++) {
            SMK_AI_ATTACK_MASK[ch][rk] = rd16(rom, 0x800000u | (uint32_t)(mrow + rk * 2));
            SMK_AI_ATTACK_TYPE[ch][rk] = rd16(rom, 0x800000u | (uint32_t)(trow + rk * 2));
        }
    }
    /* the distance windows, {max, min}: $F09B the drop, $F071 the throw,
     * $F0D3 the star, $F097/$F09F the drop's other rows */
    SMK_AI_ATTACK_WIN[0][0] = rd16(rom, 0x80F09Bu); SMK_AI_ATTACK_WIN[0][1] = rd16(rom, 0x80F09Du);
    SMK_AI_ATTACK_WIN[1][0] = rd16(rom, 0x80F071u); SMK_AI_ATTACK_WIN[1][1] = rd16(rom, 0x80F073u);
    SMK_AI_ATTACK_WIN[2][0] = rd16(rom, 0x80F0D3u); SMK_AI_ATTACK_WIN[2][1] = rd16(rom, 0x80F0D5u);
    SMK_AI_ATTACK_WIN[3][0] = rd16(rom, 0x80F097u); SMK_AI_ATTACK_WIN[3][1] = rd16(rom, 0x80F099u);
    return SMK_AI_ATTACK_TYPE[0][0] == 0x0C;      /* the shape the decode read */
}

/* $81:BB70, byte for byte: a 16-bit shuffle with an $AA55 trap */
uint16_t smk_rng_step(uint16_t *state)
{
    uint16_t r = *state;
    uint16_t a = (uint16_t)((r & 0xFF) << 8);     /* lda / and #$FF / xba */
    a ^= r;                                       /* eor $1F26 */
    a = (uint16_t)((a << 8) | (a >> 8));          /* xba */
    r = a;                                        /* sta $1F26 */
    a = (uint16_t)((a << 8) | (a >> 8));          /* xba */
    a &= 0xFF;
    a = (uint16_t)(a << 1);                       /* asl */
    a ^= r;                                       /* eor $1F26 */
    unsigned carry = a & 1;
    a >>= 1;                                      /* lsr */
    a ^= 0xFF80;
    if (carry) a ^= 0x8180;
    else if (a == 0xAA55) a ^= 0x8180;            /* the trap: never park on $AA55 */
    *state = a;
    return a;
}

void smk_ai_attack_init(smk_ai_attack *st, unsigned seed)
{
    memset(st, 0, sizeof *st);
    st->neighbour = -1;
    st->rng = (uint16_t)(seed ? seed : 0x1234);
}

static bool in_window(int d, const uint16_t win[2])
{
    return d < (int)win[0] && d >= (int)win[1];  /* $80F0FE: A < max, A >= min */
}

int smk_ai_attack_step(smk_ai_attack *st, smk_racer *racers, int n,
                       const bool *humans, smk_proj *projs, int nproj,
                       int *type_out)
{
    if (type_out) *type_out = 0;
    if (n < 2) return -1;
    /* $80EEF9: the victim is whichever of blocks 0 and 1 leads */
    int v = racers[0].rank <= racers[1].rank ? 0 : 1;
    smk_racer *vic = &racers[v];
    (void)humans;
    if (vic->trouble) return -1;                             /* $10 bit 5 */
    if (vic->lap < 2) return -1;                             /* $C0 < $8100 */
    if (st->cool > 0) { st->cool--; return -1; }            /* $0FEC */
    if (vic->k.speed == 0) { st->adjacent = 0; st->armed = 0; return -1; }
    /* the neighbour: behind a leader, ahead of anyone else */
    smk_racer *att = NULL;
    for (int i = 0; i < n; i++)
        if (i != v && racers[i].rank == (vic->rank == 0 ? 1 : vic->rank - 1)) att = &racers[i];
    if (!att) { st->adjacent = 0; st->armed = 0; return -1; }
    int a = (int)(att - racers);
    if (humans && humans[a]) return -1;                     /* $10 bit 13: computer-driven only */
    int ch = att->character % SMK_CHARACTERS;
    if (st->neighbour != ch * 2) { st->neighbour = ch * 2; st->adjacent = 0; st->armed = 0; return -1; }
    int dist = (int)sqrtf((float)((smk_kart_px(att->k.x) - smk_kart_px(vic->k.x)) * (smk_kart_px(att->k.x) - smk_kart_px(vic->k.x))
                                 + (smk_kart_px(att->k.y) - smk_kart_px(vic->k.y)) * (smk_kart_px(att->k.y) - smk_kart_px(vic->k.y))));
    if (st->armed == 0) {
        /* $80EF61: the 61st consecutive frame makes an attempt */
        if (SMK_AI_ATTACK_ADJ >= st->adjacent) { st->adjacent++; return -1; }
        st->adjacent = 0;
        uint16_t rnd = smk_rng_step(&st->rng);
        int rk = vic->rank & 7;
        if (rnd & SMK_AI_ATTACK_MASK[ch][rk]) return -1;
        st->armed = SMK_AI_ATTACK_TYPE[ch][rk];
        if (st->armed == 0) return -1;
    }
    int kind = smk_ai_weapon_of(ch);
    int type = st->armed;
    bool done = false;
    /* $80F17A takes a FREE block and the game has two ($1A00/$1A80);
     * with none free the attack is lost and the cooldown still runs -
     * seen in the 100cc recording, a cooldown set with no object born
     * while both blocks were live.  The port's list is longer (OURS), so
     * the AI is held to the game's two. */
    int live = 0;
    for (int i = 0; i < nproj; i++) if (projs[i].kind != SMK_PROJ_NONE) live++;
    bool block_free = live < 2;
    switch (type) {
    case 0x04:                                              /* $80F07F: the drop, by the kart ahead */
        if (!in_window(dist, SMK_AI_ATTACK_WIN[0])) break;
        /* $80F10B: the attacker's $2C within 8 of its rest - its steering
         * word, read off the recording (NOTES 279): a drop only while
         * driving straight.  OURS: the port's heading error stands in for
         * a word whose scale is not decoded. */
        { int16_t err = (int16_t)(att->dbg_want - att->k.angle);
          if (err > 0x0200 || err < -0x0200) break; }
        if (block_free && kind != SMK_AI_WEAPON_NONE && kind != SMK_AI_WEAPON_STAR)
            smk_proj_ai_drop(projs, nproj, kind, &att->k, a);
        done = true; break;
    case 0x0A:                                              /* $80F055: the throw, by the kart behind a leader */
        if (!in_window(dist, SMK_AI_ATTACK_WIN[1])) break;
        if (block_free && kind != SMK_AI_WEAPON_NONE && kind != SMK_AI_WEAPON_STAR)
            smk_proj_ai_throw(projs, nproj, kind, &att->k, a);
        done = true; break;
    case 0x08: case 0x0C:                                   /* $80F0B6 / $80F0A3: the star */
        if (!in_window(dist, SMK_AI_ATTACK_WIN[2])) break;
        att->star_t = SMK_AI_STAR_T;
        done = true; break;
    default:
        st->armed = 0; return -1;
    }
    if (!done) return -1;                                   /* out of range: stays armed, retries */
    st->cool = SMK_AI_ATTACK_COOL; st->adjacent = 0; st->armed = 0;   /* $80F135 */
    if (type_out) *type_out = type;
    return a;
}
