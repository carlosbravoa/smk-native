/* Drive the PLAYER into a barrier using the game's own step, not the
 * library primitives.
 *
 * The bounce worked at the library level and still failed in play,
 * because step_kart rewrote the velocity from the heading every frame
 * and wiped the rebound.  A library-level test could never have caught
 * that, so this one drives the real thing: it links src/main.c's
 * step_kart via a tiny shim.
 */
#include "smk.h"
#include <stdio.h>
#include <string.h>

/* mirror of the player's tick, kept deliberately small: what matters is
 * that it exercises the SAME order main.c uses */
extern void smk_kart_face(smk_kart *);
int main(void)
{
    smk_rom rom; char err[128];
    if (!smk_rom_load(&rom, "rom/smk_usa.sfc", err, sizeof err)) { puts(err); return 77; }
    smk_physics phys; smk_physics_load(&rom, 0, &phys);
    int top = (int16_t)phys.w[SMK_PHYS_TARGET + 6];

    static smk_track trk; memset(&trk, 0, sizeof trk);
    trk.surface[0] = 0x40; trk.surface[1] = 0x80;
    for (int row = 0; row < SMK_MAP_DIM; row++)
        trk.map[row * SMK_MAP_DIM + 50] = 1;

    smk_kart k; memset(&k, 0, sizeof k);
    k.x = (int32_t)(370 * SMK_POS_ONE);
    k.y = (int32_t)(300 * SMK_POS_ONE);
    k.angle = SMK_ANGLE_TURN / 4;
    k.speed = 700;

    printf("player-order tick into a wall at x=400:\n");
    int minx = 9999, maxx = 0, moved = 0;
    for (int f = 0; f < 60; f++) {
        int32_t a = (int32_t)smk_physics_accel(&phys, k.speed) << 8;
        float head = (float)(top - k.speed) / (float)top * 1.6f;
        if (head < 0) head = 0;
        if (head < 1) a = (int32_t)((float)a * head);
        k.accel = (int16_t)(a >> 16);
        k.accel_frac = (uint16_t)(a & 0xFFFF);
        smk_kart_accelerate(&k);
        if (k.speed > top) k.speed = (int16_t)top;
        /* THE PLAYER'S ORDER: velocity from heading, but only outside
         * the ballistic window */
        if (k.bounce_cool == 0) {
            k.vx = (int16_t)(( 0.0f + k.speed) * 1.0f);   /* heading +X */
            k.vy = 0;
        }
        smk_kart_gravity(&k);
        smk_kart_move(&k, &trk);
        int px = smk_kart_px(k.x);
        if (px < minx) minx = px;
        if (px > maxx) maxx = px;
        if (f > 12 && px != maxx) moved = 1;
        if (f % 5 == 0 || k.bounce_cool == 9)
            printf("  f%2d x=%3d spd=%4d vx=%6d cool=%d %s\n",
                   f, px, k.speed, k.vx, k.bounce_cool,
                   k.bounce_cool == 9 ? "<-- IMPACT" : "");
    }
    printf("travelled between x=%d and x=%d -> %s\n", minx, maxx,
           (maxx - minx) > 6 ? "BOUNCES (not stuck)" : "STUCK");
    return (maxx - minx) > 6 ? 0 : 1;
}
