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
#define SMK_TILE_TOTAL    256         /* 192 theme tiles + 64 object tiles */
#define SMK_WORLD_PX      (SMK_MAP_DIM * SMK_TILE_PX)   /* 1024 */

/* Surface behaviour, one byte per tile index, from the ROM's table.
 * The physics at $80FA8C reads exactly this array (RAM $0B00) and the
 * collision test at $80F8A7 is `and #$0020`. */
#define SMK_SURF_SOLID   0x20u    /* bit 5: blocks the kart                 */
#define SMK_SURF_SPECIAL 0x80u    /* bit 7: handled separately ($80FA8F)    */

typedef struct {
    uint8_t  map[SMK_MAP_BYTES];                    /* tile index per cell   */
    uint8_t  surface[SMK_TILE_TOTAL];               /* behaviour per tile    */
    uint8_t  tiles[SMK_TILE_TOTAL * SMK_TILE_BYTES];/* expanded 8bpp pixels  */
    uint32_t palette[256];                          /* 0xRRGGBB              */
    int      track;
    int      theme;
} smk_track;

/* Load a course.  `theme` < 0 means "use the ROM's own binding". */
bool smk_track_load(const smk_rom *rom, int track, int theme,
                    smk_track *out, char *err, size_t errsz);

/* Stamp the track's objects into the tilemap, as $84F1A4 does at race
 * setup.  A separate step because the loader cross-check (tools/test.py)
 * compares smk_track_load against the game's LOADER, which has not
 * stamped yet either. */
void smk_track_place_objects(const smk_rom *rom, smk_track *t);

/* The starting grid, read off the game itself (docs/NOTES.md 029): eight
 * karts in two staggered columns, x alternating 952/920, y stepping down by
 * 24 from 756, all facing angle 0 (-Y).  Confirmed against the demo race.
 * Falls back to a road-finding heuristic on the few courses where that grid
 * is not on drivable ground. */
#define SMK_GRID_X_ODD   920
#define SMK_GRID_X_EVEN  952
#define SMK_GRID_Y0      756
#define SMK_GRID_DY      24
void smk_track_start(const smk_track *t, int kart, float *x, float *y, float *angle);

/* The fallback: longest run of a non-solid tile. */
void smk_track_guess_start(const smk_track *t, float *x, float *y, float *angle);

/* Colour of a world pixel, wrapping at the 1024x1024 edge. */
uint32_t smk_track_texel(const smk_track *t, int wx, int wy);
uint8_t  smk_track_texel_index(const smk_track *t, int wx, int wy);
void     smk_render_set_plane_mask(uint8_t *mask, int pitch);
/* Sprite priority under the Mode 7 plane: while set, sprite pixels are
 * dropped where the mask marks the plane opaque (NOTES 128). */
void     smk_draw_set_clip_mask(const uint8_t *mask, int pitch);
/* $81F638: the angle of a velocity vector, the game's own octant atan2 */
uint16_t smk_angle_of(int16_t vx, int16_t vy);

/* Surface byte under a world position, exactly as $80FA62 computes it:
 * index = (y >> 3) * 128 + (x >> 3), then map -> surface table. */
uint8_t smk_track_surface(const smk_track *t, int wx, int wy);
/* Solid: only bit 7.  MEASURED (NOTES 119) by swapping the live class
 * table under a lapping kart: on class $20 and on $26 the kart FALLS and
 * Lakitu fetches it back ($A0 walks $0A -> $0C -> $0E), it does not stop.
 * So the hazard classes $20-$3E are not walls; treating bit 5 as solid
 * put an invisible wall around Ghost Valley and Rainbow Road, where the
 * game drops you.  The bit-7 classes are the real barriers (NOTES 044/088:
 * Mario Circuit's blocks are $80, measured head-on). */
static inline bool smk_surface_solid(uint8_t s)
{ return (s & 0x80u) != 0; }

/* The surface byte's low nibble is a TYPE index (even values, so type =
 * (s >> 1) & 7 for the drag rows).  DECODED:
 *   $80A590: coasting drag per type       (accel when off-throttle)
 *   $80A65D: over-cap deceleration per type, two rows of 8
 *   $80A701: the cap test - `speed > cap[type]` selects the decel row
 * The cap values themselves are computed per kart into scratch; ours are
 * MEASURED plateaus from a driving AI kart (NOTES 053) and labelled so. */
/* 16 types, not 8: the class low nibble is (s>>1)&$0F, and $80A65D is one
 * 16-entry per-type table (its "second row" is types 8-15 - the ice road
 * classes $56/$58 are types 11/12 with gentle -12/-28 decel, which IS the
 * ice feel).  The old &7 fold aliased 8-15 onto 0-7 (NOTES 060). */
static inline int smk_surface_type(uint8_t s) { return (s >> 1) & 0x0F; }
int16_t smk_surface_drag(int type);          /* $80A590 row */
int16_t smk_surface_overcap_decel(int type); /* $80A65D row 0 */
int16_t smk_surface_cap(uint8_t surf);       /* 0 = uncapped */
int16_t smk_surface_cap_frac(uint8_t surf);  /* thousandths of road speed,
                                                MEASURED (NOTES 066) */
