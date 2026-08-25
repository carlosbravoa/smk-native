/* The MEASURED SMK ground projection (docs/NOTES.md 083).
 *
 * The game precomputes its per-scanline Mode 7 tables through the DSP-1
 * raster command at boot; with the DSP protocol decoded, the tables read
 * out as an exact law.  In SNES frame lines (112-line view):
 *
 *     lines  0..23   sky (the HDMA hold band)
 *     lines 24..107  ground, i = line - 24:
 *         scale(i)   = 19.375 / (i + 3.65)   world px per screen px
 *         forward(i) = scale(i) * (102 - line)
 *     line 102       the camera's own ground row (the kart sits here)
 *
 * Derived quantities (all from the ROM's own DSP $02 build call, fitted
 * exactly against the generated table): camera height 18.5 world px,
 * pitch Azs = $3400, Les = 256, VOffset/sinAzs = 77.6 lines, Vs base
 * -74.  The scanline law is rendered at host resolution by mapping our
 * rows onto the 112-line frame.
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

void smk_render_mode7(const smk_track *t, const smk_camera *cam,
                      uint32_t *pixels, int w, int h, int pitch_px)
{
    const float sa = sinf(cam->angle), ca = cosf(cam->angle);

    /* the measured frame geometry, in SNES frame lines */
    const float SKY_LINE = 24.0f, CAM_LINE = 102.0f;
    const float SCALE_K = 19.375f, SCALE_C = 3.65f;
    const float l2h = (float)h / 112.0f;          /* our px per frame line */
    const int horizon = (int)(SKY_LINE * l2h);

    const uint32_t sky_far  = t->palette[1];
    const uint32_t sky_near = t->palette[2];

    for (int sy = 0; sy < h; sy++) {
        uint32_t *row = pixels + (size_t)sy * (size_t)pitch_px;

        float line = (float)sy / l2h;
        if (line < SKY_LINE) {
            uint32_t c = sky_colour(sy, horizon, sky_near, sky_far);
            for (int sx = 0; sx < w; sx++) row[sx] = c;
            continue;
        }

        float i = line - SKY_LINE;
        float scale = SCALE_K / (i + SCALE_C);    /* world px / SNES px  */
        float fwd = scale * (CAM_LINE - line);    /* ahead of the camera */

        float cx = cam->x + ca * fwd;
        float cy = cam->y + sa * fwd;

        /* one HOST pixel of horizontal travel */
        float step = scale * 256.0f / (float)w;
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
}

bool smk_project(const smk_camera *cam, float wx, float wy,
                 int w, int h, float *sx, float *sy, float *scale)
{
    /* The SPRITE projection - a separate, much flatter law than the
     * ground (both are the game's own: the DSP $02 for sprites has the
     * eye 256 px behind the kart; fitted from hundreds of live OAM
     * samples, docs/NOTES.md 082/083):
     *
     *     d   = depth + 256
     *     x   = centre + 256 * lateral / d      (SNES px)
     *     row = 97 + 1250 / d                   (frame lines)
     */
    const float sa = sinf(cam->angle), ca = cosf(cam->angle);

    float dx = wx - cam->x, dy = wy - cam->y;
    while (dx >  SMK_WORLD_PX / 2) dx -= SMK_WORLD_PX;
    while (dx < -SMK_WORLD_PX / 2) dx += SMK_WORLD_PX;
    while (dy >  SMK_WORLD_PX / 2) dy -= SMK_WORLD_PX;
    while (dy < -SMK_WORLD_PX / 2) dy += SMK_WORLD_PX;

    float zf =  dx * ca + dy * sa;          /* along the camera's forward */
    float xr = -dx * sa + dy * ca;          /* to its right               */
    if (zf < -240.0f) return false;         /* behind the eye             */

    float d = zf + 256.0f;
    float line = 97.0f + 1250.0f / d;
    *sy = line * (float)h / 112.0f;
    *sx = (float)w * 0.5f + xr * (float)w / d;
    *scale = (float)w / d;                  /* screen px per world px     */
    return *sy < (float)h && *sy > 0.0f && d > 16.0f;
}
