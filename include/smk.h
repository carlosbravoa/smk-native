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

/* Surface behaviour, one byte per tile index, from the ROM's table.
 * The physics at $80FA8C reads exactly this array (RAM $0B00) and the
 * collision test at $80F8A7 is `and #$0020`. */
#define SMK_SURF_SOLID   0x20u    /* bit 5: blocks the kart                 */
#define SMK_SURF_SPECIAL 0x80u    /* bit 7: handled separately ($80FA8F)    */

typedef struct {
    uint8_t  map[SMK_MAP_BYTES];                    /* tile index per cell   */
    uint8_t  surface[SMK_TILE_COUNT];               /* behaviour per tile    */
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

/* Surface byte under a world position, exactly as $80FA62 computes it:
 * index = (y >> 3) * 128 + (x >> 3), then map -> surface table. */
uint8_t smk_track_surface(const smk_track *t, int wx, int wy);
static inline bool smk_surface_solid(uint8_t s) { return (s & SMK_SURF_SOLID) != 0; }

/* ---- Kart state, in the game's own arithmetic -------------------------
 *
 * These are the ROM's units, not convenient ones.  See docs/NOTES.md 016-017.
 *
 *   position   16.16 fixed point, in track pixels.  The ROM keeps the
 *              fraction and the integer in separate words ($16/$18 for X,
 *              $1A/$1C for Y); we keep one int32, which is the same number.
 *   velocity   8.8 fixed point, pixels per frame ($22,x / $24,x).
 *   angle      16-bit, 65536 = one full turn ($2A,x).  0 points along -Y
 *              and it increases clockwise, because the ROM builds the
 *              velocity as (sin, -cos) * speed.
 *
 * The integration at $80879D is exactly `position += velocity << 8`.
 */
#define SMK_POS_SHIFT   16
#define SMK_POS_ONE     (1 << SMK_POS_SHIFT)
#define SMK_WORLD_FIX   ((int32_t)SMK_WORLD_PX * SMK_POS_ONE)
#define SMK_VEL_SHIFT   8
#define SMK_VEL_ONE     (1 << SMK_VEL_SHIFT)    /* 1 pixel per frame */
#define SMK_ANGLE_TURN  65536

/* Field names carry the ROM offsets they mirror, because the layout is the
 * ROM's: the kart array lives at WRAM $1000, eight karts, stride $100. */
typedef struct {
    int32_t  x, y;          /* $16/$18 and $1A/$1C - 16.16 position     */
    int16_t  vx, vy;        /* $22 / $24 - 8.8 velocity, px per frame   */
    uint16_t angle;         /* $2A - 65536 = a turn, 0 = -Y, clockwise  */
    /* Speed and acceleration are both 32-bit, split across two words,
     * and the *high* word is the 8.8 value handed to DSP-1 as the radius. */
    int16_t  speed;         /* $EA */
    uint16_t speed_frac;    /* $E8 */
    int16_t  accel;         /* $EE */
    uint16_t accel_frac;    /* $EC */
} smk_kart;

/* $80A4E1: speed += acceleration as one 32-bit add, then clamp at zero. */
void smk_kart_accelerate(smk_kart *k);

/* ---- Physics tables ---------------------------------------------------
 *
 * The ROM keeps these as 64 BYTES per engine class; the loader at $81FEB6
 * widens each to a word by shifting left 4 and writes them to WRAM $0690.
 * We read them from the ROM the same way, so no game data is compiled in.
 *
 *   words  0..15   acceleration, indexed by current speed   (WRAM $0690)
 *   words 16..31   target speed, by character stat and class (WRAM $06B0)
 *   words 32..63   further per-class constants, not yet identified
 */
#define SMK_PHYS_WORDS   64
#define SMK_PHYS_CLASSES 3          /* 50cc / 100cc / 150cc */
#define SMK_PHYS_ACCEL   0          /* first index of the acceleration table */
#define SMK_PHYS_TARGET  16         /* first index of the target-speed table */

typedef struct { uint16_t w[SMK_PHYS_WORDS]; int engine_class; } smk_physics;

bool smk_physics_load(const smk_rom *rom, int engine_class, smk_physics *out);
/* $80A7E1: acceleration for the current speed. */
int16_t smk_physics_accel(const smk_physics *p, int16_t speed);

/* Velocity from angle and speed, the way $80F8CF does it. */
void smk_kart_face(smk_kart *k);
/* One frame of motion: the integration at $80879D, with wall blocking. */
void smk_kart_move(smk_kart *k, const smk_track *t);
static inline int smk_kart_px(int32_t v) { return (int)(v >> SMK_POS_SHIFT); }

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