int16_t smk_surface_decel(uint8_t surf);     /* units/frame toward the cap,
                                                MEASURED (NOTES 067) */

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
    /* Height.  DECODED at $80B1D6, the ROM's only `sbc #$001A` (NOTES 045):
     * z is a 24-bit fixed-point value in the block at $1E..$20, and the
     * per-frame update adds the velocity to the WORD at $1F - i.e.
     * z += zvel << 8 - after subtracting gravity.  Pixel height is z >> 16. */
    int32_t  z;             /* $1E..$20 - 24-bit, px in the top byte     */
    int16_t  zvel;          /* $26 - added as << 8 each frame            */
    bool     airborne;      /* $E2 bit 15                                */
    /* horizontal knockback while bouncing off a wall (NOTES 044/045)    */
    int16_t  bvx, bvy;
    int8_t   bounce_cool;   /* $5C: the 8-frame window with the velocity held */
    uint8_t  bounce_dir;    /* $56: which way the wall pushed (0/2/4/6)       */
    uint8_t  bounce_pend;   /* $52's $C000 bits: damp on the NEXT frame       */
    uint8_t  bounce_hit;    /* $10 bit 12: hit a wall, $80A0C7 owes a cost   */
    int16_t  crash_lag;     /* $A8 as $80A106 sets it: the bounce's slip     */
    int8_t   crash_frames;  /* how long $AC = $16 decelerates (see NOTES 132)*/
    uint8_t  bounce_obj;    /* the window came from an OBJECT, not a wall    */
    uint8_t  stuck;         /* $5A: consecutive frames with nowhere to go   */
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
#define SMK_PHYS_TURN    32         /* turn-rate by heading error ($80AFF9)  */

typedef struct { uint16_t w[SMK_PHYS_WORDS]; int engine_class; } smk_physics;

bool smk_physics_load(const smk_rom *rom, int engine_class, smk_physics *out);
/* $80A7E1: acceleration for the current speed. */
int16_t smk_physics_accel(const smk_physics *p, int16_t speed);

/* $80AFF9: per-frame turn amount for a heading error.  `row` is the
 * per-kart $C8 offset in words (0 for the base handling row). */
uint16_t smk_physics_turn(const smk_physics *p, uint16_t err, int row);

/* Velocity from angle and speed, the way $80F8CF does it. */
void smk_kart_face(smk_kart *k);
/* One frame of motion: the integration at $80879D, with wall blocking. */
void smk_kart_move(smk_kart *k, const smk_track *t);
/* the same without the class-$10 launcher (the player's classes are handled
 * in player.c from the ROM's own tables) */
void smk_kart_move_ex(smk_kart *k, const smk_track *t, bool auto_ramp);

/* Height, decoded at $80B1D6 (NOTES 045).  Gravity is 26 units per frame;
 * a second mode at $80DFED uses 18.  Landing clears height, velocity and
 * the airborne flag. */
#define SMK_GRAVITY      26         /* $001A - the captured bounce arc  */
#define SMK_GRAVITY_ALT  18         /* $0012 - the $80DFED mode */
/* Two DIFFERENT vertical events, and conflating them is what made the
 * hop invisible (playtest "no jump"):
 *
 *   BOUNCE ($80F8C0) - launch $0080, captured z-for-z from the running
 *   game in NOTES 045 and pinned in the selftest: lands on frame 8.
 *
 *   HOP ($80B6A5) - measured on SCREEN (NOTES 088): the player's sprite
 *   rises 12 px and is back down after ~19 frames.  With the game's own
 *   gravity 26 that means a launch of 9.5*26 = 247, not $0080, which
 *   peaked under one pixel.
 *
 * Both use the same gravity; only the launch differs. */
#define SMK_BOUNCE_VEL   0x0080     /* $80F8C0, the captured arc         */
#define SMK_HOP_VEL      247        /* $80B6A5: 19 frames, 12 px on screen */
#define SMK_RAMP_VEL     259        /* class $10 ramps (z peaked 247/224) */

void smk_kart_gravity(smk_kart *k);

/* ---- The player kart's control, DECODED (NOTES 103) --------------------
 *
 * Every table here is read from the ROM at setup: the character's top
 * speed, acceleration curve, surface caps and steering rows ($81:8000..),
 * and the global drift rows ($80AC36), row selectors ($80A4A0/$80A4C0),
 * turn damping ($80A7FF) and the four deceleration tables ($80A65D..).
 * The per-frame step is a transcription of $80A4D0 / $80B1BE / $80A892 /
 * $80A3B7, verified frame-exact against the attract race (tools/labs). */
