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

/* The gap between grid rows: $81:904C loads $0C with #$0018 and adds it
 * to y once per kart.  The columns are grid_step apart and both come
 * from the course's own record - see smk_course_start. */
#define SMK_GRID_ROW     24

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
    uint8_t  star;          /* $E2 bit 1 mirrored: obstacles fly instead of you */
    uint8_t  hazard_hit;    /* touched a plant / a fish this frame: spin, not a wall */
    uint8_t  bounce_dir;    /* $56: which way the wall pushed (0/2/4/6)       */
    uint8_t  bounce_pend;   /* $52's $C000 bits: damp on the NEXT frame       */
    uint8_t  bounce_hit;    /* $10 bit 12: hit a wall, $80A0C7 owes a cost   */
    int16_t  crash_lag;     /* $A8 as $80A106 sets it: the bounce's slip     */
    int8_t   crash_frames;  /* how long $AC = $16 decelerates (see NOTES 132)*/
    uint8_t  bounce_obj;    /* the window came from an OBJECT, not a wall    */
    uint8_t  stuck;         /* $5A: consecutive frames with nowhere to go   */
    int8_t   bump_cool;     /* $5E: the kart-to-kart pair cooldown, and the
                               window the exchanged velocity survives      */
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
    uint8_t  rev_over;         /* past $3000 while the lights still run     */
    uint8_t  rev_wobble;       /* $C4 in $8095BB: the $3F00/$4F00 oscillator */
    uint8_t  rev_spin;         /* $E2 bit 0: over-revved, wheels spinning   */
    uint16_t pad, pad_prev;    /* $C4 - the composed pad word, and last frame's */
    uint16_t flags;            /* $E2 - bit 15 airborne, 2/5 drift pose,
                                  3 spinning, 6 reward armed                */
    int      row, steer_row, type;   /* $28 >> 4, $DE row, surface type    */
    int16_t  target;           /* $D6 = base_top + 8 * min(coins, 10)       */
    int      coins;
    bool     item_held;        /* $0D70,y < 0: an item (or its roulette) - boxes are
                                  not consumed while it is (src/item.c owns the word) */
    /* items on the kart (docs/ITEMS.md §4, §6) */
    int16_t  tumble;           /* $E4: the shell-hit spin rate, -$40 a frame */
    int      star_t;           /* $86: 512 frames of star                   */
    int      boo_t;            /* $82: 1152 frames invisible                */
    int      shrink_t;         /* $84: 1088 frames small after lightning    */
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
/* hit by a banana ($81:9982 -> $80:B443): the 60-frame spin.  False if a
 * star makes the kart immune.  The caller takes the coins. */
bool smk_player_hit_bump(smk_player *p, smk_kart *k);
bool smk_player_hit_banana(smk_player *p, smk_kart *k);
/* hit by a shell or lightning ($81:9ACE -> $80:B4D1 / $80:B709): the
 * tumble.  `dir` picks the spin direction ($38's parity in the game). */
bool smk_player_hit_shell(smk_player *p, smk_kart *k, int dir);
void smk_player_star(smk_player *p);              /* $80:E9F8 / $80:B4B2 */
void smk_player_feather(smk_player *p, smk_kart *k); /* $80:B578 */
void smk_player_shrink(smk_player *p, smk_kart *k, int dir); /* $80:EA3B victim */
/* use a mushroom ($80B47C): false if the kart is spinning */
bool smk_player_boost(smk_player *p);
/* The countdown's rev ($C2) and the launch test (NOTES 143/163): call
 * smk_player_rev once a frame while the lights run - it ticks on every
 * second one, like the game - then smk_player_launch when they go out. */
void smk_player_rev(smk_player *p, bool throttle, unsigned frame);
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

/* The kart sprite's SHAKE per surface class - MEASURED (tools/labs/
 * surffx.py, NOTES 197): the sprite's top row through 80 frames on each
 * class, as offsets from row 70, repeating every 8 frames.  The road's
 * own 1 px engine bob is pattern 0; gravel ($52) has only that (its
 * business is grip); grass / bridge / sand ($44 $50 $58 $5A) the hard
 * pattern with a 3 px dip; $4A $54 the soft one; mud ($5C $5E) sits 2-3
 * px DOWN and buzzes.  Classes not measured take the road's. */
static inline const signed char *smk_shake_of(uint8_t cls)
{
    static const signed char P0[8] = {  0, -1,  0, -1,  0, -1,  0, -1 };   /* the bob */
    static const signed char P1[8] = { -1, -1, -3, -1, -1,  0,  0,  0 };   /* hard   */
    static const signed char P2[8] = { -1,  0, -1,  0,  0, -1, -2, -2 };   /* soft   */
    static const signed char P3[8] = {  3,  2,  3,  2,  3,  2,  3,  2 };   /* sunk   */
    switch (cls & 0xFE) {
    case 0x44: case 0x50: case 0x58: case 0x5A: return P1;
    case 0x4A: case 0x54: case 0x56:            return P2;
    case 0x5C: case 0x5E:                       return P3;
    default:                                    return P0;
    }
}

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
#define SMK_SPR_FRAMES  49         /* 48 on the sheet + the celebration, built (NOTES 199) */
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
/* The victory pose, from the user's own screenshot of the original:
 * face on, mouth open, both white gloves raised.
 *
 * It is a 16x16 sprite in the UPPER RIGHT quadrant of frame 40 - frames
 * 33-43 each pack four 16x16 drawings - and the game draws it at double
 * the normal art scale, which is why its pixels are visibly chunkier than
 * a driving kart's.  Two earlier readings of this were wrong: frame 47 is
 * arms-up but a REAR view with bare arms, and frame 32 is just the
 * smallest tier of the ordinary front view (NOTES 180). */
/* The block the game keeps in VRAM for the player's kart at rest, matched
 * by rendering VRAM tile $180 and comparing against every sheet frame
 * (NOTES 182).  Folded it is the straight pose; UNFOLDED its own right
 * half is the standstill lean. */
#define SMK_SPR_LEAN      47
#define SMK_SPR_FRONT     46       /* the front view, drawn as its left half + mirror */
#define SMK_SPR_WIN_FRAME 48       /* built: frame 46 with sheet tiles 3 16 19 34 35 - arms up */
#define SMK_WIN_ARMS_AT   101      /* MEASURED per frame (NOTES 199): arms up from +101 ...     */
#define SMK_WIN_TOGGLE    16       /* ... 16 frames up, 16 down, up first - MEASURED             */
#define SMK_SPR_WIN_QUAD   1
#define SMK_SPR_WIN       47    /* arms up, REAR view - not the finish pose */

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
/* The per-track entity list is 32 words at $85:C800 + track*64, so this
 * is the most a course can hold. */
#define SMK_COURSE_ENTS  32
/* Everything on the plane goes through ONE depth-sorted list, and it has
 * to hold every entity AND the whole field: sized smaller, a busy course
 * fills it with obstacles and the opponents are never drawn at all -
 * which is exactly what happened on 11 of the 20 courses (NOTES 165). */
#define SMK_DRAW_LIST   (SMK_COURSE_ENTS + SMK_CHARACTERS)
#define SMK_SECT_FINISH  0x80u
#define SMK_SECT_OFF     0x7Fu
#define SMK_MAX_SECTORS  128

/* ---- Movers (NOTES 152) -------------------------------------------------
 *
 * Thwomps move in Z only, and not at all until the first lap is complete -
 * they sit parked at the top through lap one.  Measured per frame on
 * Rainbow Road, both live blocks, from the object block's height word
 * (+$1F):
 *
 *     parked   z = 4096 until the lap completes
 *     FALL     velocity starts -64 and gains -32 a frame, clamped at 0
 *     hold     135 frames on the floor, every cycle, both objects
 *     RISE     +64 a frame, linear
 *
 * LABELLED: how long the RISE lasts is script data we have not decoded.
 * It measured 119/116/96 frames on one object and 144/199/93 on the other
 * and is not proximity-driven, so this uses the value that reproduces the
 * 270-frame period one of them held (the other ran nearer 294). */
