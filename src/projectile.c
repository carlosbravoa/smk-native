/* Bananas and shells - the player's thrown items.
 *
 * Numbers from the oracle, not from the ROM's script VM: $80:F243 is a
 * chain of per-frame handlers run through jmp ($0000,x) and reading it
 * would have cost the day.  What was measured instead is in smk.h and
 * docs/ITEMS.md §5, with the two things still OURS labelled there.
 */
#include "smk.h"
#include <math.h>
#include <string.h>

static int free_slot(smk_proj *list, int n)
{
    for (int i = 0; i < n; i++) if (list[i].kind == SMK_PROJ_NONE) return i;
    return -1;
}

static void set_velocity(smk_proj *p)
{
    int16_t sx, cy;
    smk_dsp_sincos(p->heading, p->speed, &sx, &cy);
    p->vx = sx;
    p->vy = (int16_t)-cy;
}

void smk_proj_throw(smk_proj *list, int n, int kind, const smk_kart *k,
                    uint16_t heading, int owner, int target, bool backward,
                    bool ahead)
{
    int i = free_slot(list, n);
    if (i < 0) return;                           /* $80:F190: no block free */
    smk_proj *p = &list[i];
    memset(p, 0, sizeof *p);
    p->kind = kind;
    p->owner = owner;
    p->target = target;
    p->heading = (uint16_t)(heading + (backward ? 0x8000 : 0));
    p->x = k->x; p->y = k->y; p->z = 0;
    switch (kind) {
    case SMK_PROJ_BANANA:
        if (ahead) {
            /* thrown: flies ahead and lands - flight time OURS until the
             * chain2 measurement lands */
            p->kind = SMK_PROJ_BANANA_AIR;
            p->speed = (int16_t)(k->speed + SMK_PROJ_SPEED_ADD / 2);
            p->zv = 0x00C0;
            set_velocity(p);
        } else {
            /* MEASURED: eight pixels behind the kart, and it stays */
            int16_t sx, cy;
            smk_dsp_sincos(heading, 8 * 256, &sx, &cy);
            p->x -= (int32_t)sx << (SMK_POS_SHIFT - 8);
            p->y += (int32_t)cy << (SMK_POS_SHIFT - 8);
        }
        break;
    case SMK_PROJ_GREEN:
    case SMK_PROJ_RED:
        /* MEASURED: the kart's speed + $300, along its heading; a backward
         * throw is OURS at the plain $300 */
        p->speed = (int16_t)(backward ? SMK_PROJ_SPEED_ADD
                                      : k->speed + SMK_PROJ_SPEED_ADD);
        p->delay = SMK_PROJ_RED_DELAY;
        set_velocity(p);
        break;
    default:
        p->kind = SMK_PROJ_NONE;
        break;
    }
}

static uint16_t bearing(int32_t fx, int32_t fy, int32_t tx, int32_t ty)
{
    float dx = (float)(tx - fx), dy = (float)(ty - fy);
    /* heading 0 = north (-y), turning clockwise: the kart convention */
    float a = atan2f(dx, -dy);
    return (uint16_t)(int)(a * (65536.0f / (2.0f * (float)M_PI)));
}

/* $81:9F0A: steer $2A toward the bearing - snap inside the band, else
 * $0400 a frame toward it */
static void home(smk_proj *p, uint16_t want)
{
    int diff = (int16_t)(uint16_t)(want - p->heading);
    if (diff > -SMK_PROJ_RED_BAND && diff < SMK_PROJ_RED_BAND) p->heading = want;
    else p->heading = (uint16_t)(p->heading + (diff < 0 ? -SMK_PROJ_RED_TURN
                                                        :  SMK_PROJ_RED_TURN));
}