typedef struct {
    int character, engine_class;
    /* the per-player block the ROM builds at $0710 (P1) / $0768 (P2) */
    uint16_t accel[16];        /* by speed/64; A<<8 into accel32 ($80A7E1) */
    int16_t  cap[16];          /* by surface type; -1 = no cap ($80A6F7)   */
    uint16_t steer[3][4];      /* max, reversal, ramp, decay ($80A80F);
                                  rows at block +$40/+$48/+$50: 0 = plain,
                                  2 = shoulder/brake held ($DE = $50)       */
    int16_t  base_top;         /* $B4: character top, class adjusted        */
    uint16_t drift[8][8];      /* $80AC36 rows: window, spin rate, vlag max,
                                  vlag rate, vlag decay, vlag entry, pose
                                  rate, pose max */
    int16_t  row_base[16], row_char[8];
    uint16_t lowturn[9];
    int16_t  damp[8], overcap[8], brake[8], coast[8], overtgt[8], hopcap[16];
    /* state, named by the ROM field it mirrors */
    uint16_t heading;          /* $A4 - the stick turns this, camera follows */
    uint16_t vel_angle;        /* $A2 = heading + vlag: direction of travel */
    uint16_t pose;             /* $2A = heading - plag: what the sprite shows */
    int16_t  turn;             /* $B2 - turn rate, added >> 3 per frame     */
    int16_t  vlag;             /* $A8 - the slide's velocity lag            */
    int16_t  plag;             /* $AA - the slide's pose offset             */
    int16_t  spin;             /* $FA - spin accumulator; +-$7A00 spins out */
    int      state;            /* $A6 - slide machine state                 */
    int      drive;            /* $AC - drive state (0 = normal)            */
    int      jump_state;       /* $A0                                       */
    /* the start rev and its flags (NOTES 143).  $C2 is not only the
     * launch: $80A10F halves it on a crash, with a floor of $0100. */
    int16_t  rev;              /* $C2                                       */
    uint16_t rev_ceiling;      /* the $81:EFF3 row, read at setup           */
    int16_t  rev_up_lo, rev_up_hi, rev_off;
    uint8_t  rev_window;       /* $E0 bit 0: inside the turbo band          */
    uint8_t  rev_spin;         /* $E2 bit 0: over-revved, wheels spinning   */
    uint16_t pad, pad_prev;    /* $C4 - the composed pad word, and last frame's */
    uint16_t flags;            /* $E2 - bit 15 airborne, 2/5 drift pose,
                                  3 spinning, 6 reward armed                */
    int      row, steer_row, type;   /* $28 >> 4, $DE row, surface type    */
    int16_t  target;           /* $D6 = base_top + 8 * min(coins, 10)       */
    int      coins;
    bool     item_held;        /* $0D70,y < 0: an item (or its roulette) - boxes are
                                  not consumed while it is (LABELLED: no item system) */
    int      fc, ca;           /* $FC countdown, $CA hold counter           */
    /* hazards (NOTES 113): water = the $22 wade, fall = the $24/$26 drop
     * and Lakitu's rescue.  The caller supplies the rescue target - the
     * ROM takes it from the kart's waypoint ($80B373). */
    int      hazard;           /* 0 none, 8 in water, 6 fallen             */
    int      resc_t;           /* frames spent in the rescue               */
    int      resc_x, resc_y;
    uint16_t resc_h;
    int32_t  accel32;          /* $EE:$EC                                   */
} smk_player;

bool smk_player_setup(const smk_rom *rom, int character, int engine_class,
                      smk_player *p);
/* use a mushroom ($80B47C): false if the kart is spinning */
bool smk_player_boost(smk_player *p);
/* The countdown's rev ($C2) and the launch test (NOTES 143): call
 * smk_player_rev once a frame while the lights run, then
 * smk_player_launch when they go out. */
void smk_player_rev(smk_player *p, bool throttle);
void smk_player_launch(smk_player *p);
/* place the kart: all three angles, machine at rest */
void smk_player_reset(smk_player *p, uint16_t heading);
/* one frame.  held / pressed are SNES pad words: B $8000 Y $4000 Left $0200
 * Right $0100 L $0020 R $0010. */
void smk_player_step(smk_player *p, smk_kart *k, const smk_track *t,
                     uint16_t held, uint16_t pressed);
/* DSP-1 command $04: radius * (sin, cos) of a 16-bit angle, the chip's own
 * table arithmetic (see player.c) - use it wherever the game does */
void smk_dsp_sincos(uint16_t angle, int16_t radius, int16_t *sx, int16_t *cy);
/* the camera azimuth the ROM feeds DSP-1 ($808632): heading + $C0 */
#define SMK_CAM_LEAD 0x00C0

/* ---- The game's own per-frame log (tools/labs/mame/demolog.lua) ---------
 * One frame of one kart as the running game had it.  demoreplay.c scores
 * the port against it; the game's --replay plays it with a ghost. */
typedef struct {
    uint16_t c4;               /* the composed pad word $C4              */
    int32_t  x, y;             /* 16.16 position                         */
    uint16_t a4, a2, pose;     /* heading, velocity angle, pose          */
    int16_t  speed; uint16_t frac;
    int16_t  vx, vy, vlag, plag, spin, turn, accel;
    int      state, drive;     /* $A6, $AC                               */
    uint16_t flags, flags10;   /* $E2, $10                               */
    int      z, zvel, coins;
    uint8_t  surf;             /* $AE: the class byte under the kart      */
} smk_demo_frame;
typedef struct {
    smk_demo_frame *f;
    int n, start;              /* start = frame before the kart moves    */
    int track, character, engine_class, kart;
    int mode;                  /* $2C: 0 GP, 4 Time Trial ...            */
} smk_demolog;
bool smk_demolog_load(const char *path, int kart, smk_demolog *out);
void smk_demolog_free(smk_demolog *d);
/* place the port's kart and player exactly where the game was at frame i */
void smk_demolog_sync(const smk_demolog *d, int i, smk_player *p, smk_kart *k);
/* the pad words the port's step wants, from the logged $C4 */
void smk_demolog_pad(const smk_demo_frame *r, uint16_t *held, uint16_t *pressed);

void smk_kart_launch(smk_kart *k, int16_t zvel);
void smk_kart_bounce_damp_for_test(smk_kart *k);   /* NOTES 125 gate */

static inline int smk_kart_px(int32_t v) { return (int)(v >> SMK_POS_SHIFT); }
/* z -> SCREEN pixels, fixed by the measured hop: a 247 launch under
 * gravity 26 peaks at z = 300288 and reads as 12 px on the SNES screen,
 * so one screen pixel is 25029 z-units.  (The old z>>16 made the whole
 * hop less than one pixel tall.) */
static inline int smk_kart_height_px(const smk_kart *k)
{ return (int)(k->z / 25029); }