#define SMK_MOVER_PARK    4096    /* resting height, measured            */
#define SMK_MOVER_GRAV      32    /* velocity lost per frame in the fall */
#define SMK_MOVER_DROP0    -64    /* the fall's first step               */
#define SMK_MOVER_HOLD     135    /* frames on the floor                 */
#define SMK_MOVER_CLIMB     64    /* rise per frame                      */
#define SMK_MOVER_RISE     120    /* LABELLED: rise frames, see above    */
/* How high a mover must be before a kart drives under it.
 *
 * OURS, and deliberately so - it is in the roadmap ledger.  Seven rigs
 * failed to measure the game's own rule (NOTES 176) and the user called
 * it: "Confirmed that it is one kart sprite in altitude, you can pass.  I
 * would even say 80% of it.  This is one of the things we don't need to
 * do super accurate and we can implement our own rule."
 *
 * One screen pixel is 25029 kart-z units, and a mover's +$1F word is the
 * high 16 bits of that same 24-bit height, so one pixel is ~97.8 of these
 * units.  A kart sprite reads about 16 px, and 80% of that is ~13 px:
 *
 *     0.8 * 16 px * 97.8 = 1252  ->  1280, which is also exactly 20
 *     frames of the measured +64 climb
 *
 * The user's own recorded Bowser Castle run agrees with it on every
 * sample: they CRASHED at heights 0, 0, 0, 0, 448 and 960, and PASSED
 * within a kart's width at 2880, 3008, 3648 and 4096.  Every one of those
 * falls on the correct side of 1280.  The run cannot pin the number - it
 * has no samples between 960 and 2880 - but it can and does refute
 * anything outside that band.
 *
 * The old value was SMK_MOVER_PARK / 2 = 2048, which is also consistent
 * with the recording and is NOT consistent with the user's eye: at 1500 a
 * Thwomp is drawn 15 px up, looks plainly lifted, and still hit you.
 * That is the bug they reported. */
/* SECOND CORRECTION (the user: "right now it has to go too high to let you
 * pass, meaning you hit something that is not there because the thwomp
 * is already high enough").  The 97.8-per-pixel ruler above is the KART's
 * z; the mover is DRAWN at SMK_MOVER_UNIT = 274 per world px and lifted by
 * 256/depth * rh/112 on screen - at the player's depth ~16.8 screen px
 * per world px, so z = 1280 was drawn 78 px up, 1.2 kart heights, and
 * still hit.  80% of a 64 px kart at the contact depth (61 + the object
 * radius) is z ~ 900-1050; the recording crashed at 960 and passed at
 * 2880, and 960 is the lowest value both agree on (`>` passes above it). */
#define SMK_MOVER_CLEAR    960
enum { SMK_MV_PARK, SMK_MV_FALL, SMK_MV_HOLD, SMK_MV_RISE };
typedef struct { int32_t z; int16_t zv; uint8_t phase; int16_t t; } smk_mover;

/* Do this track's objects move?  Appearance is per THEME (NOTES 152 -
 * $0D2C is cup position, not object type), and the user reports Thwomps
 * on Bowser Castle and Rainbow Road.  LABELLED: which themes have movers
 * is read off that, not decoded. */
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
    /* finish-line rectangle */
    int      fin_cell, fin_w, fin_h;
    /* The game's own starting grid, DECODED (NOTES 161).  Per-track
     * record reached through $81:8A79; three words that $81:903C turns
     * into eight karts.  See smk_course_start below. */
    int16_t  grid_x, grid_y, grid_step;
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
    struct { uint8_t kind; uint16_t x, y; } ent[SMK_COURSE_ENTS];
    uint8_t  dead[SMK_COURSE_ENTS];   /* knocked out by a star; back on respawn */
    int      nent;
    smk_mover mv[32];         /* one per ENTITY, NOTES 152/155 */
    int      theme;           /* for smk_theme_has_movers */
} smk_course;

bool smk_course_load(const smk_rom *rom, int track, smk_course *out);

bool smk_theme_has_movers(int theme);
/* Show every object, not the game's live pair.
 *
 * The ROM keeps two object blocks in a one-player race ($819136) and
 * respawns them as the lap segment changes, which is an OAM budget, not a
 * statement about the track: pipes wink in and out as you drive. We have
 * no such budget, so by default every entity is drawn AND collided - the
 * same named divergence as S7's full-resolution perspective. `--rom-spawn`
 * restores the live pair. */
extern bool smk_obj_show_all;
void smk_course_movers_reset(smk_course *c);
/* one frame; `activated` is false until the first lap is complete */
void smk_course_movers_step(smk_course *c, bool activated);
/* The height of live slot j, in WORLD pixels.
 *
 * It must be world units, not screen ones: a height converted at the
 * kart's own depth and then reused at any distance puts a far Thwomp a
 * third of the way up the screen (user: "they do look too high").  The
 * caller multiplies by smk_project's scale, exactly as the ground and the
 * sprites do, so the lift shrinks with distance like everything else.
 *
 * The unit comes from the kart's own hop, SWEPT rather than sampled
 * (tools/labs/hop_shadow.py).  Hopping the kart in the oracle and logging
 * every frame of the arc gives sixteen ($1F, screen lift) pairs spanning
 * $1F = 48..856, and a fit through the origin is
 *
 *     lift = $1F / 65.3 screen lines,  rms 0.39 px
 *
 * at the kart's depth of 61 px from the eye.  The fit is confirmed by
 * geometry it was not given: it implies the kart's ground line is
 * 20.36 + 4972/61 = 101.9, and SMK_PLAYER_LINE - measured independently -
 * is 102.0.  At that depth the scale is 256/61 = 4.197 screen px per
 * world px, so one world pixel is 65.3 * 4.197 = 274 units of $1F.
 *
 * The old 410 came from a SINGLE reading ($1F = 1173 -> 12 px) and made
 * every Thwomp sit 1.50x too low - "the height they get is lower than in
 * game" (user).  One point can calibrate a law but cannot choose between
 * laws; this one is a sweep.
 *
 * LABELLED: the 1/depth falloff is the projection's own law, applied here
 * as it is everywhere else, not separately measured - the sweep is all at
 * the player's fixed depth. */
#define SMK_MOVER_UNIT 274.0f
float smk_mover_world(const smk_course *c, int slot);

/* An object's height in $1F units, and the ONE place that answers it.
 *
 * smk_course_movers_reset parks every one of the 32 slots at
 * SMK_MOVER_PARK, whatever the theme, and smk_course_movers_step then
 * returns early for a theme that has no movers - so on Mario Circuit the
 * z of a pipe sits at 4096 for the whole race.  Drawing never noticed,
 * because smk_mover_world gates on the theme and returned 0; collision
 * did not gate, and skipped anything above half the parked height:
 *
 *     if (crs->mv[i].z > SMK_MOVER_PARK / 2) continue;
 *
 * which is every pipe on every non-mover track.  They drew on the ground
 * and you drove straight through them ("I can pass through green pipes...
 * no collision" - user).  From commit 3f0b5d5, when movers landed.
 *
 * NOTES 151 already paid for this lesson once, in the other direction:
 * "there should be two thwomps and there is only one, but you can still
 * hit the invisible one".  Whatever is drawn is what you can hit, so the
 * height that decides both comes from here and nowhere else. */
static inline int smk_mover_z(const smk_course *c, int slot)
{
    if (slot < 0 || slot >= 32 || !smk_theme_has_movers(c->theme)) return 0;
    return c->mv[slot].z;
}

/* ---- The object shadow -------------------------------------------------
 *
 * One shared 32x8 solid-black ellipse, and the SAME one under every object
 * and under the player's kart when it hops (user: "shadow is exactly the
 * same for all objects, because it is an oval").  It is not generated: it
 * is two 4bpp tiles in the shared sprite blob at $C1:0000, decompressed
 * offset $120, which the game blits into object-sheet slots 43/44 - slots
 * that all six distinct theme sheets leave blank, which is exactly why it
 * is the same oval everywhere.  OAM assembles it as four 8x8 sprites,
 * tiles $0EB $0EC $0EC $0EB-mirrored, palette 5 index 14 = $0000, pure
 * black with no shading.
 *
 * It is drawn on ALTERNATE FRAMES ONLY (measured: the strip appears on
 * every odd frame of the kart's hop and on no even one) - the SNES faking
 * a translucent shadow, since sprites cannot blend.  We render the effect
 * that flicker produces, a 50% darkening, rather than the flicker itself:
 * the port runs at a higher and unlocked frame rate, where the flicker
 * would read as strobing rather than as shade.  LABELLED as a deliberate
 * divergence; the ART and the colour are the ROM's.
 *
 * The art is 32x8 screen px at the kart's own depth of 61, so in world
 * terms it is 32*61/256 = 7.63 by 1.91 px, and it scales with distance by
 * the same projection as everything else. */
