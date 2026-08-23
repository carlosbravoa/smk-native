/* Track asset loading: tilemap, Mode 7 tileset, palette. */
#include "smk.h"
#include <stdio.h>
#include <string.h>

/* Pointer tables: three bytes per entry, 16-bit address then bank. */
#define TBL_TILEMAP  0x81EB5Bu   /* 24 entries, DOUBLY compressed          */
#define TBL_TILESET  0x81EBA3u   /* packed 4bpp tiles + palette-base table */
#define TBL_PALETTE  0x81EBBBu   /* 256 colours, BGR555                    */
#define TBL_THEME    0x81EC2Fu   /* 24 bytes, track -> theme*2 ($81EC5E)   */
#define SURF_BLOB    0x87FDBAu   /* compressed surface behaviour data       */
#define TBL_SURF_OFF 0x81EB4Bu   /* per-theme 16-bit offset into that blob  */

/* We mirror WRAM bank $7F, because the game's loads are not independent:
 * each decompresses through the staging area at $7F:C000 and the next one
 * can reference what the last left behind.  Same buffer, same order, same
 * result - see $81E67A. */
#define WRAM_SIZE     0x10000u
#define STAGE_OFF     0xC000u    /* where loads are decompressed to first  */
#define MAP_OFF       0x0000u    /* final tilemap                          */
#define TILES_OFF     0x4000u    /* expanded Mode 7 tiles                  */

static uint32_t table_ptr(const smk_rom *rom, uint32_t table, int index)
{
    uint32_t pc = smk_snes_to_pc(rom, table) + (uint32_t)index * 3u;
    return ((uint32_t)rom->data[pc + 2] << 16)
         | ((uint32_t)rom->data[pc + 1] << 8)
         |  (uint32_t)rom->data[pc];
}

int smk_track_theme(const smk_rom *rom, int track)
{
    if (track < 0 || track >= SMK_TRACK_COUNT) return 0;
    /* the ROM stores theme*2, because the caller uses it as a *1.5 index
     * into a three-byte-per-entry table */
    return rom->data[smk_snes_to_pc(rom, TBL_THEME) + track] >> 1;
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

/* Surface behaviour table, per $81EB11:
 *     decompress $87:FDBA, then copy 192 bytes starting at
 *     $81EB4B[theme] into RAM $0B00.
 * 192 is not a coincidence - it is one byte per Mode 7 tile. */
static bool load_surface(const smk_rom *rom, int theme, uint8_t *out)
{
    static uint8_t buf[WRAM_SIZE];
    memset(buf, 0, sizeof buf);
    long n = smk_decompress_into(rom->data, rom->size,
                                 smk_snes_to_pc(rom, SURF_BLOB),
                                 buf, WRAM_SIZE, 0, NULL);
    if (n < 0) return false;
    uint32_t tp = smk_snes_to_pc(rom, TBL_SURF_OFF) + (uint32_t)theme * 2u;
    uint32_t off = (uint32_t)rom->data[tp] | ((uint32_t)rom->data[tp + 1] << 8);
    if (off >= WRAM_SIZE) return false;
    /* Some themes read past the end of the decompressed blob - Rainbow Road
     * most of all, which is why almost all of its tiles come back $00
     * ("nothing there").  The game reads whatever WRAM held; we read the
     * zeroed buffer, which is the same answer for a freshly cleared bank. */
    (void)n;
    memcpy(out, buf + off, SMK_TILE_COUNT);
    return true;
}

/* Decompress table[index] into the WRAM image at `dest`. */
static long load_into(const smk_rom *rom, uint32_t table, int index,
                      uint8_t *wram, size_t dest)
{
    uint32_t pc = smk_snes_to_pc(rom, table_ptr(rom, table, index));
    return smk_decompress_into(rom->data, rom->size, pc,
                               wram, WRAM_SIZE, dest, NULL);
}

bool smk_track_load(const smk_rom *rom, int track, int theme,
                    smk_track *out, char *err, size_t errsz)
{
    static uint8_t wram[WRAM_SIZE];

    if (track < 0 || track >= SMK_TRACK_COUNT) {
        snprintf(err, errsz, "track %d out of range 0..%d",
                 track, SMK_TRACK_COUNT - 1);
        return false;
    }
    if (theme < 0) theme = smk_track_theme(rom, track);
    if (theme >= SMK_THEME_COUNT) theme %= SMK_THEME_COUNT;

    memset(out, 0, sizeof *out);
    memset(wram, 0, sizeof wram);
    out->track = track;
    out->theme = theme;

    /* --- tilemap, in the same two steps as $81E745 ------------------- */
    if (load_into(rom, TBL_TILEMAP, track, wram, STAGE_OFF) < 0) {
        snprintf(err, errsz, "track %d: outer tilemap stream is bad", track);
        return false;
    }
    long n = smk_decompress_into(wram + STAGE_OFF, WRAM_SIZE - STAGE_OFF, 0,
                                 wram, WRAM_SIZE, MAP_OFF, NULL);
    if (n != SMK_MAP_BYTES) {
        snprintf(err, errsz, "track %d: tilemap is %ld bytes, expected %d",
                 track, n, SMK_MAP_BYTES);
        return false;
    }
    memcpy(out->map, wram + MAP_OFF, SMK_MAP_BYTES);

    /* --- tileset, staged over the same area, as $81E6D4 does ---------- */
    if (load_into(rom, TBL_TILESET, theme, wram, STAGE_OFF) < 0) {
        snprintf(err, errsz, "theme %d: tileset stream is bad", theme);
        return false;
    }
    /* The game always expands 192 tiles regardless of how much the stream
     * produced, reading past the end into whatever WRAM held.  Reproduce
     * that rather than refusing - several themes depend on it. */
    expand_tiles(wram + STAGE_OFF, WRAM_SIZE - STAGE_OFF,
                 out->tiles, SMK_TILE_COUNT);

    /* --- palette ----------------------------------------------------- */
    long qn = load_into(rom, TBL_PALETTE, theme, wram, STAGE_OFF);
    if (qn != 512) {
        snprintf(err, errsz, "theme %d: palette is %ld bytes, expected 512",
                 theme, qn);
        return false;
    }
    for (int i = 0; i < 256; i++)
        out->palette[i] = bgr555((uint16_t)(wram[STAGE_OFF + i * 2]
                                          | wram[STAGE_OFF + i * 2 + 1] << 8));

    /* --- surface behaviour ------------------------------------------- */
    if (!load_surface(rom, theme, out->surface)) {
        snprintf(err, errsz, "theme %d: cannot load the surface table", theme);
        return false;
    }
    return true;
}

uint8_t smk_track_surface(const smk_track *t, int wx, int wy)
{
    wx &= (SMK_WORLD_PX - 1);
    wy &= (SMK_WORLD_PX - 1);
    unsigned tile = t->map[(wy >> 3) * SMK_MAP_DIM + (wx >> 3)];
    return tile < SMK_TILE_COUNT ? t->surface[tile] : 0;
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
 * not been decoded yet (roadmap P2).  Until then, look for the longest
 * horizontal run of a single non-solid tile whose width is road-like
 * (4..24 tiles) and start in the middle of it, facing along the run.  That
 * lands on drivable ground on every one of the 24 maps.
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
                bool drivable = cur < SMK_TILE_COUNT
                                && !smk_surface_solid(t->surface[cur]);
                if (drivable && run >= 4 && run <= 24 && run > best_len) {
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