/* ---- Kart sprites ------------------------------------------------------
 *
 * Uncompressed 4bpp in ROM, laid out exactly as the PPU wants them: a 32x32
 * sprite is 4x4 tiles with a **16-tile row stride**, and frames sit side by
 * side, so frame f begins at tile (f%4)*4 + (f/4)*64.  Each frame is 512
 * bytes, which is why the game streams them to VRAM in 128-byte quarters.
 *
 * Colours come from the same 256-entry palette as the track (CGRAM is
 * uploaded in one 512-byte transfer), with the sprite palettes at $90, $A0,
 * $B0 ... - $90 is Mario, $A0 Luigi, $B0 Peach.
 */
#define SMK_SPR_PX      32
/* The sheet region is $2000-$8000 per bank: 48 frames.  0-31 are the three
 * size tiers of rotation steps (NOTES 030); 32-47 are spin/tumble poses,
 * far-tier variants and specials (NOTES 040). */
#define SMK_SPR_FRAMES  48
#define SMK_SPR_BYTES   512
/* Three size tiers of eleven rotation steps (frames 0..10, 11..21, 22..31;
 * NOTES 030), with 32..47 holding spin/tumble poses and specials.
 *
 * The rotation rule is MEASURED, not guessed (NOTES 041): spinning a kart in
 * place in the running game and logging which frame it uploads gives
 * boundaries at 22.5 deg + 11.25 deg steps for frames 1..7, then 22.5 deg
 * steps to frame 10, which spans the frontal arc through 180 deg.  The far
 * side of the circle is the same frames hflipped.  Frame 1 is the
 * straight-from-behind view.  The game also applies ~3.6 deg of hysteresis
 * at each boundary; we omit that for now.
 *
 * Still assumed: tiers 1 and 2 use the same angular boundaries (measured
 * only on the near tier), and which side maps to hflip. */
#define SMK_SPR_TIER0     0
#define SMK_SPR_TIER1    11
#define SMK_SPR_TIER2    22
#define SMK_SPR_TIER_LEN 11
#define SMK_SPR_REAR      1     /* measured: the straight-from-behind pose */

/* The measured rule: frame index and hflip for a heading relative to the
 * camera (angle units, 65536 = full turn; 0 = seen squarely from behind). */
int smk_sprite_for_heading(int tier, uint16_t rel, bool *hflip);

typedef struct {
    uint8_t px[SMK_SPR_FRAMES][SMK_SPR_PX * SMK_SPR_PX];  /* palette indices */
    int frames;
} smk_sprites;

/* `base` is a SNES address; 0 selects the default kart sheet. */
bool smk_sprites_load(const smk_rom *rom, uint32_t base, smk_sprites *out);

/* The eight drivers.  Seven sprite sheets at $C0-$C6:$2000 cover them,
 * because Mario and Luigi share one and differ only by palette - which is
 * also how the game does it.  Sprite palettes are CGRAM $80 + n*16. */
#define SMK_CHARACTERS 8
typedef struct { const char *name; uint32_t sheet; int pal; } smk_driver;
extern const smk_driver SMK_DRIVERS[SMK_CHARACTERS];

/* ---- Course data: sectors, racing line, finish -------------------------
 *
 * Decoded from the loader at $81FBC0-$81FEB5 and verified byte-exact
 * against the running game (docs/NOTES.md 042).  A 64x64 map of 16px cells
 * holds a SECTOR index per cell (bit 7 = finish-line strip; $7F in the low
 * bits means off-course).  One waypoint of the racing line per sector.
 * Unpainted cells are scratch left by the tile expander and are dont-care.
 */
#define SMK_SECT_W       64
#define SMK_SECT_CELLS   (SMK_SECT_W * SMK_SECT_W)
#define SMK_SECT_CELL_PX 16
#define SMK_SECT_FINISH  0x80u
#define SMK_SECT_OFF     0x7Fu
#define SMK_MAX_SECTORS  128

typedef struct {
    uint8_t  map[SMK_SECT_CELLS];      /* sector | flags per cell          */
    uint16_t wx[SMK_MAX_SECTORS + 1];  /* racing line, closed              */
    uint16_t wy[SMK_MAX_SECTORS + 1];
    uint8_t  wattr[SMK_MAX_SECTORS];   /* per-sector attribute byte        */
    int      sectors;
    /* the lap SEGMENT the obstacles are respawned on (NOTES 127):
     * $0D28 = ROM[$81:8B73 + track] picks a threshold table through
     * $84DB83, $0D2C = ROM[$81:8B8C + track] picks the list inside it,
     * and the player's waypoint against those bytes gives the segment. */
    uint8_t  seg_thresh[8];
    int      nseg;
    int      seg;              /* the live segment, from smk_course_spawn */
    int      nlive;            /* $819136: 2 slots one-player, 4 two      */
    int      live[4];          /* indices into ent[]                      */
    uint16_t lap_word;                 /* $80D4 param, meaning undecoded   */
    /* finish-line rectangle, kept for grid placement */
    int      fin_cell, fin_w, fin_h;
    /* the AI direction field at $7F:4000 (NOTES 056): per on-course cell,
     * the high byte of the angle from the cell centre to the cell's own
     * sector's waypoint.  Verified 95% byte-exact vs the game, 100% within
     * one step (the ROM's table atan2 rounds differently at boundaries). */
    uint8_t  flow[SMK_SECT_CELLS];
    /* track objects ($85:D000 + track*128, NOTES 064): item boxes, pipes,
     * coins - [kind][cell:word] records.  kind bits 0-5 = stamp graphic,
     * bits 6-7 = size class. */
    struct { uint8_t kind; uint16_t x, y; } obj[42];
    int      nobj;
    /* Sprite obstacles (pipes, Thwomps, moles...), decoded from the
     * spawner at $84DC20: per-track word list at $85:C800 + track*64,
     * [kind:2][y:7][x:7], coordinates cell*8+4, zero-terminated. */
    struct { uint8_t kind; uint16_t x, y; } ent[32];
    int      nent;
} smk_course;