#define SMK_SHADOW_W    32
#define SMK_SHADOW_H     8
#define SMK_SHADOW_SRC  0xC10000u   /* the shared sprite blob            */
#define SMK_SHADOW_OFF  0x120       /* byte offset of the first tile     */
#define SMK_SHADOW_DARK 50          /* percent of the ground left showing */
/* world size = screen size at the kart's depth / the scale there */
#define SMK_SHADOW_WW   (SMK_SHADOW_W * SMK_CAM_TRAIL / SMK_PROJ_LES)
#define SMK_SHADOW_WH   (SMK_SHADOW_H * SMK_CAM_TRAIL / SMK_PROJ_LES)
typedef struct { uint8_t px[SMK_SHADOW_H][SMK_SHADOW_W]; bool ok; } smk_shadow;
bool smk_shadow_load(const smk_rom *rom, smk_shadow *out);


/* The starting grid, from the game's own per-track record (NOTES 161).
 *
 * $81:903C builds it: x starts at grid_x and alternates by grid_step, y
 * starts at grid_y and steps by 24 a slot, heading is left at 0 - every
 * course's grid faces -Y, which is why the record carries no angle.
 * Slot 0 is the POLE, at the front; the slots run backwards from there.
 *
 * A kart ALONE on the track - a time trial - does not use a grid slot:
 * $818F7F builds the grid and then nudges the front kart by
 * (grid_step/4, -16), which is the start position both time-trial
 * recordings show to the pixel.  That is smk_course_start_solo. */
void smk_course_start(const smk_course *c, int slot,
                      float *x, float *y, uint16_t *heading);
