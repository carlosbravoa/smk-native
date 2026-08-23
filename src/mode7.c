/* Perspective ground-plane renderer.
 *
 * The SNES produces this with a per-scanline affine transform (M7A-M7D
 * rewritten by HDMA every line).  We are not bound by the PPU, so the same
 * geometry is computed directly and at whatever resolution the host wants -
 * the projection is identical, it just is not quantised to 256x224.
 *
 * For a screen row `sy` below the horizon, the ground distance is
 *
 *     z = height * focal / (sy - horizon)
 *
 * and one screen pixel of horizontal travel covers z/focal of world space,
 * rotated into the camera's heading.  That is the whole renderer.
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
    const float focal = cam->fov * (float)w;
    const int   horizon = (int)(cam->horizon * (float)h);

    /* Sky: a gradient between two colours sampled from the track palette so
     * it always belongs to the same world as the ground. */
    const uint32_t sky_far  = t->palette[1];
    const uint32_t sky_near = t->palette[2];

    for (int sy = 0; sy < h; sy++) {
        uint32_t *row = pixels + (size_t)sy * (size_t)pitch_px;

        if (sy <= horizon) {
            uint32_t c = sky_colour(sy, horizon, sky_near, sky_far);
            for (int sx = 0; sx < w; sx++) row[sx] = c;
            continue;
        }

        float dy = (float)(sy - horizon);
        float z  = cam->height * focal / dy;

        /* centre of this scanline in world space */
        float cx = cam->x + ca * z;
        float cy = cam->y + sa * z;

        /* world-space step for one screen pixel to the right */
        float step = z / focal;
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