bool smk_course_load(const smk_rom *rom, int track, smk_course *out);

/* Starting-grid placement derived from decoded course data: two columns
 * behind the finish strip, facing along the racing line.  Replaces the
 * old fixed-coordinate grid, which held track 7's values and dropped
 * karts "in the middle of nowhere" elsewhere. */
void smk_course_start(const smk_course *c, int slot,
                      float *x, float *y, uint16_t *heading);
static inline uint8_t smk_course_cell(const smk_course *c, int wx, int wy)
{
    return c->map[((wy >> 4) & 63) * SMK_SECT_W + ((wx >> 4) & 63)];
}

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

/* Project a world point onto the screen through the Mode 7 camera.
 * Returns false when the point is behind the camera or above the horizon.
 * `scale` comes back as pixels-per-world-unit at that depth. */
bool smk_project(const smk_camera *cam, float wx, float wy,
                 int w, int h, float *sx, float *sy, float *scale);

/* Blit one sprite frame, nearest-neighbour, index 0 transparent.
 * `hflip` mirrors horizontally - the game draws the far half of the
 * rotation circle this way. */
/* Continuous-scale kart draw; `mirror_half` reflects the left half for
 * the straight pose.  See NOTES 100. */
void smk_draw_sprite_scaled(const smk_sprites *s, int frame,
                            const uint32_t *palette, int pal_base,
                            int cx, int cy, float scale, bool hflip,
                            bool mirror_half,
                            uint32_t *pixels, int w, int h, int pitch_px);

void smk_draw_sprite(const smk_sprites *s, int frame, const uint32_t *palette,
                     int pal_base, int cx, int cy, int scale, bool hflip,
                     uint32_t *pixels, int w, int h, int pitch_px);

/* The game's own HUD sprite set (docs/NOTES.md 085): $81:E856
 * decompresses $C1:0000 to $7F:C000 and DMAs offset $200 (4096 bytes =
 * 128 tiles) into the sprite tiles $40-$BF.  Digits 0-4 are tiles
 * $A7-$AB and 5-9 are $B7-$BB - the strip is 5 wide and wraps by the
 * 16-tile VRAM row.  Sprite palette $C0 (from the live OAM attribute). */
#define SMK_HUD_TILES   128
#define SMK_HUD_TILE0   0x40
#define SMK_HUD_PAL     0xC0
typedef struct { uint8_t px[SMK_HUD_TILES][64]; bool ok; } smk_hud;
bool smk_hud_load(const smk_rom *rom, smk_hud *out);
/* tile index for one decimal digit */
static inline int smk_hud_digit(int d)
{ return (d < 5 ? 0xA7 + d : 0xB7 + (d - 5)) - SMK_HUD_TILE0; }

/* The object/entity sprite set (docs/NOTES.md 093).
 *
 * Found by searching the graphics banks for the bytes of a live VRAM
 * entity tile: the stream at **$C1:0F9B** decompresses to 1824 bytes =
 * 57 tiles of 4bpp, and stream tile n is VRAM sprite tile $C0 + n.  The
 * pipe is VRAM $CE-$D7 = stream tiles 14-23, arranged 2 wide x 5 tall,
 * palette base $F0 (its pixels use indices $A-$E).
 *
 * NOTES 086's $C7:0000 was wrong - refuted by a byte comparison against
 * the running game (NOTES 092). */
/* The set is PER THEME, from the pointer table at $81:EBD3 (3 bytes an
 * entry, beside the tilemap/tileset/palette tables).  Every theme uses
 * the same tile layout, so only the artwork changes: Mario Circuit gets
 * pipes, Rainbow Road gets Thwomps, and so on.  Hardcoding the Mario
 * Circuit stream drew green pipes on Rainbow Road (playtest). */
#define SMK_OBJ_TABLE   0x81EBD3u
#define SMK_OBJ_TILES   57
#define SMK_OBJ_PAL     0xF0
/* The pipe is 2x2 tiles with the SNES's 16-tile VRAM ROW STRIDE - so
 * stream tiles 14,15 over 30,31, not four consecutive ones.  Stacking
 * ten consecutive tiles as 2 wide x 5 tall is what produced the
 * scrambled, offset column in playtest: consecutive tiles after 15 are
 * OTHER objects (each successively narrower), not more of this pipe. */
/* The sheet holds the same pipe at many SIZES - the SNES cannot scale a
 * sprite, so it stores a tier per distance band, which is why a live
 * entity's tile list changes as you approach.  Base 32 is the tier that
 * matches the original screenshots (a 12x16 cylinder with a dark rim);
 * base 14, which one captured entity happened to be using, is a small
 * far tier and rendered as a squat can.
 *
 * We draw ONE tier scaled continuously - a labelled divergence, the same
 * one we make for karts.  Choosing the tier by distance instead is the
 * faithful behaviour and is still open. */
/* The size TIERS.  The SNES cannot scale a sprite, so the sheet stores
 * the object at several sizes and the game picks one by distance - which
 * is why a live entity's tile list changes as you approach.  Measured
 * off the sheet (identical layout in every theme):
 *
 *     base 32 -> 12x15    base 34 -> 11x13    base 36 -> 10x11
 *
 * The tier art is the BASE drawing, not a size cap.  NOTES 105: the
 * game keeps a per-entity scale at block+$06 and it is 0x4200/d in 8.8,
 * so an object stands at its natural art size SMK_OBJ_SCALE_K world px
 * from the kart and at DOUBLE that from half as far.  Measured on the
 * reference screenshot, a pipe beside the kart draws 22x32 SNES px
 * against the kart's own 30x31 - it is magnified, and the outline that
 * is one art pixel on the kart is two on the pipe. */