void smk_course_start_solo(const smk_course *c,
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
/* one 16x16 quadrant of a packed frame (33-43), at any scale */
void smk_draw_sprite_quad(const smk_sprites *s, int frame, int quad,
                          const uint32_t *palette, int pal_base,
                          int cx, int cy, int scale, bool hflip,
                          uint32_t *pixels, int w, int h, int pitch_px);

/* The game's own HUD sprite set (docs/NOTES.md 085): $81:E856
 * decompresses $C1:0000 to $7F:C000 and DMAs offset $200 (4096 bytes =
 * 128 tiles) into the sprite tiles $40-$BF.  Digits 0-4 are tiles
 * $A7-$AB and 5-9 are $B7-$BB - the strip is 5 wide and wraps by the
 * 16-tile VRAM row.  Sprite palette $C0 (from the live OAM attribute). */
#define SMK_HUD_TILES   128
#define SMK_HUD_TILE0   0x40
#define SMK_HUD_PAL     0xC0
/* The stream's FIRST $200 bytes are a second block, and they are not
 * spare: the sixteen tiles before offset $200 are uploaded to sprite
 * tiles $EF-$FE, and $FB-$FE are the start light's four lamps (NOTES
 * 162).  Matched tile for tile against the oracle's VRAM. */
#define SMK_HUD_LOW_TILES 16
#define SMK_HUD_LOW_TILE0 0xEF
typedef struct {
    uint8_t px[SMK_HUD_TILES][64];              /* sprite tiles $40-$BF */
    uint8_t low[SMK_HUD_LOW_TILES][64];         /* sprite tiles $EF-$FE */
    bool ok;
} smk_hud;
bool smk_hud_load(const smk_rom *rom, smk_hud *out);
/* tile index for one decimal digit */
static inline int smk_hud_digit(int d)
{ return (d < 5 ? 0xA7 + d : 0xB7 + (d - 5)) - SMK_HUD_TILE0; }
/* One 8x8 tile by its SPRITE-VRAM number, from either block; NULL if the
 * number is in neither. */
static inline const uint8_t *smk_hud_tile_px(const smk_hud *h, int vram_tile)
{
    if (!h->ok) return 0;
    if (vram_tile >= SMK_HUD_TILE0 && vram_tile < SMK_HUD_TILE0 + SMK_HUD_TILES)
        return h->px[vram_tile - SMK_HUD_TILE0];
    if (vram_tile >= SMK_HUD_LOW_TILE0
        && vram_tile < SMK_HUD_LOW_TILE0 + SMK_HUD_LOW_TILES)
        return h->low[vram_tile - SMK_HUD_LOW_TILE0];
    return 0;
}

/* ---- The start: Lakitu and his light (NOTES 162) ---------------------
 *
 * $809FE1 loads $0146 with -$150 and $80A1F8 counts it up one a frame,
 * releasing the field on the frame it reaches 0 (NOTES 145).  Everything
 * the player sees hangs off that frame number, and all of it was read
 * out of the game's own OAM through the Python oracle
 * (tools/labs/lakitu.py), because none of it is reachable from MAME.
 *
 * He is four 16x16 sprites in a 32x32 block at a FIXED screen position,
 * every one of them H-flipped, over sprite palette 5 ($D0); the light is
 * three 8x8 sprites hanging off his rod, palette 4 ($C0).  The art is
 * the HUD stream's - see smk_hud above, including the block it used to
 * skip, which is where the lamps live.
 *
 * MEASURED, NOT DERIVED: the drop and the fly-away are the trajectory
 * the game produced, frame by frame, not a law.  The generator was
 * looked for and not found - the only WRAM word that tracks his sprite
 * turned out to be the OAM shadow at $0220 - so this is the same choice
 * NOTES 152 made for the movers: port the cycle, not the machine that
 * makes it.  tools/labs/lakitu_full.py re-derives the fixture. */
#define SMK_COUNT_FRAMES  336      /* $809FE1: $0146 = -$150            */
#define SMK_START_X        36      /* his block's left edge, SNES px    */
#define SMK_START_LAMP_X   63      /* the light hangs here              */
#define SMK_START_LAMP_DY  16      /* first lamp, below his block's top */
typedef struct {
    bool    on;                    /* is the sequence running him       */
    int     x, y;                  /* top-left of the 32x32 block       */
    bool    cheer;                 /* the pose the green light brings   */
    uint8_t lamp[3];               /* sprite tile per lamp, top down    */
    int     lit;                   /* lamps alight, 0..3                */
    /* the four quadrants, each a 16x16 sprite drawn H-FLIPPED */
    struct { int dx, dy; uint8_t tile; } quad[4];
} smk_start;
#define SMK_START_LAST    439      /* he is parked clear of the screen  */
/* t counts frames from the arm; the field is released at t = 336, and he
 * is still on screen for a hundred frames after it */
void smk_start_frame(int t, smk_start *out);
/* the lamps' own tiles, so a caller can name them */
#define SMK_LAMP_GREEN_OFF 0xFB
#define SMK_LAMP_GREEN_ON  0xFC
#define SMK_LAMP_RED_OFF   0xFD
#define SMK_LAMP_RED_ON    0xFE

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
/* Which CGRAM row the object art indexes.
 *
 * $F0 for most themes: on Mario Circuit its $FA-$FD are the pipe's greens
 * and on Rainbow Road its $FA-$FF are a grey ramp, both correct.  On
 * BOWSER CASTLE $F0 is CE0000 / FF6300 / FFBD00 - reds and ambers - so the
 * Thwomps came out looking like lava (user).
 *
 * MEASURED: over 600 frames of a Bowser Castle race the game puts palette
 * 7 ($F0) in OAM exactly ZERO times, while an object-free Ghost Valley run
 * uses it 120 - so $F0 is not what its objects are drawn from.  $C0 is the
 * only row on that theme carrying the grey ramp the art indexes ($CA-$CE =
 * EFEFEF D6D6D6 BDBDBD A5A5A5 7B7B7B).
 *
 * LABELLED: the per-theme table is ours.  The OAM tally could not isolate
 * the objects' own row (karts, HUD and tyre smoke dominate the counts), so
 * where the base really comes from is still undecoded - this fixes the one
 * theme that is provably wrong and leaves the rest where they were. */
#define SMK_OBJ_PAL     0xF0
int smk_obj_pal(int theme);
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
/* The drawn size in WORLD pixels.  MEASURED (NOTES 159), and the two are
 * no longer equal on purpose.
 *
 * WIDTH.  Taking each sprite's depth from the ground row it stands on -
 * not from our own size law, which is what made an earlier check circular
 * and wrong - the real game's objects come out at:
 *
 *     Thwomp  depth 72.1  ink 23.8 px -> 6.71 world px
 *     Thwomp  depth 66.1  ink 23.8 px -> 6.15
 *     pipe    depth 57.8  ink 22.0    -> 4.97
 *     pipe    depth 64.9  ink 21.9    -> 5.54
 *     pipe    depth 136.9 ink 14.1    -> 7.52
 *     pipe    depth 145.6 ink 13.2    -> 7.49      mean 6.40
 *
 * The spread IS the hardware's band quantisation - the game holds one
 * drawing across a range of depths, so the implied world size falls as you
 * close on it.  6.40 is the mean, and 0.75 * 8.5 = 6.38.  At 16 the port
 * drew 12.0 and every object was 1.9x too wide (user: "Now they look
 * bigger than real game").
 *
 * HEIGHT is a SHAPE, not a world size, and is tuned to what the eye sees.
 *
 * Getting the world height geometrically right makes objects look wrong,
 * and the reason is the port's own view.  It renders the 256x112 race view
 * into (say) 512x448, so a LINE gets 4 host px while a horizontal SNES
 * pixel gets 2 - the road is stretched 2x vertically, which is the better
 * road angle the port is for.  A billboard projected honestly into that
 * stretched space comes out twice as tall as it reads on a SNES, and the
 * user's frame showed exactly that: our ink measured 23.0 SNES px wide by
 * 28.0 lines - both close to the game's 23.8 x 32.5 - and yet on screen it
 * was aspect 0.39 where the game reads 0.87 ("they need to be shorter").
 *
 * So the height is set from the SHAPE the game presents, not the height it
 * occupies in a stretched world.  On screen the game's object is 123 x 141
 * px in the user's own capture, aspect 0.87; the drawn ink here is
 * 0.75 * pw wide by ph tall in square host pixels, so
 *
 *     ph = 0.75 * pw / 0.87   ->   H = 0.862 * W = 7.4
 *
 * The kart is drawn the same way - square host pixels per art pixel - so
 * objects and karts now share one convention.  LABELLED: the width is a
 * measurement of the game, this is a match of its proportions. */
#define SMK_OBJ_PIPE_W  8.5f      /* world px across - measured        */
#define SMK_OBJ_PIPE_H  7.4f      /* shape, not size - see above       */

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

/* The NEAR drawing, measured on the real game (NOTES 157).
 *
 * User: "pipe scaling is finally right, don't touch it, ever ... The only
 * thing that is incorrect in both is the sprite shown when getting closer.
 * We are only scaling the sprite used for far away objects."
 *
 * Right.  Band 0 is not a 16x16 drawing at all - it is a 32x32 metasprite
 * built the way the SNES builds every symmetric thing in this game (the
 * kart is $180/$180-H over $1A0/$1A0-H, the shadow $0EB $0EC $0EC $0EB-H):
 *
 *     [ base 0 | base 0 mirrored ]      tiles 0,1,16,17
 *     [ base 2 | base 2 mirrored ]      tiles 2,3,18,19
 *
 * Measured off two uncropped 1444x1036 frames the user captured, by
 * matching the sprite's row-width profile against every assembly the sheet
 * can make: a Bowser Castle Thwomp at band 0 is 24x32 and picks this at
 * rms 2.50 with the next candidate at 4.30, and a Mario Circuit pipe picks
 * the SAME pair independently at rms 3.78.
 *
 * Bases 0/2/4/6 were previously dismissed as "skewed perspective variants"
 * because base 0's ink is right-aligned (x 4..15).  That is exactly what
 * the LEFT HALF of a mirrored pair looks like; reading it as a whole
 * drawing is why the near art was never found and why the port magnified
 * the far one instead.
 *
 * The ink fills the same fraction of its block as the 16x16 drawings do
 * (24/32 = 12/16 across, 32/32 = 16/16 down), so this is drawn into the
 * SAME rect at the SAME size - four times the detail, and the scaling law
 * is untouched.  Bands 1 and 2 keep the drawings they had. */
#define SMK_OBJ_NEAR_TOP  0        /* top row's left half   */
#define SMK_OBJ_NEAR_BOT  2        /* bottom row's left half */
#define SMK_OBJ_NEAR_W   32
#define SMK_OBJ_NEAR_H   32

#define SMK_OBJ_NEAR    ((float)SMK_OBJ_RADIUS)
typedef struct { uint8_t px[SMK_OBJ_TILES][64]; bool ok; } smk_objgfx;
bool smk_objgfx_load(const smk_rom *rom, int theme, smk_objgfx *out);

/* ---- Opponent karts ---------------------------------------------------
 *
 * The DATA is the ROM's (sector map, waypoints, acceleration tables) and
 * the steering law matches the decoded shape; the slew rate, the
 * target-speed entry per kart and rubber-banding are PLACEHOLDERS,
 * labelled in src/ai.c. */
/* How far a kart must have travelled from where its rescue timer last
 * reset to count as NOT stuck.  Ours, labelled: the ROM's own rescue
 * trigger is not decoded (NOTES 057/169). */
#define SMK_AI_STUCK_PX      128
#define SMK_AI_RESCUE_FRAMES 600
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
    int      anchor_x, anchor_y;  /* where it was when no_prog last reset */
    int      lap_cool;      /* one lap event per strip transit          */
    int      rank;          /* $E6 >> 1: place, 0 = leading              */
    int      row;           /* $C8 >> 1: the rubber band's target row    */
    /* $80ADA0's own inputs, measured against a recorded race (NOTES 174).
     * is_player is $10 bit 15 - the flag the whole rubber band turns on,
     * because every branch asks it of the NEIGHBOURING kart.  da is $DA,
     * static per slot (0,0,0,0,2,4,6,8 in the race that was logged), and
     * is also the value the routine is entered with ($80AD9F lda $DA,x).
     * skill is $C1 & 7, which picks the row of $80AF0F - NOT the engine
     * class, which is what this port used to index it by. */
    int      is_player;     /* $10 bit 15                                */
    int      da;            /* $DA, and the routine's own parameter      */
    /* Which $80AF0F row this kart uses = ($7F + lap) & 7.  $C1 is the
     * HIGH byte of the word at $C0, and $80:89B6 increments it by $0100
     * immediately before comparing against $F8, the progress watermark:
     * it is the lap counter, based at $7F for the line not yet crossed.
     * So the catch-up distances are re-tuned EVERY LAP, and lap one
     * indexes row 7 - past the end of the table, into the code of
     * $80AF5F - which is the original's own overrun (NOTES 174).
     * No constant reproduces this: against a recorded race the live
     * value scores 94.2% and the best constant 78.8%. */
    int      skill;         /* ($7F + lap) & 7: which $80AF0F row        */
    int      trouble;       /* $84 != 0 or $10 & $0020 -> the $18 row    */
    int      branch;        /* which $80ADA0 branch answered (diagnostic) */
    /* hit by an item (docs/ITEMS.md §6).  The AI has no $A6 machine here,
     * so the tumble is carried on the racer: pose spin, speed to zero. */
    int      hit_t;         /* frames left in the reaction              */
    int16_t  tumble;        /* $E4, starts $2000 for an AI               */
    int16_t  spin_pose;     /* added to the drawn angle while tumbling   */
    int      hit_dir;
    int      shrink_t;      /* $84                                       */
    int      coins;         /* $0E00,y for this kart: a bump costs one   */
    int      hit_kind;      /* what hit it: 1 banana 2 shell 3 lightning 4 coinless bump */
    int      weapon_cool;   /* frames until this AI may use its weapon again (NOTES 190) */
    int      star_t;        /* Mario / Luigi's star: frames left (OURS: the player's $200) */
    /* When this kart crossed for the last time, in race frames, and where
     * it came.  Nothing tracked either before: the race ended the instant
     * the PLAYER finished and the other seven simply stopped existing, so
     * a results table had nothing to show. -1 = still going. */
    long     finish_frame;
    int      place;
} smk_racer;
/* hit by an item, on the AI's racer (docs/ITEMS.md §6): 1 banana, 2 shell,
 * 3 lightning.  `dir` picks the spin direction. */
void smk_racer_hit(smk_racer *r, int kind, int dir);

