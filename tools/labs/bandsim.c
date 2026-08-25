/* Can the kart get INSIDE a barrier band?
 *
 * The playtest screenshot shows the kart sitting on top of the coloured
 * blocks, not stopped at their edge.  This drives at a DIAGONAL band -
 * the shape those barriers actually form - from a spread of angles, and
 * reports any approach that ends up embedded.
 */
#include "smk.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static smk_track trk;

static int embedded(const smk_kart *k)
{
    return smk_surface_solid(smk_track_surface(&trk, smk_kart_px(k->x),
                                               smk_kart_px(k->y)));
}

int main(void)
{
    memset(&trk, 0, sizeof trk);
    trk.surface[0] = 0x40;
    trk.surface[1] = 0x80;
    /* a diagonal band 3 tiles thick, like the barrier blocks */
    for (int i = 0; i < SMK_MAP_DIM; i++)
        for (int t = 0; t < 3; t++) {
            int cx = i, cy = i + t;
            if (cy >= 0 && cy < SMK_MAP_DIM)
                trk.map[cy * SMK_MAP_DIM + cx] = 1;
        }

    int bad = 0;
    for (int deg = 0; deg < 360; deg += 5) {
        smk_kart k;
        memset(&k, 0, sizeof k);
        /* start well clear, below-right of the band, aimed by `deg` */
        k.x = (int32_t)(500 * SMK_POS_ONE);
        k.y = (int32_t)(300 * SMK_POS_ONE);
        k.angle = (uint16_t)(deg * SMK_ANGLE_TURN / 360);
        k.speed = 800;
        int hit = 0, emb = 0, embf = -1;
        for (int f = 0; f < 400; f++) {
            smk_kart_face(&k);
            smk_kart_gravity(&k);
            smk_kart_move(&k, &trk);
            if (k.bounce_cool) hit = 1;
            if (embedded(&k)) { emb = 1; if (embf < 0) embf = f; }
        }
        if (emb) {
            bad++;
            printf("  deg %3d: EMBEDDED at frame %d (pos %d,%d)\n",
                   deg, embf, smk_kart_px(k.x), smk_kart_px(k.y));
        }
        (void)hit;
    }
    printf("%d of 72 approach angles ended up inside the band\n", bad);
    return bad ? 1 : 0;
}