#define SMK_OBJ_PIPE0   32        /* the near tier's top-left tile     */
#define SMK_OBJ_TIERS   3
#define SMK_OBJ_RADIUS  6
#define SMK_OBJ_STRIDE  16        /* VRAM tiles per row                */
#define SMK_OBJ_PIPE_W  16        /* pixels                            */
#define SMK_OBJ_PIPE_H  16

/* The object's projected scale, MEASURED and then found in the ROM
 * (NOTES 129).  $80C879 stores the DSP-1 projection's third output in the
 * block's +$06, and it is
 *
 *     +$06 = $4200 / (depth ALONG THE VIEW AXIS ahead of the kart)
 *
 * - fitted over 975 samples of a driven lap at 2.1% mean error, against
 * 19% for the Euclidean distance the port had been using.  That error is
 * the whole bug: a pipe BESIDE you has a small axis depth and must draw
 * large, but its Euclidean distance stays big, so it drew small.
 *
 * $80C883: at $0300 and above the game parks the sprite off-screen
 * ($30,x = $0140) - an object nearer than $4200/$300 = 22 px along the
 * axis is simply not drawn, which is what stops it filling the screen.
 *
 * $84DA18 then picks the DRAWING by which band the scale falls in,
 * walking $84DA3C = C0 60 30 00 - so three drawings and, past the last
 * threshold, nothing. */
#define SMK_OBJ_SCALE_K 16896.0f     /* $4200 */
#define SMK_OBJ_SCALE_HIDE 0x0300    /* $80C883: nearer than this, hidden */
#define SMK_OBJ_BAND0   0x00C0       /* $84DA3C, the size ladder          */
#define SMK_OBJ_BAND1   0x0060
#define SMK_OBJ_BAND2   0x0030

/* The KART ladder is NOT this one.  The kart blocks carry the same +$06,
 * written by the same $80C881, but which drawing each scale selects is a
 * different table that has not been measured - so the kart path keeps the
 * constant it was tuned to and stays in the ledger (S10). */
#define SMK_KART_SCALE_K 66.0f
#define SMK_OBJ_MAG_MAX  2.0f        /* kart path only, still LABELLED */

/* The drawn size is TWICE the sheet's drawing (NOTES 139).  Measured off
 * a reference frame of the real game, using the kart's known 32 px as the
 * ruler: the pipe there is 23 x 31 SNES px and the sheet's complete pipe
 * is 12 x 16, so 1.9x in both directions.  Where the larger art comes
 * from is NOT known - the whole object sheet tops out at 16 x 16 - so
 * this magnifies what we have.  LABELLED: the size is measured, the
 * mechanism is not. */
#define SMK_OBJ_MAG      2

#define SMK_OBJ_NEAR    ((float)SMK_OBJ_RADIUS)
typedef struct { uint8_t px[SMK_OBJ_TILES][64]; bool ok; } smk_objgfx;
bool smk_objgfx_load(const smk_rom *rom, int theme, smk_objgfx *out);

/* ---- Opponent karts ---------------------------------------------------
 *
 * The DATA is the ROM's (sector map, waypoints, acceleration tables) and
 * the steering law matches the decoded shape; the slew rate, the
 * target-speed entry per kart and rubber-banding are PLACEHOLDERS,
 * labelled in src/ai.c. */
typedef struct {
    smk_kart k;
    int      character;     /* who drives this slot (SMK_DRIVERS index)  */
    int      sector;        /* last on-course sector                    */
    int      lap;
    int      progress_max;  /* $F8,x: max of (lap<<8)|sector, monotonic */
    int      slow_frames;   /* stuck-at-a-wall recovery counter         */
    int      escape;        /* frames left of hold-heading wall escape  */
    int      was_fast;      /* escape only after the kart has driven    */
    int      last_px, last_py, still;   /* position-stagnation detector  */
    int      esc_len;       /* escalating escape duration               */
    int      no_prog;       /* frames since monotonic progress          */
    int      rescue_max;    /* rescue timer's own progress watermark    */
    int      lap_cool;      /* one lap event per strip transit          */
} smk_racer;

/* Sprite-obstacle collision, shared by the player and the AI. */
void smk_collide_objects(smk_kart *k, const smk_course *crs);
/* $84DBD5: which lap segment a waypoint is in, and which obstacles that
 * spawns.  Call once a frame with the player's waypoint. */
int  smk_course_segment(const smk_course *c, int waypoint);
void smk_course_spawn(smk_course *c, int waypoint, bool two_player);
extern smk_course *course_for_step;

int  smk_race_rank(const smk_racer *racers, int who, const smk_course *crs);
void smk_racer_start(smk_racer *r, const smk_course *crs, int slot);
/* The starting grid's characters ($81EE33, NOTES 111): slot 0 is P1, slot
 * 1 the rival (2P: P2), slots 2..7 the rest in the ROM's order for the
 * row character, skipping the humans.  out[8] = character per slot. */
void smk_grid_order(const smk_rom *rom, int p1, int p2, bool two_players, int out[8]);
void smk_racer_step(smk_racer *r, const smk_track *trk,
                    const smk_course *crs, const smk_physics *phys);

/* The projection constants, from the ROM's own DSP-1 geometry
 * (docs/NOTES.md 084).  depth(line) = K/(line - H) world px from the eye;
 * LES is the DSP's own screen distance and makes depth/scale exact. */
