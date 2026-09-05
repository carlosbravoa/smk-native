/* Kart motion, in the ROM's own fixed-point arithmetic.
 *
 * What is decoded and faithful here:
 *   - the position/velocity/angle representations and their units
 *   - the velocity construction, (sin, -cos) * speed, from $80F8CF
 *   - the integration, position += velocity << 8, from $80879D
 *
 * What is NOT the game (see ledger S1): how player input becomes `speed`
 * and how the angle changes.  The ROM's acceleration curves, drift, hop and
 * surface response are still undecoded, and the constants in main.c are
 * invented.  Swapping them in later does not disturb anything in this file.
 */
#include "smk.h"
#include <math.h>

/* $80A4E1, the speed update:
 *
 *      clc
 *      lda ...   / adc $EC,x / sta $E8,x    ; fraction += accel fraction
 *      lda $EA,x / adc $EE,x / sta $EA,x    ; speed    += accel  + carry
 *      bpl +
 *      lda #$0000 / sta $E8,x / sta $EA,x   ; a negative speed clamps to 0
 *
 * i.e. one 32-bit add with a floor at zero.  It is written as two 16-bit
 * adds only because the CPU is 16-bit.
 */
void smk_kart_accelerate(smk_kart *k)
{
    int32_t speed = ((int32_t)k->speed << 16) | k->speed_frac;
    int32_t accel = ((int32_t)k->accel << 16) | k->accel_frac;
    speed += accel;
    if (speed < 0)
        speed = 0;                       /* the ROM zeroes both words */
    k->speed      = (int16_t)(speed >> 16);
    k->speed_frac = (uint16_t)(speed & 0xFFFF);
}

/* The DSP-1 sin/cos the game calls with (angle, speed).  Our model returns
 * radius*sin and radius*cos unshifted; the unit analysis in NOTES 017
 * confirms that scaling - it is the only one for which the 8.8 velocity
 * feeding a 16.16 position works out. */
static void smk_kart_face_real(smk_kart *k);

void smk_kart_face(smk_kart *k)
{
    /* during the measured 10-frame bounce window ($42 countdown) the kart
     * is ballistic: velocity persists, steering and thrust do not apply */
    if (k->bounce_cool > 0) {
        k->bounce_cool--;
        return;
    }
    /* the kart-to-kart window does the same thing (NOTES 166) */
    if (k->bump_cool > 0) {
        k->bump_cool--;
        return;
    }
    smk_kart_face_real(k);
}

static void smk_kart_face_real(smk_kart *k)
{
    double a = (double)k->angle * (2.0 * M_PI / (double)SMK_ANGLE_TURN);
    k->vx = (int16_t)lrint(sin(a) * (double)k->speed);
    k->vy = (int16_t)lrint(-cos(a) * (double)k->speed);
}

/* $80879D:
 *      clc
 *      lda $21,x / and #$FF00 / adc $16,x / sta $16,x   ; frac += vel<<8
 *      lda #$FF00 / and $22,x / sign extend / xba
 *      adc $18,x / sta $18,x                            ; int  += vel>>8 + C
 * which is one 32-bit add of (velocity << 8).
 */
static int32_t advance(int32_t pos, int16_t vel)
{
    pos += (int32_t)vel << (SMK_POS_SHIFT - SMK_VEL_SHIFT);
    /* the track plane wraps, and so does the ROM's tilemap */
    /* The world does NOT wrap (NOTES 063; bug 1: a long jump off Ghost
     * Valley's edge teleported you across).  Clamp; the void classes at
     * the edge do the rest. */
    if (pos < 0) pos = 0;
    if (pos > SMK_WORLD_FIX - 1) pos = SMK_WORLD_FIX - 1;
    return pos;
}

#define BOUNCE_VEL    0x1000      /* 16 px per frame, as measured */

/* $80B1D6 - the ROM's only `sbc #$001A`, so this integrator is unambiguous:
 *
 *      lda $26,x / sec / sbc #$001A / sta $26,x     velocity -= gravity
 *      clc / adc $1F,x                             height word += velocity
 *      bpl still-airborne
 *      stz $1F,x / stz $26,x                       landed: clear both
 *      lda $E2,x / and #$7FFF / sta $E2,x          clear the airborne flag
 *
 * Adding the velocity to the word at $1F is `z += zvel << 8` on the 24-bit
 * value, and the landing test is the sign of that word.  Verified frame by
 * frame against the running game: with zvel $0080 the arc peaks at 0.99 px
 * and lands on frame 8; with $0180 it peaks at 10.34 px and lands on 31.
 */
