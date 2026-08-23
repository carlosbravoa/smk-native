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

void smk_kart_move(smk_kart *k, const smk_track *t)
{
    int32_t nx = advance(k->x, k->vx);
    int32_t ny = advance(k->y, k->vy);

    /* PLACEHOLDER wall response - see ledger S6.  The ROM enters a collision
     * state at $80F8C0 ($42,x = $8000, $26,x = $80) with its own recovery;
     * until that is decoded we refuse the blocked axis so a graze slides. */
    bool bx = smk_surface_solid(smk_track_surface(t, smk_kart_px(nx), smk_kart_px(k->y)));
    bool by = smk_surface_solid(smk_track_surface(t, smk_kart_px(k->x), smk_kart_px(ny)));
    if (!bx) k->x = nx;
    if (!by) k->y = ny;
    if (bx && by) k->speed = (int16_t)(k->speed / 4);
}