#define SMK_PROJ_K    4972.0f
#define SMK_PROJ_H      20.36f
#define SMK_PROJ_LES   256.0f
#define SMK_SKY_LINES   24.0f
#define SMK_CAM_TRAIL   61.0f     /* eye sits this far behind the kart */
#define SMK_PLAYER_LINE 102.0f    /* measured row of the player's kart */

/* The mirrored straight pose: frame 0's left half reflected (the game
 * stores only the half - measured, NOTES 080). */
void smk_draw_sprite_mirror(const smk_sprites *s, int frame,
                            const uint32_t *palette, int pal_base,
                            int cx, int cy, int scale,
                            uint32_t *pixels, int w, int h, int pitch_px);
void smk_draw_sprite_mirror2(const smk_sprites *s, int frame,
                             const uint32_t *palette, int pal_base,
                             int cx, int cy, int scale, bool mini,
                             uint32_t *pixels, int w, int h, int pitch_px);

/* Half-size kart sprite for the far range (see sprite.c - the game
 * composes this at runtime; ours is a labelled 2:1 approximation). */
void smk_draw_sprite_mini(const smk_sprites *s, int frame,
                          const uint32_t *palette, int pal_base,
                          int cx, int cy, int scale, bool hflip,
                          uint32_t *pixels, int w, int h, int pitch_px);


/* ---- Coins and item boxes (src/pickup.c, NOTES 110) ----------------------
 * The collector at $81B73B: the tilemap cell under a grounded player decides.
 * Returns true when something was picked up (the map is rewritten). */
bool smk_pickup_step(const smk_rom *rom, smk_track *t, smk_player *p, const smk_kart *k,
                     bool grounded_before);

/* ---- Breakable blocks (src/blocks.c, NOTES 123) ------------------------
 * Ghost Valley's rails and Vanilla Lake's ice: class $82 and up crumble
 * over four steps into the void tile, so the hole they leave can be
 * fallen through.  Bind the track being played, report hits, step once a
 * frame. */
void smk_blocks_bind(smk_track *t);
bool smk_blocks_breakable(uint8_t cls);
bool smk_blocks_hit(int cell, bool by_player);
void smk_blocks_step(void);

/* ---- The horizon layer (src/horizon.c, NOTES 117) ----------------------
 * gfx_d[theme] are the tiles, gfx_e[theme] the 32x24 map that arranges
 * them; both matched byte-exact against the running game's VRAM. */
#define SMK_HZ_W      32
#define SMK_HZ_H      24
#define SMK_HZ_TILES  128
#define SMK_HZ_PAL    64          /* mode 0: BG3's CGRAM block */
typedef struct {
    bool ok;
    int  tiles;
    int  last_row;             /* lowest row of the map with scenery */
    uint8_t  px[SMK_HZ_TILES][64];
    uint16_t map[SMK_HZ_W * SMK_HZ_H];
} smk_horizon;
bool smk_horizon_load(const smk_rom *rom, int theme, smk_horizon *hz);
/* draw the band at the top of the frame; `scale` is host px per SNES px */
void smk_horizon_draw(const smk_horizon *hz, const uint32_t *palette,
                      uint16_t heading, int band_h, uint32_t *fb,
                      int w, int h, int scale);
/* give the renderer the layer to draw above the horizon (NULL = flat sky) */
void smk_render_set_horizon(const smk_horizon *hz, uint16_t heading);

/* ---- Ground effects: tyre smoke and dust (src/effects.c, NOTES 109) ---- */
typedef struct { int n; int8_t x[8], y[8]; uint8_t tile[8], attr[8]; } smk_effect_template;
typedef struct { int n; uint8_t dur[8], tpl[8]; smk_effect_template t[8]; } smk_effect_script;
typedef struct { bool valid; uint8_t attr_xor; smk_effect_script script[12]; } smk_effect_kind;
typedef struct {
    bool ok;
    uint8_t tiles[32][64];         /* VRAM $100..$11F, palette indices */
    int16_t wobble[8];             /* $80D46F, by frame counter & 7     */
    smk_effect_kind kind[8];       /* by record offset / 6              */
} smk_effects;
typedef struct { int kind, frame_idx, pos, dur; } smk_effect_state;
bool smk_effects_load(const smk_rom *rom, smk_effects *fx);
/* $80D4A3 + class handlers: the kind to show, or -1 */
int  smk_effects_pick(uint8_t surf, bool grounded, bool spinning, bool deep_drift, int speed);
void smk_effects_step(smk_effect_state *st, int kind, int frame_idx);
/* base = the kart sprite's top-left + (0, 16), in framebuffer pixels */
void smk_effects_draw(const smk_effects *fx, const smk_effect_state *st, bool mirror,
                      unsigned frame_counter, int base_x, int base_y, int scale,
                      const uint32_t *palette, uint32_t *fb, int w, int h);

/* ---- Menu font, cups, records, and the game shell ----------------------
 *
 * The font and the cup order are the ROM's own (src/font.c, src/cups.c);
 * the lap records are ours (src/records.c).  See those files for where
 * each number comes from. */
#define SMK_FONT_TILES 256
typedef struct {
    uint8_t  px[SMK_FONT_TILES][64];   /* 2bpp values 0..3, from $C7:0000 */
    uint32_t pal[8][16];               /* the menu's own BG palettes      */
    bool ok, has_pal;
} smk_font;
bool smk_font_load(const smk_rom *rom, smk_font *f);
int  smk_font_glyph(int ch);           /* -1 for space and unknown        */
int  smk_font_text_w(const char *s, int scale);
void smk_font_draw(const smk_font *f, uint32_t *fb, int w, int h,
                   int x, int y, const char *s, int scale, const uint32_t col[4]);

