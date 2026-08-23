/* Super Mario Kart - native reimplementation.
 *
 * Reads assets from a Super Mario Kart (USA) ROM the user supplies.  No game
 * data is compiled into this program; only addresses and formats, which were
 * derived by reverse engineering and are documented in docs/FINDINGS.md.
 */
#ifndef SMK_H
#define SMK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- ROM ------------------------------------------------------------- */

#define SMK_ROM_SIZE   0x80000u
#define SMK_SHA1_USA   "47e103d8398cf5b7cbb42b95df3a3c270691163b"

typedef struct {
    uint8_t *data;
    size_t   size;
    char     title[22];
    bool     recognised;      /* matches the known-good USA dump */
} smk_rom;

bool     smk_rom_load(smk_rom *rom, const char *path, char *err, size_t errsz);
void     smk_rom_free(smk_rom *rom);
/* HiROM: pc = ((bank & $3F) << 16 | addr) & (size-1) */
uint32_t smk_snes_to_pc(const smk_rom *rom, uint32_t snes);

/* ---- compression ------------------------------------------------------ */

/* Decode one stream.  Returns bytes produced, or -1 on a malformed stream.
 * `consumed`, if non-NULL, receives the number of input bytes read. */
long smk_decompress(const uint8_t *src, size_t srclen, size_t off,
                    uint8_t *out, size_t outcap, size_t *consumed);

/* Decompress into `buf` at `dest`, resolving back-references against the
 * whole buffer.  Needed to reproduce assets whose streams reference bytes an
 * earlier load left in WRAM. */
long smk_decompress_into(const uint8_t *src, size_t srclen, size_t off,
                         uint8_t *buf, size_t bufsize, size_t dest,
                         size_t *consumed);

/* ---- track assets ----------------------------------------------------- */

#define SMK_TRACK_COUNT   24          /* 20 GP courses + 4 battle courses */
#define SMK_THEME_COUNT    8          /* Mario Circuit, Ghost Valley, ...     */

/* Theme (tileset + palette) for a course, from the ROM's own table $81EC2F. */
int smk_track_theme(const smk_rom *rom, int track);
#define SMK_MAP_DIM       128         /* tiles per side */
#define SMK_MAP_BYTES     (SMK_MAP_DIM * SMK_MAP_DIM)
#define SMK_TILE_PX       8
#define SMK_TILE_BYTES    64          /* Mode 7: linear, 1 byte per pixel */
#define SMK_TILE_COUNT    192
#define SMK_WORLD_PX      (SMK_MAP_DIM * SMK_TILE_PX)   /* 1024 */

typedef struct {
    uint8_t  map[SMK_MAP_BYTES];                    /* tile index per cell   */
    uint8_t  tiles[SMK_TILE_COUNT * SMK_TILE_BYTES];/* expanded 8bpp pixels  */
    uint32_t palette[256];                          /* 0xRRGGBB              */
    int      track;
    int      theme;
} smk_track;

/* Load a course.  `theme` < 0 means "use the ROM's own binding". */
bool smk_track_load(const smk_rom *rom, int track, int theme,
                    smk_track *out, char *err, size_t errsz);

/* A drivable spot to start from.  Placeholder until the real start line is
 * decoded; see the comment on the implementation. */
void smk_track_guess_start(const smk_track *t, float *x, float *y, float *angle);

/* Colour of a world pixel, wrapping at the 1024x1024 edge. */
uint32_t smk_track_texel(const smk_track *t, int wx, int wy);

/* ---- Mode 7 camera and renderer --------------------------------------- */

typedef struct {
    float x, y;        /* world position, in 1024x1024 track pixels */
    float angle;       /* radians, 0 = +x */
    float height;      /* eye height above the plane                */
    float horizon;     /* screen row of the horizon, in [0,1]       */
    float fov;         /* focal length scale                        */
} smk_camera;

void smk_render_mode7(const smk_track *t, const smk_camera *cam,
                      uint32_t *pixels, int w, int h, int pitch_px);

#endif /* SMK_H */
