/* The horizon layer - the scenery above the track (NOTES 117).
 *
 * The race's sky band is 24 scanlines of BG mode 0 over the Mode 7 plane
 * (NOTES 114): inside it the game draws two scenery planes and the HUD.
 * The FAR plane is decoded here, both halves matched byte-for-byte against
 * the running game's VRAM:
 *
 *   gfx_d[theme]  ($81EBEB)  the tiles, 2bpp, 8x8 - hills and trees,
 *                            clouds, mountains, castle battlements, ice,
 *                            a star field.  The game DMAs them to the
 *                            layer's character base (VRAM word $7000).
 *   gfx_e[theme]  ($81EC03)  the tilemap that arranges them: exactly 1536
 *                            bytes for every theme = 768 entries = 32 x 24,
 *                            copied to VRAM word $7800.  (Rendered as
 *                            TILES it looks like noise, which is what hid
 *                            it - it is a map.)
 *
 * A map entry is the usual SNES word: tile index in bits 0-9, palette in
 * 10-12, then priority, hflip, vflip.  Colour 0 is transparent, so the
 * backdrop shows through - that is the sky.
 *
 * LABELLED, not yet measured: the horizontal scroll law.  The game writes
 * the layer's HOFS every frame; we turn the panorama once per full turn
 * of the camera (256 px of map for 360 degrees), which is the natural
 * reading of a 32-tile-wide map on a 256-px screen.
 */
#include "smk.h"
#include <string.h>

#define HZ_TILES_TBL  0x81EBEBu   /* gfx_d: the horizon tiles, 2bpp, per theme */
#define HZ_MAP_TBL    0x81EC03u   /* gfx_e: its 32x24 tilemap, 1536 B per theme */

static uint32_t table_ptr3(const smk_rom *rom, uint32_t table, int index)
{
    uint32_t pc = smk_snes_to_pc(rom, table) + (uint32_t)index * 3u;
    return ((uint32_t)rom->data[pc + 2] << 16)
         | ((uint32_t)rom->data[pc + 1] << 8)
         |  (uint32_t)rom->data[pc];
}

bool smk_horizon_load(const smk_rom *rom, int theme, smk_horizon *hz)
{
    static uint8_t tbuf[0x4000], mbuf[0x4000];
    memset(hz, 0, sizeof *hz);
    if (theme < 0) theme = 0;
    theme %= SMK_THEME_COUNT;

    long nt = smk_decompress_into(rom->data, rom->size,
                                  smk_snes_to_pc(rom, table_ptr3(rom, HZ_TILES_TBL, theme)),
                                  tbuf, sizeof tbuf, 0, NULL);
    long nm = smk_decompress_into(rom->data, rom->size,
                                  smk_snes_to_pc(rom, table_ptr3(rom, HZ_MAP_TBL, theme)),
                                  mbuf, sizeof mbuf, 0, NULL);
    if (nt < 16 || nm < SMK_HZ_W * SMK_HZ_H * 2) return false;

    hz->tiles = (int)(nt / 16);
    if (hz->tiles > SMK_HZ_TILES) hz->tiles = SMK_HZ_TILES;
    for (int t = 0; t < hz->tiles; t++) {
        const uint8_t *s = tbuf + (size_t)t * 16u;
        for (int y = 0; y < 8; y++) {
            uint8_t lo = s[y * 2], hi = s[y * 2 + 1];
            for (int x = 0; x < 8; x++) {
                int b = 7 - x;
                hz->px[t][y * 8 + x] = (uint8_t)(((lo >> b) & 1) | (((hi >> b) & 1) << 1));
            }
        }
    }
    for (int i = 0; i < SMK_HZ_W * SMK_HZ_H; i++)
        hz->map[i] = (uint16_t)(mbuf[i * 2] | (mbuf[i * 2 + 1] << 8));
    /* Which rows of the map the band shows: the game picks them with the
     * layer's vertical scroll, which we have not measured.  The scenery
     * sits on the horizon, so align the LAST non-empty row with the
     * bottom of the band - data-driven, and LABELLED as our rule. */
    hz->last_row = 0;
    for (int r = 0; r < SMK_HZ_H; r++)
        for (int c = 0; c < SMK_HZ_W; c++)
            if ((hz->map[r * SMK_HZ_W + c] & 0x3FF) != 0) { hz->last_row = r; break; }
    hz->ok = true;
    return true;
}

void smk_horizon_draw(const smk_horizon *hz, const uint32_t *palette,
                      uint16_t heading, int band_h, uint32_t *fb,
                      int w, int h, int scale)
{
    if (!hz->ok || band_h <= 0 || scale < 1) return;
    /* one panorama per full turn of the camera (LABELLED - see above) */
    int scroll = (int)(((uint32_t)heading * (SMK_HZ_W * 8)) >> 16);

    int rows = band_h / (8 * scale);
    if (rows < 1) rows = 1;
    if (rows > SMK_HZ_H) rows = SMK_HZ_H;
    /* The band shows the map's TOP rows.  Which rows the game picks is its
     * layer vertical scroll, not yet measured; the top rows are where the
     * scenery sits in every theme's map, and they match the captured
     * Mario Circuit frame.  LABELLED. */
    int top_row = 0;
    for (int py = 0; py < band_h; py++) {
        if (py >= h) break;
        int my = py / scale + top_row * 8;       /* map pixel row */
        if (my >= (top_row + rows) * 8) break;
        uint32_t *dst = fb + (size_t)py * (size_t)w;
        for (int px = 0; px < w; px++) {
            int mx = (px / scale + scroll) & (SMK_HZ_W * 8 - 1);
            uint16_t e = hz->map[(my >> 3) * SMK_HZ_W + (mx >> 3)];
            int tn = e & 0x3FF;
            if (tn >= hz->tiles) continue;
            int pal = (e >> 10) & 7;
            int tx = mx & 7, ty = my & 7;
            if (e & 0x4000) tx = 7 - tx;         /* hflip */
            if (e & 0x8000) ty = 7 - ty;         /* vflip */
            uint8_t v = hz->px[tn][ty * 8 + tx];
            if (!v) continue;                    /* colour 0 is the sky */
            dst[px] = palette[(SMK_HZ_PAL + pal * 4 + v) & 0xFF];
        }
    }
}