#define SMK_CUPS         4
#define SMK_CUP_COURSES  5
extern const char *const SMK_CUP_NAMES[SMK_CUPS];
/* $81EC1B: the game's own cup*5 + course -> track index */
int smk_cup_track(const smk_rom *rom, int cup, int course);
/* "MARIO CIRCUIT 1" - family from $81EC2F, ordinal from the cup order */
const char *smk_track_name(const smk_rom *rom, int track);

#define SMK_RECORD_SLOTS 5
typedef struct { long frames; int character; } smk_record;
typedef struct { smk_record best[SMK_TRACK_COUNT][SMK_RECORD_SLOTS]; } smk_records;
const char *smk_records_path(void);
void smk_records_clear(smk_records *r);
void smk_records_load(smk_records *r);
bool smk_records_save(const smk_records *r);
/* insert if it makes the table; returns the slot or -1 */
int  smk_records_add(smk_records *r, int track, long frames, int character);
/* the game's own M'SS"HH from a frame count */
void smk_time_text(long frames, char *out, size_t n);

/* The race length, from the ROM: $014C = $8500 is the finish threshold on
 * the progress word (lap << 8 | sector), and the grid's first crossing is
 * $8000 ($8089C9 skips it), so a race is exactly five laps.  $8089D4's
 * `cmp #$FF00` against the same threshold is what lights FINAL LAP. */
#define SMK_RACE_LAPS 5

/* ---- The autopilot (src/autopilot.c) ------------------------------------
 *
 * A driver that only presses buttons: it hands smk_player_step the same pad
 * word a person would, so it is subject to every rule the player is.  It
 * follows the ROM's own route points as a reference and reacts to the
 * ground it is crossing.  The POLICY is ours and labelled; see the file. */
typedef struct {
    int sector;                /* last valid sector, the ROM's keep rule */
    int last_px, last_py, still, tick;
    int lost;                  /* frames the sector map has disagreed */
    int ahead;                 /* probe steps clear straight ahead      */
    int recover, recover_dir;
    int slide;
    /* readouts, for tuning the driver against a real course */
    int dbg_bend, dbg_need, dbg_limit, dbg_aim, dbg_dev;
    int dbg_flips, last_steer;   /* steering reversals: the weave, counted */
    long dbg_err_sum; int dbg_err_n;   /* mean |heading error|: the weave's SIZE */
} smk_autopilot;
typedef struct {
    bool accel, brake, left, right, hop, hop_held;
} smk_autopilot_out;
void smk_autopilot_init(smk_autopilot *a);
void smk_autopilot_step(smk_autopilot *a, const smk_track *trk,
                        const smk_course *crs, const smk_player *p,
                        const smk_kart *k, smk_autopilot_out *out);

/* The screens.  $002C is the game's own mode word - 0 GP, 2 match race,
 * 4 time trial, 6 battle (NOTES 113) - and time trial is the one this
 * shell drives. */
typedef enum {
    SMK_UI_TITLE, SMK_UI_MODE, SMK_UI_PLAYER, SMK_UI_COURSE,
    SMK_UI_RACE, SMK_UI_RESULT
} smk_ui_screen;
#define SMK_MODE_GP    0
#define SMK_MODE_TT    4
typedef struct { bool up, down, left, right, confirm, back; } smk_ui_input;
typedef struct {
    smk_ui_screen screen;
    int  mode_sel;        /* 0 Grand Prix (disabled), 1 Time Trial */
    int  player_sel;      /* SMK_DRIVERS index                     */
    int  cup_sel, course_sel;
    int  engine_class;
    int  track;           /* resolved on confirm                   */
    unsigned tick;        /* blink phase                           */
    bool denied;          /* flash: Grand Prix is not built yet    */
    int  denied_t;
} smk_ui;
void smk_ui_init(smk_ui *ui);
/* advance one frame; true when a race should start */
bool smk_ui_step(smk_ui *ui, const smk_rom *rom, const smk_ui_input *in);
void smk_ui_draw(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                 const smk_records *rec, const uint32_t *palette,
                 uint32_t *fb, int w, int h);
/* the results screen after a time trial */
typedef struct {
    long lap[SMK_RACE_LAPS];
    long total;
    int  laps_done;
    int  best_slot;       /* where the best lap landed in the table, or -1 */
    long best_lap;
} smk_ui_result;
void smk_ui_draw_result(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                        const smk_records *rec, const smk_ui_result *res,
                        uint32_t *fb, int w, int h);
/* One forward crossing of the finish strip in a time trial.
 *
 * The ROM's own bookkeeping ($8089B6 with $014C = $8500, NOTES 052): the
 * progress word starts at lap $7F because the grid sits BEHIND the line,
 * so the FIRST crossing only takes it to $80 - entering lap 1, not
 * completing one - and the five that follow reach the threshold.  Five
 * laps are therefore SIX crossings, which tools/laptest.c checks on every
 * GP course.
 *
 * `now` is the race clock in frames since the lights.  Returns true when
 * the race is over.  LABELLED: lap 1 is timed from the LIGHTS, so it
 * carries the run up to the line - the ROM's own clock start is not
 * decoded, and timing lap 1 from the first crossing instead would make it
 * a couple of seconds of rolling start rather than a lap. */
bool smk_tt_crossing(smk_ui_result *res, int *crossings, long *lap_start,
                     long now);

/* the in-race time panel: current lap clock, the splits so far, best lap */
void smk_ui_draw_splits(const smk_font *f, const smk_ui_result *res,
                        long cur_lap_frames, int lap, bool mushroom,
                        uint32_t *fb, int w, int h);

#endif /* SMK_H */
