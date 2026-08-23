/* Track asset loading: tilemap, Mode 7 tileset, palette. */
#include "smk.h"
#include <stdio.h>
#include <string.h>

/* Pointer tables: three bytes per entry, 16-bit address then bank. */
#define TBL_TILEMAP  0x81EB5Bu   /* 24 entries, DOUBLY compressed          */
#define TBL_TILESET  0x81EBA3u   /* packed 4bpp tiles + palette-base table */
#define TBL_PALETTE  0x81EBBBu   /* 256 colours, BGR555                    */

#define SCRATCH  0x20000u

static uint32_t table_ptr(const smk_rom *rom, uint32_t table, int index)
{
    uint32_t pc = smk_snes_to_pc(rom, table) + (uint32_t)index * 3u;
    return ((uint32_t)rom->data[pc + 2] << 16)
         | ((uint32_t)rom->data[pc + 1] << 8)
         |  (uint32_t)rom->data[pc];
}

static long load_blob(const smk_rom *rom, uint32_t table, int index,
                      uint8_t *out, size_t outcap)
{
    uint32_t pc = smk_snes_to_pc(rom, table_ptr(rom, table, index));
    return smk_decompress(rom->data, rom->size, pc, out, outcap, NULL);
}

/* The tile expander at $84E3C7.
 *
 * Source: 256 palette-base bytes, then 32 bytes per tile holding two 4-bit
 * pixels each, LOW nibble first.  A non-zero nibble is OR-ed with that tile's
 * palette base, which is how a 16-colour tile reaches anywhere in the
 * 256-colour Mode 7 palette.  Zero is the backdrop index and is left alone.
 */
static bool expand_tiles(const uint8_t *packed, size_t packedlen,
                         uint8_t *out, int count)
{
    size_t need = 0x100u + (size_t)count * 32u;
    if (packedlen < need) return false;

    size_t o = 0;
    for (int t = 0; t < count; t++) {
        uint8_t base = packed[t];
        const uint8_t *s = packed + 0x100 + (size_t)t * 32u;
        for (int k = 0; k < 32; k++) {
            uint8_t b  = s[k];
            uint8_t lo = b & 0x0F, hi = b >> 4;
            out[o++] = lo ? (uint8_t)(lo | base) : 0;
            out[o++] = hi ? (uint8_t)(hi | base) : 0;
        }
    }
    return true;
}

static uint32_t bgr555(uint16_t v)
{
    unsigned r = v & 0x1F, g = (v >> 5) & 0x1F, b = (v >> 10) & 0x1F;
    /* scale 5 bits to 8 properly, not just <<3 */
    r = (r * 255 + 15) / 31; g = (g * 255 + 15) / 31; b = (b * 255 + 15) / 31;
    return (r << 16) | (g << 8) | b;
}

bool smk_track_load(const smk_rom *rom, int track, int tileset, int palette,
                    smk_track *out, char *err, size_t errsz)
{
    static uint8_t stage[SCRATCH];
    static uint8_t packed[SCRATCH];

    if (track < 0 || track >= SMK_TRACK_COUNT) {
        snprintf(err, errsz, "track %d out of range 0..%d",
                 track, SMK_TRACK_COUNT - 1);
        return false;
    }
    memset(out, 0, sizeof *out);
    out->track = track;

    /* Tilemap: compressed twice.  The first pass yields another stream. */
    long n = load_blob(rom, TBL_TILEMAP, track, stage, sizeof stage);
    if (n < 0) { snprintf(err, errsz, "track %d: outer stream is bad", track); return false; }
    n = smk_decompress(stage, (size_t)n, 0, out->map, sizeof out->map, NULL);
    if (n != SMK_MAP_BYTES) {
        snprintf(err, errsz, "track %d: tilemap is %ld bytes, expected %d",
                 track, n, SMK_MAP_BYTES);
        return false;
    }

    /* Tileset. */
    long pn = load_blob(rom, TBL_TILESET, tileset, packed, sizeof packed);
    if (pn < 0 || !expand_tiles(packed, (size_t)pn, out->tiles, SMK_TILE_COUNT)) {
        snprintf(err, errsz, "tileset %d: cannot expand %ld bytes into %d tiles",
                 tileset, pn, SMK_TILE_COUNT);
        return false;
    }

    /* Palette. */
    long qn = load_blob(rom, TBL_PALETTE, palette, stage, sizeof stage);
    if (qn != 512) {
        snprintf(err, errsz, "palette %d: %ld bytes, expected 512", palette, qn);
        return false;
    }
    for (int i = 0; i < 256; i++)
        out->palette[i] = bgr555((uint16_t)(stage[i * 2] | stage[i * 2 + 1] << 8));

    return true;
}

uint32_t smk_track_texel(const smk_track *t, int wx, int wy)
{
    wx &= (SMK_WORLD_PX - 1);
    wy &= (SMK_WORLD_PX - 1);
    unsigned tile = t->map[(wy >> 3) * SMK_MAP_DIM + (wx >> 3)];
    if (tile >= SMK_TILE_COUNT) return t->palette[0];
    return t->palette[t->tiles[tile * SMK_TILE_BYTES + ((wy & 7) << 3) + (wx & 7)]];
}

/* Pick a plausible starting spot.
 *
 * PLACEHOLDER: the real per-track start line lives in track data that has
 * not been decoded yet.  Until then, look for the longest horizontal run of
 * a single tile whose width is road-like (4..24 tiles) and start in the
 * middle of it, facing along the run.  That lands on tarmac on every one of
 * the 24 maps, which is enough to drive around and look at things.
 */
void smk_track_guess_start(const smk_track *t, float *x, float *y, float *angle)
{
    int best_len = 0, best_tx = SMK_MAP_DIM / 2, best_ty = SMK_MAP_DIM / 2;

    for (int ty = 0; ty < SMK_MAP_DIM; ty++) {
        int run = 0;
        uint8_t cur = 0xFF;
        for (int tx = 0; tx <= SMK_MAP_DIM; tx++) {
            uint8_t v = (tx < SMK_MAP_DIM) ? t->map[ty * SMK_MAP_DIM + tx] : 0xFE;
            if (tx && v == cur) {
                run++;
            } else {
                if (run >= 4 && run <= 24 && run > best_len) {
                    best_len = run;
                    best_tx  = tx - run / 2 - 1;
                    best_ty  = ty;
                }
                run = 1;
                cur = v;
            }
        }
    }
    *x = (float)(best_tx * SMK_TILE_PX + SMK_TILE_PX / 2);
    *y = (float)(best_ty * SMK_TILE_PX + SMK_TILE_PX / 2);
    *angle = 0.0f;
}
