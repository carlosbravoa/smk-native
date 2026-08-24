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
void smk_kart_face(smk_kart *k)
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
    pos &= (SMK_WORLD_FIX - 1);
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

void smk_kart_move(smk_kart *k, const smk_track *t)
{
    /* Airborne: the kart flies over most solids - that is what makes jumps
     * work - but a hard WALL (surface type 0, e.g. $20) still blocks, or a
     * bounced kart can land embedded inside one and lock up (NOTES 053).
     * The ROM treats landing per type too: $80B1F2 remaps type-$22 cells
     * to $4C at touchdown.  INFERRED: the exact set of flight-blocking
     * types; type 0 is the one observed to embed. */
    if (k->airborne) {
        int32_t fx = advance(k->x, k->bvx ? k->bvx : k->vx);
        int32_t fy = advance(k->y, k->bvy ? k->bvy : k->vy);
        uint8_t sv = smk_track_surface(t, smk_kart_px(fx), smk_kart_px(fy));
        if (smk_surface_solid(sv) && smk_surface_type(sv) == 0) {
            /* deflect along the wall, as on the ground */
            uint8_t sx = smk_track_surface(t, smk_kart_px(fx), smk_kart_px(k->y));
            if (smk_surface_solid(sx) && smk_surface_type(sx) == 0) {
                k->bvx = 0;
                k->vx = 0;
            } else {
                k->bvy = 0;
                k->vy = 0;
            }
            fx = advance(k->x, k->bvx ? k->bvx : k->vx);
            fy = advance(k->y, k->bvy ? k->bvy : k->vy);
            sv = smk_track_surface(t, smk_kart_px(fx), smk_kart_px(fy));
            if (smk_surface_solid(sv) && smk_surface_type(sv) == 0) {
                /* fully cornered mid-flight: land on the spot, drop the
                 * bounce, and stop - do not hover and re-bounce forever */
                k->z = 0;
                k->zvel = 0;
                k->airborne = false;
                k->bvx = k->bvy = 0;
                k->speed = 0;
                k->vx = k->vy = 0;
                return;
            }
        }
        k->x = fx;
        k->y = fy;
        return;
    }

    int32_t nx = advance(k->x, k->vx);
    int32_t ny = advance(k->y, k->vy);

    /* Jump bars: the bit-7 surface classes ($80/$82/$84) are the ramps.
     * Driving onto one launches the kart - the class-$80 response measured
     * in NOTES 044 - and the flight carries it over the $22/$24 gap
     * beyond.  Correction from playtest (NOTES 058): the gaps themselves
     * are NOT self-vaulting; without a bar you stop at the edge, and rails
     * ($22 on Ghost Valley) block driving entirely. */
    {
        uint8_t here = smk_track_surface(t, smk_kart_px(nx), smk_kart_px(ny));
        if ((here & 0x80) && !k->airborne && k->speed >= 200) {
            smk_kart_launch(k, 0x0140);
            k->x = nx;
            k->y = ny;
            return;
        }
    }

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
        /* Wall contact (user playtest, NOTES 055).  SMK1 walls are sticky:
         * a hit kills the into-wall component and most of the speed, with
         * no launch - the launch + $1000 along-wall fling measured in
         * NOTES 044 belongs to bit-7 SPECIAL surfaces only (that is where
         * it was measured) and applying it to plain walls produced the
         * reported eternal ping-pong.  Plain-wall numbers are a labelled
         * feel model, not a decode. */
        uint8_t wallv = bx
            ? smk_track_surface(t, smk_kart_px(nx), smk_kart_px(k->y))
            : smk_track_surface(t, smk_kart_px(k->x), smk_kart_px(ny));
        if (0) {   /* the old on-block fling: superseded by the jump bar below */
            k->bounce_cool = 30;
            k->speed = (int16_t)(k->speed - k->speed / 4);
            smk_kart_launch(k, SMK_HOP_VEL);
            if (bx && by) {
                k->bvx = (int16_t)(k->vx > 0 ? -BOUNCE_VEL / 2 : BOUNCE_VEL / 2);
                k->bvy = (int16_t)(k->vy > 0 ? -BOUNCE_VEL / 2 : BOUNCE_VEL / 2);
                k->vx = k->vy = 0;
            } else if (bx) {
                k->vx = 0; k->bvx = 0;
                k->bvy = (int16_t)(k->vy >= 0 ? BOUNCE_VEL : -BOUNCE_VEL);
            } else {
                k->vy = 0; k->bvy = 0;
                k->bvx = (int16_t)(k->vx >= 0 ? BOUNCE_VEL : -BOUNCE_VEL);
            }
        } else {
            /* sticky wall, angle-dependent: a graze scrubs a little, a
             * head-on hit takes nearly everything.  Loss is proportional
             * to the blocked share of the velocity. */
            int into  = bx ? (k->vx < 0 ? -k->vx : k->vx)
                           : (k->vy < 0 ? -k->vy : k->vy);
            int along = bx ? (k->vy < 0 ? -k->vy : k->vy)
                           : (k->vx < 0 ? -k->vx : k->vx);
            if (bx && by) {
                k->speed = 0;
            } else if (into + along > 0) {
                k->speed = (int16_t)((int32_t)k->speed * along
                                     / (into + along));
            }
            if (bx) k->vx = 0;
            if (by) k->vy = 0;
        }
        if (!bx) k->x = nx;
        if (!by) k->y = ny;
        return;
    }
    k->x = nx;
    k->y = ny;
}
