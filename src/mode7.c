/* The SMK projection, derived from the ROM's own DSP-1 geometry
 * (docs/NOTES.md 083/084) and self-consistent for ground AND sprites.
 *
 * The boot raster stream feeds Vs = line - 98 into the DSP with the
 * race camera (Les 256, Lfe 256, Azs $3400 -> camera height 18.6 world
 * px).  Its own arithmetic then gives, per SNES frame line L:
 *
 *     depth(L)  = 4972 / (L - 20.36)          world px from the EYE
 *     scale(L)  = depth(L) / 256              world px per SNES pixel
 *
 * - the depth/scale ratio comes out at exactly Les = 256, which is the
 * cross-check that the whole chain is right.  Line 20.36 is the horizon
 * pole; the game blanks 24 lines of sky above the first ground line.
 *
 * The player's kart sits at line 102 (measured), so its depth from the
 * eye is 4972/81.6 = 61 world px: the camera TRAILS the kart by 61 px.
 * Every other kart is projected with the same law, which is what makes
 * sprites and ground agree.
 */
#include "smk.h"
#include <math.h>

/* Colours used above the horizon, as a vertical gradient. */
static uint32_t sky_colour(int sy, int horizon, uint32_t near_c, uint32_t far_c)
{
    if (horizon <= 0) return far_c;
    float t = (float)sy / (float)horizon;          /* 0 at top, 1 at horizon */
    if (t < 0) t = 0; else if (t > 1) t = 1;
    unsigned r = (unsigned)(((far_c >> 16) & 0xFF) * (1 - t) + ((near_c >> 16) & 0xFF) * t);
    unsigned g = (unsigned)(((far_c >>  8) & 0xFF) * (1 - t) + ((near_c >>  8) & 0xFF) * t);
    unsigned b = (unsigned)(((far_c      ) & 0xFF) * (1 - t) + ((near_c      ) & 0xFF) * t);
    return (r << 16) | (g << 8) | b;
}

/* The horizon layer, drawn into the sky band after it is filled with the
 * backdrop colour.  Set by the caller; NULL leaves the band flat. */
static const smk_horizon *render_horizon;
static uint16_t render_heading;
void smk_render_set_horizon(const smk_horizon *hz, uint16_t heading)
{
    render_horizon = hz;
    render_heading = heading;
}

void smk_render_mode7(const smk_track *t, const smk_camera *cam,
                      uint32_t *pixels, int w, int h, int pitch_px)
{
    const float sa = sinf(cam->angle), ca = cosf(cam->angle);
    const float l2h = (float)h / 112.0f;          /* host px per frame line */
    const int horizon = (int)(SMK_SKY_LINES * l2h);

    /* The sky is the BACKDROP colour, palette entry 0 - measured from a
     * real race frame (MAME pixel dump, NOTES 114): Mario Circuit's band
     * above the horizon is $4BBF = (248,232,144), which is exactly
     * CGRAM[0].  The old vertical gradient from entries 1-2 was invented.
     * The theme's HILL graphics that sit on it are still missing (S5). */
    const uint32_t sky_far  = t->palette[1];
    const uint32_t sky_near = t->palette[2];

    for (int sy = 0; sy < h; sy++) {
        uint32_t *row = pixels + (size_t)sy * (size_t)pitch_px;

        float line = (float)sy / l2h;
        if (line < SMK_SKY_LINES) {
            uint32_t c = t->palette[0];      /* the backdrop is the sky */
            (void)sky_colour; (void)horizon; (void)sky_far; (void)sky_near;
            for (int sx = 0; sx < w; sx++) row[sx] = c;
            continue;
        }

        float depth = SMK_PROJ_K / (line - SMK_PROJ_H);   /* from the eye */
        float fwd = depth - SMK_CAM_TRAIL;                /* from the kart */

        float cx = cam->x + ca * fwd;
        float cy = cam->y + sa * fwd;

        /* one HOST pixel of horizontal travel */
        float step = depth / (SMK_PROJ_LES * (float)w / 256.0f);
        float sx_step = -sa * step;
        float sy_step =  ca * step;

        float wx = cx - sx_step * (float)(w / 2);
        float wy = cy - sy_step * (float)(w / 2);

        for (int sx = 0; sx < w; sx++) {
            row[sx] = smk_track_texel(t, (int)floorf(wx), (int)floorf(wy));
            wx += sx_step;
            wy += sy_step;
        }
    }

    /* the scenery above the horizon (NOTES 117): the game's own band is 24
     * scanlines of mode 0, so ours is the same in frame lines */
    if (render_horizon && render_horizon->ok) {
        int scale = (int)(l2h + 0.5f);
        if (scale < 1) scale = 1;
        int band = (int)(SMK_SKY_LINES * l2h);
        smk_horizon_draw(render_horizon, t->palette, render_heading,
                         band, pixels, w, h, scale);
    }
}

bool smk_project(const smk_camera *cam, float wx, float wy,
                 int w, int h, float *sx, float *sy, float *scale)
{
    const float sa = sinf(cam->angle), ca = cosf(cam->angle);

    float dx = wx - cam->x, dy = wy - cam->y;
    while (dx >  SMK_WORLD_PX / 2) dx -= SMK_WORLD_PX;
    while (dx < -SMK_WORLD_PX / 2) dx += SMK_WORLD_PX;
    while (dy >  SMK_WORLD_PX / 2) dy -= SMK_WORLD_PX;
    while (dy < -SMK_WORLD_PX / 2) dy += SMK_WORLD_PX;

    float zf =  dx * ca + dy * sa;          /* ahead of the KART          */
    float xr = -dx * sa + dy * ca;          /* to its right               */

    float d = zf + SMK_CAM_TRAIL;           /* depth from the EYE         */
    if (d < 12.0f) return false;            /* behind, or on top of, us   */

    float line = SMK_PROJ_H + SMK_PROJ_K / d;
    *sy = line * (float)h / 112.0f;
    *sx = (float)w * 0.5f + xr * (SMK_PROJ_LES * (float)w / 256.0f) / d;
    /* screen px per world px, in HOST pixels */
    *scale = (SMK_PROJ_LES * (float)w / 256.0f) / d;
    return *sy < (float)h && *sy > 0.0f;
}