void smk_kart_gravity(smk_kart *k)
{
    if (!k->airborne) {
        if (k->bounce_cool > 0) k->bounce_cool--;
        return;
    }
    k->zvel = (int16_t)(k->zvel - SMK_GRAVITY);
    int32_t nz = k->z + ((int32_t)k->zvel << 8);
    if ((int16_t)(nz >> 8) < 0) {          /* the game's `bpl` test */
        k->z = 0;
        k->zvel = 0;
        k->airborne = false;
        k->bvx = k->bvy = 0;
        return;
    }
    k->z = nz;
}

void smk_kart_launch(smk_kart *k, int16_t zvel)
{
    k->zvel = zvel;
    k->airborne = true;
}

/* A class-$10 ramp, the ROM's way ($80B7AF-$80B7E8, the player's own
 * path in src/player.c): the speed is raised to at least $2E0, the DSP-1
 * Triangle command turns it into speed*sin($0E00) up and speed*cos($0E00)
 * forward, and $1F starts at $280 - two and a half pixels up on the
 * launch frame.  The AI used to launch with a constant $103 and from the
 * ground, which is the battery's reading at ONE speed (NOTES 088, 608
 * -> 736 -> 247 up), so a fast kart reached Mario Circuit 2's crossing
 * wall before it was four pixels high and every kart landed short, on
 * the bridge road, into sector 20's loop (NOTES 278). */
void smk_kart_ramp(smk_kart *k, int theme)
{
    /* $80B79E: on theme $0C (Bowser Castle, theme 6) the floor is $400 */
    int16_t floor_ = theme == 6 ? 0x0400 : 0x02E0;
    int16_t s = k->speed < floor_ ? floor_ : k->speed;
    int16_t up, fwd;
    smk_dsp_sincos(0x0E00, s, &up, &fwd);
    k->zvel = up;
    /* An AI kart KEEPS its speed at the launch - MEASURED (NOTES 280): DK
     * Jr 1330 on the ramp, 1380 the frame after, held through the flight;
     * the player's own path (src/player.c) takes speed*cos, as the oracle
     * showed for the player.  The floor is the player's, unverified for
     * a slow AI kart. */
    /* FAIR (NOTES 281): the player's cosine, the game's speed kept */
    k->speed = smk_cpu_rules == SMK_CPU_FAIR ? fwd : s;
    k->z = (int32_t)0x0280 << 8;
    k->airborne = true;
}

void smk_kart_move(smk_kart *k, const smk_track *t) { smk_kart_move_ex(k, t, true); }

/* $80FC74: (v * f) >> 8, the SNES multiplier with the sign put back - an
 * arithmetic shift, so it floors toward minus infinity. */
static int16_t scale8(int16_t v, int f)
{
    return (int16_t)(((int32_t)v * f) >> 8);
}