/* ---- The rubber band (NOTES 167) --------------------------------------
 *
 * The user: "no matter how fast you are they can keep up... when one of
 * them gets behind their original position, they start to go faster
 * (sometimes cheating) until they catch up and get back to their place."
 * That is $80AD5E, and it has two halves.
 *
 * $80AD96 picks a ROW into the target-speed table and stores it at $C8,
 * which $80B074 then adds to the waypoint attribute's own index:
 *
 *     target = $06B0[ (attr & 3) * 2 + $C8 ]
 *
 * The row comes from the kart's RANK ($E6, from the sort at $80A047,
 * which also builds rank-ordered kart lists at $010C/$010E/$0110) and
 * the DSP-1 distance ($80AF5F) to the kart one place ahead, compared at
 * $80AEFC against a per-(class, rank) table.  Measured through a race:
 * a kart far from the one ahead takes $08 and runs 758-1002, one in the
 * pack takes $10 and runs 548-784, and a leader far enough clear takes
 * $00 and eases off.
 *
 * Then $80B086 adds a second, flat correction - $B099 by the $DA timer
 * if it is running, otherwise $B0A1 by RANK. */
/* The four rows, and what they are worth - read out of the ROM's own
 * table rather than named from the branches, because the naming was
 * wrong first time.  At 50cc on a plain waypoint:
 *
 *     CHASE $08   512      the fastest.  A kart that has lost touch.
 *     EASE  $00   448      the leader with clear air: it backs off.
 *     HOLD  $10   256      in the pack, holding station.
 *     SLOW  $18   256      slowest of all; $80ADB0 hands it out in a
 *                          state this port does not model ($84,x set).
 *
 * So the band pulls both ways: drop back and you are given the fast row
 * until the gap closes, get clear at the front and you are given a
 * slower one.  That is the user's "no matter how fast you are they keep
 * up", from both ends. */
/* The four rows of $80ADA0.  NOT named for speed: the speed of a row is
 * a property of the physics block, w[16 + (surface&3) + row], and there
 * the measured order is $08 > $00 > $10 > $18 in every class.  $00 is a
 * FAST cruising row - this port used to treat it as an ease-off and hand
 * it only to the leader, which is why our field was 0.1% on $00 where
 * the real game is 20% (NOTES 174). */
#define SMK_AI_ROW_EASE   0        /* $80ADF1: $C8 = $00, the second fastest */
#define SMK_AI_ROW_CHASE  4        /* $80ADDC: $C8 = $08, catching up   */
#define SMK_AI_ROW_HOLD   8        /* $80AE1F: $C8 = $10, in the pack   */
#define SMK_AI_ROW_SLOW  12        /* $80ADB0: $C8 = $18, the slowest    */
/* $80AF0F, [class][rank]: how far the kart ahead may get before this one
 * starts chasing.  Row 3 is the ROM's fourth class, which this port has
 * no selector for; 50/100/150cc take 0..2. */
/* $80AF0F verbatim: 8 rows of 8 words, indexed by ($C1 & 7) then rank.
 * Only rows 0-4 are data - row 5 onwards is the CODE of $80AF5F, and the
 * original really does index into it, because $C1 & 7 reaches 5 for most
 * of the field.  Those karts get an effectively infinite catch-up
 * distance.  Reproduced rather than tidied: it is the game's behaviour,
 * and a tidied table would chase where the original does not (NOTES 174). */
#define SMK_AI_SKILLS 8
extern uint16_t SMK_AI_CATCHUP[SMK_AI_SKILLS][8];
bool smk_ai_catchup_load(const smk_rom *rom);
extern int smk_ai_player_block;   /* which racers[] slot is the human */
/* The race clock, in frames since the lights, so smk_racer_step can stamp
 * a finish.  main.c owns it; the labs and tools leave it at 0. */
extern long smk_race_frame;
/* Five laps from a grid BEHIND the line is SIX crossings (NOTES 052), so a
 * kart has finished when its lap counter reaches this. */
#define SMK_RACE_CROSSINGS (SMK_RACE_LAPS + 1)
extern int smk_ai_branch;        /* which $80ADA0 branch last answered */
extern int smk_ai_skill;          /* $C1 & 7 stand-in; -1 = engine class */
/* $80AD96 -> $80ADA0 on one kart, exposed so tools/rowcheck.c can replay
 * it against the real game's logged inputs rather than judge it by feel. */
int smk_ai_row_for(const smk_racer *r, const smk_racer *ahead,
                   const smk_racer *behind, const smk_racer *third,
                   int *s04, int *s06);
/* $80B0A1 by rank: the flat correction under the row */
extern const int16_t SMK_AI_RANK_BONUS[8];
/* Fill every racer's rank and row, once a frame, before they step. */
void smk_ai_rubber(smk_racer *racers, int n, const smk_course *crs, int cls);

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

/* ---- Lakitu's lap sign (NOTES 168) ------------------------------------
 *
 * Crossing the line to START a lap brings him back with a sign.  Read
 * out of the game's own OAM at a lap-completing crossing on track 7
 * (tools/labs/lakitu_lap.py) - and note it has to be that crossing: the
 * first one, $7F -> $80, is the grid leaving the line and shows nothing
 * (NOTES 052), which cost one capture.
 *
 * Four sprites, all palette 5, moving as one group from (X, Y):
 *
 *     (X,     Y     )  16x16  tile $A0        the plate, "LAP" on it
 *     (X + 8, Y     )   8x16  tile $A3        the plate's edge bar
 *     (X + 16,Y     )   8x16  tile $A4 + n    the numeral, n = lap - 2
 *
 * The game places ONE 16x16 sprite at X+8 covering both, and for lap 2
 * its halves happen to be the bar and "2".  Reading that as "the digit
 * sprite is 16x16 at $A3 + n" is wrong and shipped a glitch: lap 4 drew
 * $A5 and $A6 side by side, "34", with the bar gone.  The numeral is
 * ONE tile column wide - $A4/$A5/$A6 are 2/3/4 - so the two columns are
 * drawn separately and every lap comes out right (NOTES 168b).
 *     (X + 1, Y + 16)  16x16  tile $46 HFLIP  his cloud, left
 *     (X + 17,Y + 16)  16x16  tile $44 HFLIP  his cloud, right
 *
 * He is on screen for about 162 frames, entering from the left and
 * drifting right and down: (63,22) at frame 40, (91,44) at frame 80. */
#define SMK_LAPSIGN_FRAMES  165
#define SMK_LAPSIGN_PLATE   0xA0
#define SMK_LAPSIGN_BAR     0xA3   /* the plate's edge bar, 8x16      */
#define SMK_LAPSIGN_DIGIT   0xA4   /* + (lap - 2): $A4/$A5/$A6 = 2/3/4 */
#define SMK_LAPSIGN_CLOUD_L 0x46
#define SMK_LAPSIGN_CLOUD_R 0x44
/* The last lap gets its own plate instead of plate+digit: a 32x16 block
 * at $AC/$AE over $BC/$BE.  LABELLED: read off the sheet, not captured -
 * reaching the fifth crossing costs five laps of interpreter time. */
#define SMK_LAPSIGN_FINAL_L 0xAC
#define SMK_LAPSIGN_FINAL_R 0xAE
typedef struct {
    bool on;
    int  x, y;          /* the group's top-left, in SNES px */
    int  plate, digit;  /* sprite tiles; digit < 0 on the final lap */
    bool final_lap;
} smk_lapsign;
/* t counts frames from the crossing */
void smk_lapsign_frame(int t, int lap, int laps, smk_lapsign *out);

/* ---- Lakitu waving you home (NOTES 184) --------------------------------
 *
 * The third of his jobs, and the last one missing.  CAPTURED, from a
 * finish reached by forcing the lap word rather than driving five laps in
 * the interpreter - which is what had made it unreachable ("needs OAM at
 * a finish", and MAME gives no OAM).
 *
 * His group, from OAM and stable across the whole pass, as offsets from
 * his head - all 16x16, palette 5, the same palette his other two jobs
 * use:
 *
 *     ( 0,  0)  $4A   his head
 *     ( 0, 16)  $4C   cloud, left
 *     (16, 16)  $44   cloud, right
 *     (16,  0)  the flag's upper half
 *     (24,  8)  the flag's lower half
 *
 * The flag WAVES through three tile pairs, about 17 frames each, and the
 * pairs are $6C/$6E, $80/$82 and $8C/$8E - the checker across $68-$9F the
 * roadmap predicted.  He enters from above the screen (y is signed and
 * starts at -48) at the left and sweeps right and down over 230 frames. */
