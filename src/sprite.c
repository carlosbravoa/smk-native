/* Kart sprites: uncompressed 4bpp frames read straight out of the ROM.
 *
 * Found by logging DMA during a race: the game streams 128-byte quarters
 * from banks $C0/$C2/$C4/$C5 at addresses $200 apart, so a frame is 512
 * bytes = 16 tiles = 32x32 pixels.  The tiles are stored in PPU order, i.e.
 * a 4x4 sprite with a 16-tile row stride.
 */
#include "smk.h"
#include <string.h>

#define KART_SHEET_DEFAULT  0xC02000u   /* observed in the DMA log */

/* One 8x8 4bpp tile -> 64 palette indices. */
static void tile_px(const uint8_t *src, uint8_t out[64])
{
    for (int pair = 0; pair < 2; pair++) {
        const uint8_t *p = src + pair * 16;
        for (int y = 0; y < 8; y++) {
            uint8_t lo = p[y * 2], hi = p[y * 2 + 1];
            for (int x = 0; x < 8; x++) {
                int bit = 7 - x;
                int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                out[y * 8 + x] |= (uint8_t)(v << (pair * 2));
            }
        }
    }
}

bool smk_sprites_load(const smk_rom *rom, uint32_t base, smk_sprites *out)
{
    memset(out, 0, sizeof *out);
    if (base == 0) base = KART_SHEET_DEFAULT;
    uint32_t pc = smk_snes_to_pc(rom, base);

    out->frames = 0;
    for (int f = 0; f < SMK_SPR_FRAMES; f++) {
        /* frames advance across, then down, in the PPU's tile grid */
        int n0 = (f % 4) * 4 + (f / 4) * 64;
        for (int tr = 0; tr < 4; tr++) {
            for (int tc = 0; tc < 4; tc++) {
                uint32_t off = pc + (uint32_t)(n0 + tr * 16 + tc) * 32u;
                if (off + 32 > rom->size) return out->frames > 0;
                uint8_t t[64];
                memset(t, 0, sizeof t);
                tile_px(rom->data + off, t);
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                        out->px[f][(tr * 8 + y) * SMK_SPR_PX + tc * 8 + x] =
                            t[y * 8 + x];
            }
        }
        out->frames = f + 1;
    }
    return true;
}

void smk_draw_sprite(const smk_sprites *s, int frame, const uint32_t *palette,
                     int pal_base, int cx, int cy, int scale,
                     uint32_t *pixels, int w, int h, int pitch_px)
{
    if (frame < 0 || frame >= s->frames || scale < 1) return;
    const uint8_t *src = s->px[frame];
    int size = SMK_SPR_PX * scale;
    int x0 = cx - size / 2, y0 = cy - size;      /* anchored at the wheels */

    for (int y = 0; y < size; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h) continue;
        const uint8_t *row = src + (y / scale) * SMK_SPR_PX;
        uint32_t *dst = pixels + (size_t)sy * (size_t)pitch_px;
        for (int x = 0; x < size; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= w) continue;
            uint8_t v = row[x / scale];
            if (v == 0) continue;                /* index 0 is transparent */
            dst[sx] = palette[(pal_base + v) & 0xFF];
        }
    }
}


/* Pick a rotation frame within a size tier.
 *
 * INFERRED.  The sheet is three tiers of ~11 rotation steps; we centre on
 * the straight-from-behind pose and lean either side of it.  The ROM's own
 * rule is a function of heading relative to the camera and has not been
 * decoded - see the header.
 */
int smk_sprite_frame(int tier, float lean)
{
    if (lean < -1.0f) lean = -1.0f;
    if (lean >  1.0f) lean =  1.0f;
    int span = 3;                                   /* frames either side */
    int f = SMK_SPR_REAR + (int)(lean * (float)span + (lean < 0 ? -0.5f : 0.5f));
    int lo = tier, hi = tier + SMK_SPR_TIER_LEN - 1;
    f += tier;
    if (f < lo) f = lo;
    if (f > hi) f = hi;
    if (f >= SMK_SPR_FRAMES) f = SMK_SPR_FRAMES - 1;
    return f;
}
