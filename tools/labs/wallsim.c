/* Drive our own kart into a wall and watch what happens.
 *
 * A synthetic track: tile 0 is road ($40), tile 1 is a barrier ($80).
 * The kart starts left of a vertical barrier and drives right.
 */
#include "smk.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

int main(void)
{
    static smk_track trk;
    memset(&trk, 0, sizeof trk);
    trk.surface[0] = 0x40;              /* road    */
    trk.surface[1] = 0x80;              /* barrier */
    for (int i = 0; i < SMK_MAP_BYTES; i++) trk.map[i] = 0;
    /* a vertical wall at world x = 400..407 (tile column 50) */
    for (int row = 0; row < SMK_MAP_DIM; row++)
        trk.map[row * SMK_MAP_DIM + 50] = 1;

    smk_kart k;
    memset(&k, 0, sizeof k);
    k.x = (int32_t)(360 * SMK_POS_ONE);
    k.y = (int32_t)(300 * SMK_POS_ONE);
    k.angle = SMK_ANGLE_TURN / 4;       /* +X */
    k.speed = 800;

    printf("  f     x     y    vx    vy  spd  cool  surf@kart\n");
    for (int f = 0; f < 40; f++) {
        if (k.bounce_cool == 0) smk_kart_face(&k);
        else                    smk_kart_face(&k);   /* gated internally */
        smk_kart_gravity(&k);
        smk_kart_move(&k, &trk);
        int px = smk_kart_px(k.x), py = smk_kart_px(k.y);
        printf("  %2d %5d %5d %5d %5d %4d   %2d   $%02X\n",
               f, px, py, k.vx, k.vy, k.speed, k.bounce_cool,
               smk_track_surface(&trk, px, py));
    }
    return 0;
}