#define SMK_FLAG_FRAMES  230
#define SMK_FLAG_WAVE     17    /* frames per wave pose, measured        */
#define SMK_FLAG_HEAD   0x4A
#define SMK_FLAG_CLOUD_L 0x4C
#define SMK_FLAG_CLOUD_R 0x44
typedef struct {
    bool on;
    int  x, y;          /* his head, in SNES px; y may be negative       */
    int  pose;          /* 0..2, which wave pose                         */
} smk_flag;
/* The flag's own art, loaded straight from the shared blob.
 *
 * NOT through smk_hud: that set's indices are not VRAM tile numbers, and
 * asking it for $6C returns the FINAL LAP plate - which is exactly what
 * the first version of this drew (NOTES 184).  A VRAM tile's bytes are at
 * (tile - 48) * 32 in the blob, the same rule the coin follows. */
typedef struct {
    bool ok;
    uint8_t px[3][2][16 * 16];    /* the flag: 3 wave poses x 2 halves  */
    uint8_t head[16 * 16];        /* $4A                                */
    uint8_t cloud[2][16 * 16];    /* $4C left, $44 right                */
} smk_flagart;
bool smk_flag_load(const smk_rom *rom, smk_flagart *out);
/* t counts frames from the final crossing */
void smk_flag_frame(int t, smk_flag *out);

/* ---- Lakitu fishing you out (NOTES 168a) ------------------------------
 *
 * The rescue's STATE MACHINE has been the ROM's since NOTES 113/124 -
 * $A0 walks fall -> $0C -> $0E, the kart is lifted, carried x then y at
 * 2 px a frame, and lowered $80 a frame.  What was missing is him.
 *
 * Captured on Ghost Valley (tools/labs/lakitu_rescue.py), walking the
 * kart off the road and letting the GAME decide it had fallen:
 *
 *     f0    $A0 = fall   the drop, 60 frames
 *     f60   $A0 = $0C    carried (1008,700) -> (968,600), x then y.
 *                        Lakitu is NOT drawn: his sprites sit parked at
 *                        x 292, off the right of a 256-wide screen.
 *     f131  $A0 = $0E    the kart is lowered - and HE IS ON SCREEN,
 *                        five 16x16 sprites at a fixed screen x of 97,
 *                        descending with it from y -56 to y +38.
 *     f229  $A0 = 0      released.
 *
 * The descent tracks the kart's own z: $3000 falling at $80 a frame is
 * 96 frames, which is the 98 the phase lasts. */
#define SMK_RESCUE_X       97      /* his block's screen x, fixed        */
#define SMK_RESCUE_FRAMES  96      /* the $0E phase, measured             */
/* His row, frame by frame from the start of the phase.  It is NOT a ramp
 * from the kart's height: he holds at -40, RISES to -56, and only then
 * comes down to +38.  The first port drove it from the kart's z, read
 * out of $1E - which is the LOW word of a 24-bit height and alternates
 * 0/-32768, so the position was built on a misreading (NOTES 169a). */
int smk_rescue_y(int t);
/* the assembly: $42/$40 over $46/$44, plus $48 beside the lower right */
#define SMK_RESCUE_TL      0x42
#define SMK_RESCUE_TR      0x40
#define SMK_RESCUE_BL      0x46
#define SMK_RESCUE_BR      0x44
#define SMK_RESCUE_EXTRA   0x48

/* ---- Kart against kart (NOTES 166) ------------------------------------
 *
 * NOTES 112 concluded there was no kart-to-kart response, from a demo in
 * which nobody actually leant on anybody.  There is one, and it is three
 * routines:
 *
 *   $81982A  the test.  |dx| and |dy| both under 4 PIXELS on the two
 *            karts' centres, neither in the pair's cooldown ($5E), and
 *            both near the ground ($1F <= $0420).  A broadphase over two
 *            position-sorted lists feeds it.
 *   $819867  marks the pair - $5E = 8 on both - and ORDERS them so the
 *            heavier is X, by $4E, which $81923A loads from the table at
 *            $81:9277 indexed by the object type.  Its first eight
 *            entries are the drivers, and they are SMK's weight classes:
 *            Bowser and DK Jr $1B, Mario and Luigi $1A, the other four
 *            $19.
 *   $819B06  the answer - but only for a FIRST contact.  While the
 *            pair's cooldown is still running down it goes to $819C93
 *            instead, which does nothing at all unless the two have
 *            nearly stopped, in which case it nudges them apart at
 *            $0180.  For a first contact with equal weights it EXCHANGES the two
 *            velocity vectors ($819CB8) and then, if both components
 *            still share a sign - the exchange left them converging -
 *            shoves them apart ($819CD2) - that separation is measured
 *            but NOT ported, see src/kart.c and ledger S24.  Measured in
 *            the oracle: two karts placed on the same pixel came out
 *            with each other's velocity exactly.
 *
 * The port carries the exchange in the kart's own $22/$24 and holds it
 * for the pair's eight frames the way the wall bounce holds its own
 * (NOTES 044) - without that, smk_kart_face rebuilds the velocity from
 * speed and heading on the next frame and the bump never happens. */
#define SMK_BUMP_BOX     4      /* $81982A: |dx|, |dy| strictly under 4  */
#define SMK_BUMP_COOL    8      /* $8198A8: $5E = 8 on both karts        */
#define SMK_BUMP_PUSH    0x80   /* $819CD2: the shove when still closing */
#define SMK_BUMP_Z_MAX   4      /* $81985A: $1F <= $0420, so 4 px of air */
/* $81:9277, first eight entries, in SMK_DRIVERS order */
extern const uint8_t SMK_KART_WEIGHT[SMK_CHARACTERS];
/* One contact.  Order is the game's: `a` must be the heavier kart.
 * Returns true if the pair touched and the response ran. */
bool smk_kart_bump(smk_kart *a, int wa, smk_kart *b, int wb);
/* The whole field, once a frame: every pair tested, heaviest first. */
void smk_karts_collide(smk_kart **karts, const uint8_t *weight, int n);

/* ---- Spilled coins (src/coinfx.c, NOTES 183) --------------------------
 *
 * When a kart is hit it drops coins, and they are thrown UP and fall back
 * (the user).  The ROM side is decoded: $85:E4B2 takes ONE coin, $85:E4E5
 * takes FOUR, and both reach $85:E5E3 which spawns effect objects.
 *
 * The ART is the game's, found by dumping VRAM at the moment of a loss and
 * searching the shared sprite blob for the tiles OAM named: three 16x16
 * spin frames - wide, edge-on, wide - at VRAM tiles $86, $A2 and $60,
 * palette 6, about four frames each.  The blob at $C1:0000 is uploaded
 * from tile 48, so a tile's bytes are at (tile - 48) * 32 in it; the port
 * already decompresses that blob for the HUD and the kart shadow.
 *
 * The MOTION is OURS and ledgered (S29).  The game's own launch constants
 * live in an effect slot this log could not pin down - $0FE2 does not
 * point at it - and under the user's standing rule for presentation
 * ("faithful is for driving experience, not for hud, menus, and things
 * that can be better without constraints") a measured-looking arc is
 * worth more than another afternoon of hunting. */
#define SMK_COIN_FRAMES  3
#define SMK_COIN_PX     16
typedef struct { bool ok; uint8_t px[SMK_COIN_FRAMES][SMK_COIN_PX * SMK_COIN_PX]; } smk_coinart;
bool smk_coin_load(const smk_rom *rom, smk_coinart *out);

#define SMK_COINFX_MAX   12
#define SMK_COIN_RISE   320     /* OURS: launch, in the kart's own z units */
#define SMK_COIN_GRAV    26     /* the kart's own gravity ($80B1D5)        */
#define SMK_COIN_SPIN     4     /* frames per spin frame, measured         */
#define SMK_COIN_LIFE   150     /* frames before it gives up               */
typedef struct {
    int      live;
    int32_t  x, y, z;       /* kept for the world-space fallback          */
    int16_t  vx, vy, vz;
    int      t;             /* frames since the loss                       */
    int      side;          /* which way this coin's path drifts           */
    uint8_t  kind;          /* 0 spilled by a bump, 1 picked up from the road */
} smk_coin;
/* The path a spilled coin takes on screen, relative to the top centre of
 * the player's kart sprite - CAPTURED from a real bump (NOTES 186).  The
 * game does not throw the coin off the kart: it appears four frames later
 * far up the screen, rises, falls, bounces once and is gone after 44
 * frames.  The port replays that, mirrored left/right per coin. */