static int16_t vec_len(int16_t vx, int16_t vy)
{
    int32_t m = (int32_t)vx * vx + (int32_t)vy * vy, r = 0, bit = 1 << 30;
    while (bit > m) bit >>= 2;
    while (bit) {                                   /* integer sqrt */
        if (m >= r + bit) { m -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (int16_t)r;
}

/* $80F99A -> $80F9DF, MEASURED frame by frame in the running game
 * (tools/labs/wall.py, NOTES 125).  The frame AFTER a wall hit, each
 * velocity component is scaled by the pair the bounce direction $56
 * selects - the reflected axis by $80/256, the other by $F0/256 - and the
 * speed scalar $EA is then RE-DERIVED from the vector, not damped.  Three
 * captures at different approach angles agree to the unit. */
static void bounce_damp(smk_kart *k)
{
    static const uint8_t TBL_VX[4] = { 0x80, 0x80, 0xF0, 0xF0 };   /* $80FA4A */
    static const uint8_t TBL_VY[4] = { 0xF0, 0xF0, 0x80, 0x80 };   /* $80FA52 */
    int d = (k->bounce_dir >> 1) & 3;
    int ax = k->vx < 0 ? -k->vx : k->vx;
    int ay = k->vy < 0 ? -k->vy : k->vy;
    if (ax < 0xC0 && ay < 0xC0) {
        /* $80F9C1 - the PUSH-OUT, and the answer to "the bounce is
         * constant no matter the speed, it feels more like a push back
         * than a real bounce" (user, NOTES 133).  With both components
         * under $C0 the game does not damp anything: it FORCES each to
         * +-$100, sign kept.  A diagonal comes out at |(256,256)| = 362
         * whatever you arrived at - and 362 is exactly what the recording
         * shows, over and over. */
        k->vx = k->vx < 0 ? (int16_t)-0x100 : (int16_t)0x100;
        k->vy = k->vy < 0 ? (int16_t)-0x100 : (int16_t)0x100;
    } else if (k->speed >= 0x500) {               /* $80FA33: a fast hit */
        k->vx = scale8(k->vx, 0x40);
        k->vy = scale8(k->vy, 0x40);
    } else {
        k->vx = scale8(k->vx, TBL_VX[d]);
        k->vy = scale8(k->vy, TBL_VY[d]);
    }
    k->speed = vec_len(k->vx, k->vy);
    k->speed_frac = 0;
    /* $80A0C7 runs on the frame AFTER the impact, so the angle it takes
     * is the DAMPED velocity's, not the reflected one's - and it comes
     * back from $81F638 masked to its high byte.  Checked against the
     * user's run: at frame 783 the damped (249,-140) is 60.6 degrees and
     * the game holds $2B00; taking the reflected (498,-149) instead gave
     * 73 degrees and a slip 13 degrees out (NOTES 134). */
    k->crash_lag = (int16_t)((smk_angle_of(k->vx, k->vy) & 0xFF00) - k->angle);
    k->bounce_pend = 0;
}

/* $81F638 reduced to what the port needs: the direction of a velocity
 * vector in the game's 65536-unit angles, 0 = north.  The ROM reads it
 * out of a boot-time octant table; ours is the same geometry in floating
 * point and is only ever used to set the post-bounce lag. */
uint16_t smk_angle_of(int16_t vx, int16_t vy)
{
    double a = atan2((double)vx, -(double)vy);
    long v = lround(a * 65536.0 / (2.0 * 3.14159265358979323846));
    return (uint16_t)(v & 0xFFFF);
}

void smk_kart_bounce_damp_for_test(smk_kart *k) { bounce_damp(k); }

void smk_kart_move_ex(smk_kart *k, const smk_track *t, bool auto_ramp)
{
    if (k->bounce_pend) bounce_damp(k);           /* $52's $C000 bits */
    /* Airborne: the kart flies over most solids - that is what makes jumps
     * work - but a hard WALL (surface type 0, e.g. $20) still blocks, or a
     * bounced kart can land embedded inside one and lock up (NOTES 053).
     * The ROM treats landing per type too: $80B1F2 remaps type-$22 cells
     * to $4C at touchdown.  INFERRED: the exact set of flight-blocking
     * types; type 0 is the one observed to embed. */
    /* HEIGHT, not surface type, decides whether a kart clears a wall.
     * $80FA5A opens with `lda $20,x / cmp #$0004 / bcs` - above four and
     * the collision test is skipped altogether; below it, an airborne
     * kart collides exactly like one on the ground.  The port had been
     * filtering by surface TYPE instead, so any hop cleared anything that
     * was not type 0 - which is why the Ghost Valley rails could be
     * jumped, and in the original they cannot: a hop launches at $E0 and
     * peaks under four (NOTES 137). */
    if (k->airborne && (k->z >> 16) >= 4) {
        k->x = advance(k->x, k->bvx ? k->bvx : k->vx);
        k->y = advance(k->y, k->bvy ? k->bvy : k->vy);
        return;
    }

    int32_t nx = advance(k->x, k->vx);
    int32_t ny = advance(k->y, k->vy);

    /* Ramps: class $10 is the launcher.  MEASURED (NOTES 088 surface
     * battery): driving onto $10 sends z to 247 while the speed RISES
     * (608 -> 701) and the kart keeps moving; every other class either
     * blocks, drags or does nothing vertical.  The old rule had bit-7
     * launching, which is backwards - see below. */
    {
        uint8_t here = smk_track_surface(t, smk_kart_px(nx), smk_kart_px(ny));
        if (auto_ramp && (here & 0xFE) == 0x10 && !k->airborne) {
            smk_kart_ramp(k, t->theme);
            k->x = nx;
            k->y = ny;
            return;
        }
    }

    /* CORRECTION (NOTES 088): the bit-7 classes are WALLS, not ramps.
     * NOTES 044 measured class $80 head-on and got a wall - the into-wall
     * component reflects, a knockback follows, speed is preserved.  The
     * later "bit-7 bars launch you" rule was invented to explain jumps and
     * contradicted that measurement: it let a kart at speed vault Mario
     * Circuit's barrier blocks and fly off the world (playtest).  Bit-7
     * now falls through to the wall response below, where it belongs. */

    /* Wall response, ported from measurement (NOTES 044): reflect the
     * into-wall component and kick away for a few frames.  MEASURED on one
     * surface class in the demo; applied to every solid here - the ROM's
     * per-class differences are not decoded. */
    bool bx = smk_surface_solid(smk_track_surface(t, smk_kart_px(nx), smk_kart_px(k->y)));
    bool by = smk_surface_solid(smk_track_surface(t, smk_kart_px(k->x), smk_kart_px(ny)));
    /* the DIAGONAL destination must be tested too, or a fast kart slips
     * between two solid cells into the interior (playtest, NOTES 063) */
    if (!bx && !by
        && smk_surface_solid(smk_track_surface(t, smk_kart_px(nx), smk_kart_px(ny))))
        bx = by = true;
    /* already embedded (legacy positions, teleports): let it move OUT */
    if (smk_surface_solid(smk_track_surface(t, smk_kart_px(k->x), smk_kart_px(k->y))))
        bx = by = false;
    if (bx || by) {
        /* Wall contact - the MEASURED $20-wall response (NOTES 071):
         * head-on at 791 rebounds at exactly 791 - the bounce is a pure
         * velocity ROTATION with zero speed loss - and $42,x counts down
         * from 10, a ballistic window with no steering or thrust.  No
         * vertical launch on plain walls ($26 stays 0; the hop belongs to
         * the bit-7 bars, NOTES 044).  Port: reflect the blocked
         * component, keep the magnitude, run the 10-frame window. */
        {
            /* Wall response, MEASURED frame by frame (NOTES 092) by
             * painting wall tiles into the path of a kart that is
             * driving normally - the only rig that produced a real
             * impact:
             *
             *   f20  845  vx -844          approaching
             *   f21  845  vx +844  $C000   the component REFLECTS
             *   f22  422  vx +422  $C000   the speed EXACTLY HALVES
             *   f22-29                     knockback, ~17 px travelled back
             *   f30  423          $8000    control returns
             *
             * The halving is the piece I had missing: reflecting at FULL
             * speed is what made the bounce feel violent, and cancelling
             * the component instead made the kart stick.  Total rebound
             * is about 20 px - a short bounce, as reported.
             *
             * $20/$24/$26 stop dead instead (speed -> 0, 3 px of travel,
             * NOTES 088). */
            /* A FRESH impact reflects and halves; STAYING against the
             * wall does not re-halve.  The surface battery drove a kart
             * into $80 continuously and the speed came back 832 -> 832
             * with ~50 px of travel, so sustained contact is a scrape,
             * not a series of impacts.  Halving on every contact frame
             * instead drove the speed to zero within a few bounces and
             * pinned the kart to the barrier (playtest, twice). */
            /* EVERY contact is an impact: reflect the blocked
             * component and halve the speed (NOTES 092, the one clean
             * capture).  Playtest: "you bounce back, you can continue to
             * accelerate towards it so you keep bouncing" - so repeated
             * bounces are the real behaviour, and the speed recovers
             * between them because the throttle is still on.
             *
             * Two things I had here were wrong, both from the same
             * degenerate rig: a "dead stop" family ($20/$24/$26 -> speed
             * 0) and a scrape that suppressed re-impacts.  The battery
             * they came from filled every driveable tile with the class
             * under test, so the kart was standing INSIDE a solid -
             * which reads as speed 0 whatever the class really does on
             * contact.  Zeroing the speed is precisely what left the
             * kart stuck against a barrier. */
            /* a breakable block crumbles on contact (NOTES 123): the
             * cell that blocked us, if its class is $82 or above */
            {
                int hx = bx ? smk_kart_px(nx) : smk_kart_px(k->x);
                int hy = by ? smk_kart_px(ny) : smk_kart_px(k->y);
                uint8_t hc = smk_track_surface(t, hx, hy);
                if (smk_blocks_breakable(hc)) {
                    int cell = (((hy - 1) & (SMK_WORLD_PX - 1)) >> 3) * SMK_MAP_DIM
                             + ((hx & (SMK_WORLD_PX - 1)) >> 3);
                    smk_blocks_hit(cell, true);
                }
            }
            /* $80FB7D/$80FB90/$80FB9A: the blocked component MIRRORS,
             * and $56 records which way the kart is now pushed.  The
             * speed scalar is untouched on the impact frame itself -
             * measured: 835 in, 835 out, the halving comes a frame
             * later through bounce_damp (NOTES 125). */
            if (bx) k->vx = (int16_t)-k->vx;
            if (by) k->vy = (int16_t)-k->vy;
            k->bounce_dir = bx ? (k->vx < 0 ? 2 : 0) : (k->vy < 0 ? 6 : 4);
            k->bounce_pend = 1;
            k->bounce_hit = 1;            /* $10 bit 12, read by $80A0C7 */
            k->crash_frames = 3;   /* the slip is taken after the damping */
            k->bounce_cool = 9;              /* $5C = 8, released on the 9th */
        }
        /* $80F93C: the game counts frames the kart spends unable to
         * move and at EIGHT ejects it, along the quadrant it FACES, at
         * a flat +-$100 on both axes ($80F964, tables $80F98A/$80F992).
         * Our port refuses entry rather than letting the kart inside, so
         * "inside a wall" here is "blocked on both axes" - and without
         * this a kart wedged in a corner of the Ghost Valley rails sat
         * there with its speed cycling and nowhere to put it, which is
         * the dead stop the user hit (NOTES 136). */
        if (bx && by) {
            if (++k->stuck >= 8) {
                static const int16_t EJX[4] = { 0x0100, 0x0100, -0x100, -0x100 };
                static const int16_t EJY[4] = { -0x100, 0x0100, 0x0100, -0x100 };
                int q = (k->angle >> 14) & 3;
                k->vx = EJX[q]; k->vy = EJY[q];
                k->speed = vec_len(k->vx, k->vy);
                k->bounce_cool = 0; k->bounce_pend = 0;
                k->crash_frames = 0; k->stuck = 0;
                k->x = advance(k->x, k->vx);
                k->y = advance(k->y, k->vy);
                return;
            }
        } else k->stuck = 0;

        /* SLIDE ALONG: move on whichever axis is not blocked.  Returning
         * without moving threw away the along-wall component too, so a
         * kart held against a barrier froze in place instead of scraping
         * past it - which is what "stuck in the barriers" looks like from
         * the driver's seat.  The surface battery measured ~50 px of
         * travel while against a wall, so the game clearly keeps moving
         * you along it. */
        if (!bx) k->x = nx;
        if (!by) k->y = ny;
        return;
    }
    k->x = nx;
    k->y = ny;
}

/* ---- Kart against kart (NOTES 166) ------------------------------------
 *
 * $81:9277's first eight entries, in the drivers' own order: SMK's weight
 * classes, and the only thing that decides who wins a bump. */
const uint8_t SMK_KART_WEIGHT[SMK_CHARACTERS] =
    { 0x1A, 0x1A, 0x1B, 0x19, 0x1B, 0x19, 0x19, 0x19 };

/* $819CD2/$819CEC: whichever of the converging pair is NEARER ZERO is
 * moved $80 further from the other, so an exchange that left them still
 * closing separates them instead.
 *
 * LABELLED, and the one place the reading and the measurement disagree.
 * The routine is indexed $14,x with x = 0 then 2, which does not pair
 * the components the way the loads at $819B7F do; and the single
 * geometry the oracle could make it fire in - (300,-400) against
 * (500,-200) - came back with $80 off BOTH x components, which does not
 * separate anything.  That measurement is not clean: the partner is an
 * AI kart whose own frame ran too.  What ships is the READING, because
 * it is the only version that does the job the routine exists for, and
 * without it the field grinds to a halt in a heap.  S24. */
static void bump_push(int16_t *p, int16_t *q)
{
    if (*p >= 0) {                            /* $819CD2 */
        if (*p < *q) *p = (int16_t)(*p - SMK_BUMP_PUSH);
        else         *q = (int16_t)(*q - SMK_BUMP_PUSH);
    } else {                                  /* $819CEC */
        if (*p >= *q) *p = (int16_t)(*p + SMK_BUMP_PUSH);
        else          *q = (int16_t)(*q + SMK_BUMP_PUSH);
    }
}

/* $819C6B is smk_dsp_sincos with the result stored as the velocity. */
static void bump_polar(smk_kart *k, uint16_t angle, int16_t radius)
{
    int16_t sx, cy;
    smk_dsp_sincos(angle, radius, &sx, &cy);
    k->vx = sx;
    k->vy = (int16_t)-cy;      /* $819C89's eor/inc: the ROM negates cos */
    /* the radius IS the speed; deriving it back out of the vector loses
     * a unit to rounding and the port's physics reads $EA, not $22/$24 */
    k->speed = radius;
}

bool smk_kart_bump(smk_kart *a, int wa, smk_kart *b, int wb)
{
    /* $81982A.  The ROM's test is `d + 4 < 8` on the difference the
     * broadphase's list order produces, so the window is [-4, +3] and
     * not quite symmetric; measured in the oracle it fires at dx -4..3
     * and dy -3..3, which is that window seen from both list orders. */
    int dx = smk_kart_px(b->x) - smk_kart_px(a->x);
    int dy = smk_kart_px(b->y) - smk_kart_px(a->y);
    if (dx + SMK_BUMP_BOX < 0 || dx + SMK_BUMP_BOX >= 2 * SMK_BUMP_BOX) return false;
    if (dy + SMK_BUMP_BOX < 0 || dy + SMK_BUMP_BOX >= 2 * SMK_BUMP_BOX) return false;
    int cool = a->bump_cool | b->bump_cool;
    if (cool > 1) return false;                              /* $819848 */
    if (a->stuck || b->stuck) return false;                   /* $819853 */
    if (smk_kart_height_px(a) > SMK_BUMP_Z_MAX
        || smk_kart_height_px(b) > SMK_BUMP_Z_MAX) return false;   /* $81985A */

    /* $A2 is the direction of TRAVEL, which is what the velocity says. */
    uint16_t va = smk_angle_of(a->vx, a->vy);

    /* $819B06's FIRST test, and the one this port was missing: $1C is
     * the pair's cooldown ORed together, and a second contact while it
     * is still running down does not get the full answer at all - it
     * goes to $819C93, which
     *
     *   $819C9A  both karts stopped -> nudge the heavy one along its
     *            HEADING at $0180, which is what unsticks a heap
     *   $819CA6  else, faster of the two under $C0 -> the same $0180
     *            nudge, along its velocity angle
     *   $819CB7  else RTS - nothing whatsoever
     *
     * so once two karts have touched, the next eight frames of contact
     * cost them nothing unless they have nearly stopped.  Without it
     * every re-contact was another full exchange, which is the user's
     * "between them bouncing is different, less aggressive". */
    if (cool) {
        int16_t fast = a->speed > b->speed ? a->speed : b->speed;
        if (!a->speed && !b->speed) bump_polar(a, a->angle, 0x0180);
        else if (fast < 0x00C0)      bump_polar(a, va, 0x0180);
        a->bump_cool = SMK_BUMP_COOL;      /* $8198A8 re-marks the pair */
        b->bump_cool = SMK_BUMP_COOL;
        return true;
    }

    int d = wa - wb;                     /* $819B1D, >= 0: a is the heavier */

    if (d == 0) {
        /* $819B75 -> $819B7F: exchange the two vectors ($819CB8) ... */
        int16_t t;
        t = a->vx; a->vx = b->vx; b->vx = t;
        t = a->vy; a->vy = b->vy; b->vy = t;
        /* $819B94: an exchange that leaves BOTH pairs sharing a sign has
         * left them closing, so separate them ($819CC9).  A zero
         * component is not a converging one - the ROM's `bpl` calls it
         * positive, but the measured head-on pair, both with vx = 0,
         * came back exchanged and nothing else. */
        if (a->vx && b->vx && a->vy && b->vy
            && ((a->vx < 0) == (b->vx < 0)) && ((a->vy < 0) == (b->vy < 0))) {
            bump_push(&a->vx, &b->vx);
            bump_push(&a->vy, &b->vy);
        }
        a->speed = vec_len(a->vx, a->vy);
        b->speed = vec_len(b->vx, b->vy);
    } else if (a->speed >= b->speed) {
        /* $819C0D: the heavier kart is also the faster one, so it keeps
         * its line and pays half the closing speed, and the lighter is
         * flung off its shoulder at that speed plus $20.  Both halves
         * measured to the unit: 600 against 400 left the heavy kart on
         * 500, and the light one at 632 - $1800 off its line. */
        int16_t keep = (int16_t)(a->speed - ((a->speed - b->speed) >> 1));
        /* LABELLED: which shoulder is OURS.  $819C4B picks the sign from
         * $2C/$32, two fields this port does not model; the side b is
         * actually on is the same answer for every case we can measure. */
        int16_t rel = (int16_t)((uint16_t)smk_angle_of((int16_t)(dx * 256),
                                                       (int16_t)(dy * 256)) - va);
        uint16_t off = (uint16_t)(rel >= 0 ? 0x1800 : -0x1800);
        int16_t fling = (int16_t)(a->speed + 0x20);
        bump_polar(a, va, keep);
        bump_polar(b, (uint16_t)(va + off), fling);
    } else if (d == 1) {
        /* $819BCA: heavier but slower, one class apart - the plain
         * exchange ($819B7F). */
        int16_t t;
        t = a->vx; a->vx = b->vx; b->vx = t;
        t = a->vy; a->vy = b->vy; b->vy = t;
        a->speed = vec_len(a->vx, a->vy);
        b->speed = vec_len(b->vx, b->vy);
    } else {
        /* $819BD5 -> $819BE4: two classes apart and rammed from behind.
         * The heavy kart is not touched at all; the light one is turned
         * $1000 off its victim's line and cut to a QUARTER of its speed,
         * which is the measurement - 400 against 600 left the rammer on
         * 99 where a quarter of 400 is 100.  Under $0100 the game turns
         * it $6000 instead ($819BF4), at half of at least $0200. */
        if (a->speed >= 0x0100)
            bump_polar(b, (uint16_t)(va + 0x1000), (int16_t)(a->speed >> 2));
        else
            bump_polar(b, (uint16_t)(va + 0x6000),
                       (int16_t)((a->speed < 0x0200 ? 0x0200 : a->speed) >> 1));
    }

    /* $8198A8: the pair is marked for eight frames.  In the port that
     * window does double duty - it is also what keeps smk_kart_face from
     * rebuilding the velocity out of speed and heading before the bump
     * has moved anybody. */
    a->bump_cool = SMK_BUMP_COOL;
    b->bump_cool = SMK_BUMP_COOL;
    /* ...and $10 bit 12 goes up on both, so $80A0C7 charges the exchange
     * as it charges a wall - the debugger shows that routine halving the
     * player's rev at the 150cc start's contact, $2F40 -> $17A0 (NOTES
     * 285).  A re-contact inside the window, above, costs nothing. */
    a->bounce_hit = 1;
    b->bounce_hit = 1;
    return true;
}

void smk_karts_collide(smk_kart **karts, const uint8_t *weight, int n)
{
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            if (!karts[i] || !karts[j]) continue;
            /* $819867 orders the pair so the heavier is X */
            if (weight[i] >= weight[j])
                smk_kart_bump(karts[i], weight[i], karts[j], weight[j]);
            else
                smk_kart_bump(karts[j], weight[j], karts[i], weight[i]);
        }
}