void smk_proj_step(smk_proj *list, int n, const smk_track *trk,
                   const smk_kart *const *karts, int nkarts)
{
    for (int i = 0; i < n; i++) {
        smk_proj *p = &list[i];
        if (p->kind == SMK_PROJ_NONE) continue;
        p->t++;
        if (p->dying) {
            /* $80:F85D: a hop, then gone when it lands */
            p->z += (int32_t)p->zv << 8;
            p->zv = (int16_t)(p->zv - 26);
            if (p->z <= 0) p->kind = SMK_PROJ_NONE;
            continue;
        }
        if (p->kind == SMK_PROJ_BANANA) continue;        /* $80:F745: sits */

        if (p->kind == SMK_PROJ_RED) {
            if (p->delay > 0) p->delay--;
            if (p->target >= 0 && p->target < nkarts && karts[p->target]) {
                const smk_kart *tk = karts[p->target];
                home(p, bearing(p->x, p->y, tk->x, tk->y));
                set_velocity(p);
            }
        }
        if (p->kind == SMK_PROJ_BANANA_AIR) {
            p->z += (int32_t)p->zv << 8;
            p->zv = (int16_t)(p->zv - 26);
            if (p->z <= 0 && p->t > 2) {                     /* landed */
                p->z = 0; p->zv = 0; p->vx = p->vy = 0; p->speed = 0;
                p->kind = SMK_PROJ_BANANA;
                continue;
            }
        }

        /* $80:FE07: the integrator, then the wall */
        int32_t nx = p->x + ((int32_t)p->vx << (SMK_POS_SHIFT - 8));
        int32_t ny = p->y + ((int32_t)p->vy << (SMK_POS_SHIFT - 8));
        int px = smk_kart_px(nx), py = smk_kart_px(ny);
        int cx = smk_kart_px(p->x), cy = smk_kart_px(p->y);
        uint8_t here = smk_track_surface(trk, px, py);
        if (here & 0x80) {
            /* MEASURED: a RED shell dies on a wall - $42 = $8000 and the
             * hop, at a class-$80 cell - where a green bounces off it */
            if (p->kind == SMK_PROJ_RED) { p->dying = true; p->zv = SMK_PROJ_DIE_HOP; p->vx = p->vy = 0; continue; }
            /* a wall: reflect the component that ran into it, at 7/8 */
            bool bx = (smk_track_surface(trk, px, cy) & 0x80) != 0;
            bool by = (smk_track_surface(trk, cx, py) & 0x80) != 0;
            if (!bx && !by) bx = by = true;               /* a corner */
            if (bx) p->vx = (int16_t)(-p->vx * SMK_PROJ_BOUNCE_NUM / 8);
            if (by) p->vy = (int16_t)(-p->vy * SMK_PROJ_BOUNCE_NUM / 8);
            p->heading = bearing(0, 0, p->vx, -p->vy);
            if (++p->bounces >= SMK_PROJ_MAX_BOUNCE) { p->dying = true; p->zv = SMK_PROJ_DIE_HOP; }
            continue;
        }
        if ((here & 0x20) && p->kind != SMK_PROJ_BANANA_AIR) {
            /* $80:F8C0: a hazard class under it - it hops out */
            p->dying = true; p->zv = SMK_PROJ_DIE_HOP;
            continue;
        }
        p->x = nx; p->y = ny;
    }
}

int smk_proj_hit(smk_proj *list, int n, const smk_kart *k, int kart_index)
{
    for (int i = 0; i < n; i++) {
        smk_proj *p = &list[i];
        if (p->kind == SMK_PROJ_NONE || p->dying) continue;
        if (p->kind == SMK_PROJ_BANANA_AIR) continue;      /* in the air */
        if (p->owner == kart_index && p->t < SMK_PROJ_OWNER_SAFE) continue;
        int dx = smk_kart_px(p->x) - smk_kart_px(k->x);
        int dy = smk_kart_px(p->y) - smk_kart_px(k->y);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx >= SMK_PROJ_HIT_R || dy >= SMK_PROJ_HIT_R) continue;
        if (k->z > SMK_BUMP_Z_MAX * 25029) continue;       /* over it */
        int kind = p->kind;
        if (kind == SMK_PROJ_BANANA) p->kind = SMK_PROJ_NONE;
        else { p->dying = true; p->zv = SMK_PROJ_DIE_HOP; p->vx = p->vy = 0; }
        return kind;
    }
    return SMK_PROJ_NONE;
}