#define SMK_COIN_DELAY   4
typedef struct { int16_t dx, dy; uint8_t frame; } smk_coin_step;
extern const smk_coin_step SMK_COIN_PATH[];
extern const int SMK_COIN_PATH_LEN;
/* and the picked-up coin's hop (src/coinup_path.inc, NOTES 189) */
extern const smk_coin_step SMK_COINUP_PATH[];
extern const int SMK_COINUP_PATH_LEN;
#define SMK_COINUP_DELAY 2
void smk_coinfx_pickup(smk_coin *c, int n);
void smk_coinfx_pickup2(smk_coin *c, int n);   /* the 2-coin item: the hop, doubled */
/* `count` coins out of a kart at (x,y) travelling on `heading` */
void smk_coinfx_spawn(smk_coin *c, int n, int32_t x, int32_t y,
                      uint16_t heading, int16_t kvx, int16_t kvy, int count);
void smk_coinfx_step(smk_coin *c, int n);

/* ---- Ground effects: tyre smoke and dust (src/effects.c, NOTES 109) ---- */
typedef struct { int n; int8_t x[8], y[8]; uint8_t tile[8], attr[8]; } smk_effect_template;
typedef struct { int n; uint8_t dur[8], tpl[8]; smk_effect_template t[8]; } smk_effect_script;
typedef struct { bool valid; uint8_t attr_xor; smk_effect_script script[12]; } smk_effect_kind;
typedef struct {
    bool ok;
    uint8_t tiles[32][64];         /* VRAM $100..$11F, palette indices */
    uint8_t lo[64][64];            /* sprite tiles $000..$03F: the stream at $C0:0903
                                    * (NOTES 197) - Lakitu's cloud puffs, which the
                                    * spray (kind $06) and splash (kind $12) assemble */
    int16_t wobble[8];             /* $80D46F, by frame counter & 7     */
    smk_effect_kind kind[12];      /* by record offset / 6, kinds $00..$42 */       /* by record offset / 6              */
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
#define SMK_FONT_PALS  32              /* two CGRAM sets, 16 palettes each */
typedef struct {
    uint8_t  px[SMK_FONT_TILES][64];   /* 2bpp values 0..3, from $C7:0000 */
    uint32_t pal[SMK_FONT_PALS][16];   /* the menu's own BG palettes      */
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
    SMK_UI_RACE, SMK_UI_RESULT, SMK_UI_STANDINGS
} smk_ui_screen;
#define SMK_MODE_GP    0
#define SMK_MODE_TT    4

/* Which grid slot a kart BLOCK starts in.
 *
 * MEASURED with NOTES 161's rig: block $1000 - P1 - takes the LAST row
 * of the grid and $1700 the pole, so the block index counts backwards
 * from the front.  The port's racers[] is indexed by block, exactly like
 * smk_grid_order's output, so this is the bridge between the two. */
#define SMK_GRID_SLOT(block)  (SMK_CHARACTERS - 1 - (block))
typedef struct { bool up, down, left, right, confirm, back; } smk_ui_input;
/* The mode rows on the select screen.  A single race is a Grand Prix
 * course run on its own - same eight karts, same grid, same coins - so
 * it hands the race SMK_MODE_GP; only the cup around it is missing. */
enum { SMK_UI_MODE_GP, SMK_UI_MODE_RACE, SMK_UI_MODE_TT, SMK_UI_MODES };
typedef struct {
    smk_ui_screen screen;
    int  mode_sel;        /* SMK_UI_MODE_*; Grand Prix is disabled */
    int  player_sel;      /* SMK_DRIVERS index                     */
    int  cup_sel, course_sel;
    int  engine_class;
    int  track;           /* resolved on confirm                   */
    unsigned tick;        /* blink phase                           */
    bool denied;          /* flash (unused now that the cup is built) */
    int  denied_t;
    /* THE CUP (NOTES 198): five courses in the cup's order, points to the
     * top four from the ROM's own table at $85:BEB4 (9 6 3 1 - the results
     * code at $85:C0C6 reads it for rank indices under 4 only), standings
     * between races, a retry of the same course when the player ranks out
     * of the top four. */
    bool gp;              /* a Grand Prix is running                 */
    int  gp_race;         /* 0..4 within the cup                     */
    int  gp_points[SMK_CHARACTERS];   /* by SMK_DRIVERS index        */
    int  gp_place[SMK_CHARACTERS];    /* last race's place, by driver */
    bool ranked_out;      /* the player finished 5th or worse        */
    int  gp_pts_table[4]; /* $85:BEB4                                */
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
    int  position;        /* finishing place in a race, 0 in a time trial */
    /* Every kart's race, not just the player's.  The user asked for it:
     * "after that, you get times: your times, and the AI's total times
     * and positions after the race". */
    struct {
        int  character;   /* SMK_DRIVERS index                            */
        long total;       /* race frames at its last crossing, -1 if DNF  */
        int  player;      /* the one the human drove                      */
    } field[SMK_CHARACTERS];
    int  entries;         /* how many of field[] are filled (0 = trial)   */
} smk_ui_result;
void smk_ui_gp_award(smk_ui *ui, const smk_ui_result *res);
void smk_ui_draw_standings(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                           uint32_t *fb, int w, int h);
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

/* ---- Items (src/item.c, docs/ITEMS.md) ---------------------------------
 *
 * The item word $0D70,y, DECODED from $81:B387 and MEASURED against the
 * user's recorded races: a box starts a roulette that steps every four
 * frames through a SEQUENCE for 193 frames, then keeps stepping until it
 * shows the TARGET chosen when the box was hit; 64 frames of blinking
 * hold; then READY until the button fires it.  Which target: the track's
 * probability block ($81:B471 by $81:8B73[track]), a record by lap and
 * rank ($81:B666), and five random bits against eight thresholds. */
#define SMK_ITEM_MUSHROOM  0
#define SMK_ITEM_FEATHER   1
#define SMK_ITEM_STAR      2
#define SMK_ITEM_BANANA    3
#define SMK_ITEM_GREEN     4
#define SMK_ITEM_RED       5
#define SMK_ITEM_BOO       6
#define SMK_ITEM_COIN      7
#define SMK_ITEM_LIGHTNING 8
#define SMK_ITEMS          9
#define SMK_ITEM_SEQS      7
#define SMK_ITEM_BLOCKS    8
#define SMK_ITEM_ROULETTE  0xC1     /* MEASURED (both races): 193 frames of free
                                       spinning.  $81:B362's $E1 is the BATTLE box */
#define SMK_ITEM_HOLD      0x40     /* $81:B3E6: frames of blinking hold   */
typedef struct {
    bool    ok;
    uint8_t seq[SMK_ITEM_SEQS][12];     /* ids, terminated by 0xFF          */
    uint8_t seq_len[SMK_ITEM_SEQS];
    uint8_t block[SMK_ITEM_BLOCKS][27]; /* 3 records x (8 thresholds + meta)*/
    uint8_t rec_by_lap_rank[40];        /* $81:B666: 0 / 9 / 18             */
    uint8_t block_of_track[20];         /* $81:8B73 >> 1                    */
} smk_itemtab;
bool smk_items_load(const smk_rom *rom, smk_itemtab *t);
extern const smk_itemtab *smk_item_tables;   /* set once loaded; smk_item_step reads it */

typedef struct {
    uint16_t word;      /* $0D70: $8000 present, $2000 spinning, $4000 ready, $1000 empty-flash; low byte = id shown */
    int16_t  timer;     /* $0D78 */
    int      seq;       /* which sequence the roulette cycles             */
    int      cursor;    /* index into it                                   */
    int      target;    /* $0D7C: the id it will land on                   */
} smk_item;
/* The box was hit: choose the outcome and start the roulette.  `roll` is
 * five random bits (OURS - the game's $1F26 is not reproduced). */
void smk_item_box(smk_item *it, const smk_itemtab *t, int track, int lap,
                  int rank, unsigned roll);
/* One frame.  `button` is the item button HELD (the ROM tests the level,
 * $81:B3C1 / $81:B40A); `can_use` is the $81:B3FB gate (grounded, free).
 * Returns the id fired this frame, or -1. */
int  smk_item_step(smk_item *it, bool button, bool can_use);
static inline bool smk_item_present(const smk_item *it) { return (it->word & 0x8000) != 0; }
static inline int  smk_item_shown(const smk_item *it)   { return it->word & 0xFF; }
/* $0D78 & 8 while held: the icon blinks */
static inline bool smk_item_blink(const smk_item *it)
{ return (it->word & 0xE000) == 0x8000 && (it->timer & 8) != 0; }

/* ---- The item icons (docs/ITEMS.md §7) ---------------------------------
 *
 * The slot is four 2bpp BG tiles, not sprites - $81:B31C's $0C26/$0C28/
 * $0C66/$0C68 are cells of the HUD tilemap, one row apart - which is why
 * no OAM dump ever showed it.  The tiles are at VRAM word $7000, and the
 * mushroom decoded there in one look.  Their source is the compressed blob
 * at $C1:12F0 (1792 bytes = 112 tiles), found by decompressing every start
 * in banks $C0-$C7 and searching for the tile VRAM held: VRAM tile n is
 * blob tile n - $80.  $81:B320 names the four tiles and the palette per
 * item: (t, t+1) over (t+2, t+3), BG palette (attr >> 2) & 7. */
#define SMK_ICON_SRC    0xC112F0u
#define SMK_ICON_TILES  112
#define SMK_ICON_BASE   0x80          /* VRAM tile - this = blob tile */
/* The HUD strip is MODE 0, where each background owns a 32-colour block,
 * and the item box is on BG2: its palettes are CGRAM 32 + pal*4.  Read
 * off the block that makes the user's screenshot - pal 4 there is
 * sky/white/GREEN/black (the green shell), 5 white/red/black (red shell,
 * mushroom), 6 white/yellow/black (banana, coin, star, lightning), 7
 * white/light-blue/black (the frame).  BG3's block at 64 gave a blue
 * shell, and block 0 a dark red one. */
#define SMK_HUD_BG_PAL  32
typedef struct {
    bool    ok;
    uint8_t px[SMK_ICON_TILES][64];   /* 2bpp -> palette index 0..3   */
    uint8_t tile[SMK_ITEMS + 2];      /* $81:B320: per id; then blank, then the empty box */
    uint8_t pal[SMK_ITEMS + 2];
} smk_itemicons;
bool smk_itemicons_load(const smk_rom *rom, smk_itemicons *out);

/* ---- The projectiles' art (docs/ITEMS.md §7) ----------------------------
 *
 * Found through OAM after all: the shells are ALWAYS in OAM, parked off
 * screen at x = 319 when unused - which is why a set-difference against a
 * no-item frame showed nothing - and they are the SMALL sprite size.
 * Green is VRAM tile $FC, red $FE, the banana $F9, sprite palette 4.  The
 * shells' bytes are in the shared blob at $C1:0000 (tile n - $EF); the
 * banana's in the blob at $C1:4552 (tile 68). */
/* CORRECTED: in this OBSEL the small sprite is 8x8 and the large 16x16,
 * so a shell is ONE tile - $FC green, $FE red, $F9 the banana - and the
 * "16x16" karts are the four-quadrant 16x16s of NOTES 182. */
typedef struct { bool ok; uint8_t px[3][8 * 8]; } smk_projart;     /* banana, green, red */
/* The road items' art: size ladders as the game's own scaler draws them,
 * imported from a rip of that output (src/itemart.inc, NOTES 192) since
 * the ROM holds no tiles for them.  Palette-indexed against the OBJ
 * palettes, 0 transparent. */
typedef struct { uint8_t w, h; uint8_t px[16 * 16]; } smk_itemart_tier;
/* which tier of a ladder for a wanted width in native px: nearest */
static inline int smk_itemart_pick(const smk_itemart_tier *t, int n, float want)
{
    int best = 0;
    for (int i = 1; i < n; i++)
        if ((want - (float)t[i].w) * (want - (float)t[i].w)
            < (want - (float)t[best].w) * (want - (float)t[best].w)) best = i;
    return best;
}
bool smk_projart_load(const smk_rom *rom, smk_projart *out);
#define SMK_PROJ_PAL 4

/* ---- Projectiles: bananas and shells (src/projectile.c, docs/ITEMS.md §5) --
 *
 * MEASURED in the oracle (tools/labs/itemfx.py): a shell leaves at the
 * kart's heading with the kart's speed + $300, tracks the kart for three
 * frames, then flies straight; a wall reflects the hit component at 7/8;
 * a red shell steers toward its target at $0400 an angle-unit per frame,
 * snapping inside $0800, after an 8-frame delay ($81:9EC2); a dropped
 * banana sits eight pixels behind the kart until something hits it.  The
 * game keeps two of them per player ($80:F174). */
#define SMK_PROJ_MAX        4
#define SMK_PROJ_NONE       0
#define SMK_PROJ_BANANA     1     /* on the road                        */
#define SMK_PROJ_BANANA_AIR 2     /* thrown ahead, in flight            */
#define SMK_PROJ_GREEN      3
#define SMK_PROJ_RED        4
#define SMK_PROJ_MUSHROOM   5     /* Peach / Toad: the poison mushroom  */
#define SMK_PROJ_EGG        6     /* Yoshi                              */
#define SMK_PROJ_FIREBALL   7     /* Bowser: the one that moves         */
/* The AI's own weapons (NOTES 190, the user's `attack` recording): one per
 * character, used only against the player, only from lap 2, only when the
 * player is near.  The object rides behind the kart for SMK_AI_CARRY
 * frames at exactly the kart's velocity, then is let go where it is. */
#define SMK_AI_CARRY        58    /* MEASURED: 58 frames at the kart's velocity, then still */
#define SMK_AI_NEAR         160   /* px; MEASURED drops at 53..153 - OURS as a bound */
#define SMK_AI_COOL         640   /* frames; MEASURED intervals 646/874/884 - OURS as a floor */
#define SMK_FIRE_WEAVE_AMP  20    /* OURS (the user: "fireballs move sideways"): px either side */
#define SMK_FIRE_WEAVE_T    96    /* OURS: frames per full weave (40 was "too fast" - the user)   */
#define SMK_AI_WEAPON_NONE  0
#define SMK_AI_WEAPON_STAR  100   /* not a projectile kind */
int  smk_ai_weapon_of(int character);    /* SMK_PROJ_* kind, or SMK_AI_WEAPON_STAR / NONE */
#define SMK_PROJ_SPEED_ADD  0x300 /* MEASURED: kart speed + $300         */
#define SMK_PROJ_RED_DELAY  8     /* $40,x                              */
#define SMK_PROJ_RED_TURN   0x0400
#define SMK_PROJ_RED_BAND   0x0800
#define SMK_PROJ_BOUNCE_NUM 7     /* MEASURED once: 1286 -> 1125 = 7/8   */
#define SMK_PROJ_DIE_HOP    0x0100 /* $80:F85D: it hops as it dies       */
#define SMK_PROJ_OWNER_SAFE 60    /* $66 = $3C: frames the owner is immune */
#define SMK_PROJ_HIT_R      5     /* OURS, labelled: contact half-box in world px (was 8: "huge" - the user) */
#define SMK_PROJ_MAX_BOUNCE 8     /* OURS, labelled: a green gives up     */
#define SMK_PROJ_BANANA_AIR_T 24  /* OURS until chain2 lands: flight frames */
typedef struct {
    int      kind;
    int32_t  x, y;          /* kart units (px << SMK_POS_SHIFT)         */
    int32_t  z;             /* kart z units                              */
    int16_t  vx, vy, zv;
    uint16_t heading;
    int16_t  speed;
    int      owner;         /* racers[] index that threw it              */
    int      target;        /* red: racers[] index it homes on           */
    int      t;             /* age in frames                             */
    int      delay;         /* red: $40 countdown                        */
    int      bounces;
    bool     dying;         /* hopping out of existence                  */
    int      carry;         /* AI drop: frames still riding behind its kart */
    int32_t  wx, wy;        /* the fireball's weave offset, kart units       */
    int      safe;          /* frames the owner cannot touch it          */
} smk_proj;
void smk_proj_ai_drop(smk_proj *list, int n, int kind, const smk_kart *k, int owner);
void smk_proj_throw(smk_proj *list, int n, int kind, const smk_kart *k,
                    uint16_t heading, int owner, int target, bool backward,
                    bool ahead);
/* one frame; `karts` indexed like racers[] for the red shell's target */
void smk_proj_step(smk_proj *list, int n, const smk_track *trk,
                   const smk_kart *const *karts, int nkarts);
/* does any live projectile touch this kart?  Returns its kind (and
 * starts it dying) or SMK_PROJ_NONE.  The owner is immune for a while. */
int  smk_proj_hit(smk_proj *list, int n, const smk_kart *k, int kart_index);
